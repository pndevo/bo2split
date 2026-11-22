// XINPUT1_3.dll proxy for Black Ops 2 Zombies (t6zm.exe): split-screen co-op for up to four
// players in one game process. The DLL forwards XInput to the system DLL and adds the local
// split-screen lobby (offline sign-in patches), the extra players as shadow clients of the
// listen server, a second to fourth view pass with its own HUD, and the pad and keyboard
// input of each player. Settings come from bo2ss.ini, the log goes to bo2ss.log, both next to
// this DLL.
//
// Supported game image (identified by PE timestamp at 0x00400000):
//   - Steam t6zm.exe (0x6a8332d9): everything above
// Only the Steam build is supported. A table entry and the window-timer driver below exist for
// Plutonium T6 r5354 (0x523dfd3e); that path is untested, installs no patches and only logs.
//
// There is no worker thread. The game polls XInputGetState every frame on its main
// thread, so patching (once) and the per-frame work happen there. Under Steam the code is
// SteamStub-encrypted at DllMain time but decrypted by the first XInput call. The window timer
// is meant for hosts that do not poll this proxy; it is untested.

#include "core/common.h"
#include "shadow/shadow.h"
#include "core/dev.h"
#include "core/version.h"
#include "engine/common.h"
#include "engine/client.h"

// ---------------------------------------------------------------------------
// Exception logging

static volatile LONG g_exceptionsLogged;
uintptr_t g_selfBase, g_selfEnd;

static LONG CALLBACK OnException(EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    // Faults inside this DLL come from ReadSafe and are handled there.
    if ((uintptr_t)r->ExceptionAddress >= g_selfBase && (uintptr_t)r->ExceptionAddress < g_selfEnd)
        return EXCEPTION_CONTINUE_SEARCH;
    // Skip debugger/C++/thread-naming noise; keep hardware faults.
    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW: break;
    default: return EXCEPTION_CONTINUE_SEARCH; // includes privileged-instruction probes
    }
    if (InterlockedIncrement(&g_exceptionsLogged) > 40) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = ep->ContextRecord;
    uintptr_t info0 = r->NumberParameters > 0 ? r->ExceptionInformation[0] : 0;
    uintptr_t info1 = r->NumberParameters > 1 ? r->ExceptionInformation[1] : 0;
    Log("EXCEPTION %08lx at eip=%08lx (%s %08lx) eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx "
        "esp=%08lx",
        r->ExceptionCode, c->Eip, info0 == 1 ? "write" : (info0 == 8 ? "exec" : "read"), info1, c->Eax, c->Ebx, c->Ecx,
        c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    uint32_t stack[8] = {};
    if (ReadSafe(c->Esp, stack, sizeof(stack)))
        Log("  stack: %08x %08x %08x %08x %08x %08x %08x %08x", stack[0], stack[1], stack[2], stack[3], stack[4],
            stack[5], stack[6], stack[7]);
    // Return addresses: every word of the first 96 on the stack that points into the game's
    // code, in order (the callers, mixed with stale values).
    uint32_t deep[96] = {};
    if (ReadSafe(c->Esp, deep, sizeof(deep))) {
        char line[512];
        int at = 0;
        for (int i = 0; i < 96 && at < (int)sizeof(line) - 16; ++i)
            if (deep[i] >= 0x00401000 && deep[i] < 0x00a00000)
                at += sprintf_s(line + at, sizeof(line) - at, " +%x:%08x", i * 4, deep[i]);
        Log("  code addresses on the stack:%s", line);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------------------
// Supported game builds

struct GameBuild {
    const char* name;
    uint32_t timestamp;         // PE timestamp of the image at 0x00400000
    uintptr_t localClientCount; // stored count after hunk allocation
    uintptr_t clientUIActive0;  // flags of local client 0 (bit 0 = active), state at +8
    uintptr_t cgPtr, cgsPtr, clPtr, clcPtr;
    uintptr_t controllerMap0; // lcn -> controller map, slot 0 (stride 0x18)
    uintptr_t primaryController;
    const BytePatch* patches;
    size_t patchCount;
    const Detour* driverHook; // per-frame driver, always installed
    const Detour* traceHooks; // logging hooks, installed when bo2ss.ini trace=1
    size_t traceHookCount;
};

static const GameBuild kBuilds[] = {
    {"Steam t6zm.exe", 0x6a8332d9, engine::kLocalClientCount, engine::kUiState, engine::kCg, engine::kCgs,
     engine::kClPtr, engine::kClcPtr, engine::kCtrlMap, engine::kPrimaryController, kSignInPatches,
     sizeof(kSignInPatches) / sizeof(kSignInPatches[0]), &kGamepadDriverHook, kSteamTraceHooks,
     sizeof(kSteamTraceHooks) / sizeof(kSteamTraceHooks[0])},
    // Plutonium: untested, not supported. No patches; the entry only reads the globals and logs.
    {"Plutonium T6 r5354", 0x523dfd3e, 0x0119cb7c, 0x0119c650, 0x01113f9c, 0x01113f78, 0x0119cb84, 0x0119cb88,
     0x025323f0, 0x00d47a10, nullptr, 0, nullptr, nullptr, 0},
};

// ---------------------------------------------------------------------------
// Per-frame work (game main thread)

static bool g_active; // set in DllMain for supported hosts
static bool g_initDone;
static bool g_passive; // Steam build while the Steam client is online: nothing is patched or hooked
static const GameBuild* g_build;
static DWORD g_nextStateLog;
static uint32_t g_lastState[10];
static unsigned g_frames;

static void InitOnGameThread() {
    uint32_t ts = ImageTimestamp(engine::kImageBase);
    if (ts == 0x6a8332d9) {
        // The Steam client's state decides everything below, so wait for steam_api (at most
        // 3 s). Online the game stays as it is: no patch, no hook; the only work is saving
        // the script update the game fetches (ScriptUpdateWatch).
        static DWORD firstTry;
        if (!firstTry) firstTry = GetTickCount();
        bool known;
        bool online = SteamIsLoggedOn(&known);
        if (!known && GetTickCount() - firstTry < 3000) return;
        if (known && online) {
            g_initDone = true;
            g_passive = true;
            for (const GameBuild& b : kBuilds)
                if (b.timestamp == ts) g_build = &b;
            Log("game build: %s (timestamp %08x)", g_build->name, ts);
            Log("steam client: ONLINE, the mod stays passive (nothing patched, nothing hooked); script update on "
                "disk: %s",
                ScriptUpdateOnDisk() ? "yes" : "no, it is saved once the game has fetched it");
            ScriptUpdateWatch();
            UpdateCheckStart();
            return;
        }
    }
    g_initDone = true;
    for (const GameBuild& b : kBuilds)
        if (b.timestamp == ts) g_build = &b;
    if (!g_build) {
        Log("unknown game image at 0x00400000 (timestamp %08x); nothing patched", ts);
        return;
    }
    Log("game build: %s (timestamp %08x), first XInput call on thread %lu", g_build->name, ts, GetCurrentThreadId());
    for (size_t i = 0; i < g_build->patchCount; ++i)
        ApplyBytePatch(g_build->patches[i]);
    if (g_build->timestamp == 0x6a8332d9 && ReadIniInt(L"offline_fake_live", 1)) InstallFakeLiveJump();
    if (g_build->timestamp == 0x6a8332d9 && LobbyJoinOn()) InstallDetour(kLobbyHintHook);
    if (g_build->driverHook) InstallDetour(*g_build->driverHook);
    if (TraceOn()) {
        g_stubPage = (uint8_t*)VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        for (size_t i = 0; i < g_build->traceHookCount; ++i)
            InstallDetour(g_build->traceHooks[i]);
    }
    if (g_build->timestamp == 0x6a8332d9 && (DevWantsPadHook() || ReadIniInt(L"p2_input", 1)))
        InstallDetour(kGpadInjectHook);
    if (g_build->timestamp == 0x6a8332d9 && ReadIniInt(L"p2_view_hook", 1)) {
        for (const Detour& d : kViewHooks)
            InstallDetour(d);
        for (const Detour& d : kViewPsHooks)
            InstallDetour(d);
        for (const Detour& d : kViewEntityHooks)
            InstallDetour(d);
        for (const Detour& d : kViewPredictHooks)
            InstallDetour(d);
        for (const Detour& d : kViewSceneHooks)
            InstallDetour(d);
        for (const Detour& d : kHudHooks)
            InstallDetour(d);
        InstallDetour(kHudScoreHook);
        InstallDetour(kHudHealthHook);
        InstallDetour(kHudVisBitsHook);
        InstallDetour(kLuiErrorHook);
        InstallDetour(kHudFrameHook);
        InstallDetour(kHudMouseHook);
        InstallDetour(kSoundListenerHook);
        for (const Detour& d : kSoundFireHooks)
            InstallDetour(d);
        for (const Detour& d : kHudCfHooks)
            InstallDetour(d);
        for (const Detour& d : kCgSwapHooks)
            InstallDetour(d);
        for (const Detour& d : kGumpHooks)
            InstallDetour(d);
        for (const Detour& d : kControlsHooks)
            InstallDetour(d);
        for (const Detour& d : kPlayerMenuHooks)
            InstallDetour(d);
        for (const Detour& d : kRumbleHooks)
            InstallDetour(d);
        if (ReadIniInt(L"p2_input", 1) && ReadIniInt(L"p_input_hooks", 1))
            for (const Detour& d : kInputHooks)
                InstallDetour(d);
        for (const Detour& d : kFxHooks)
            InstallDetour(d);
        for (const BytePatch& bp : kViewLoopPatches)
            ApplyBytePatch(bp);
        EnlargeCommandBuffers();
    }
    if (g_build->timestamp == 0x6a8332d9 && ReadIniInt(L"p2_enable", 1)) {
        g_p2.enabled = true;
        for (const Detour& d : kShadowClientHooks)
            InstallDetour(d);
    }
    DevInstallHooks(g_build->timestamp == 0x6a8332d9);
    if (g_build->timestamp == 0x6a8332d9) UpdateCheckStart();
    for (DWORD pad = 0; pad < 4; ++pad) {
        uint8_t state[16];
        Log("pad %lu: %s", pad, pGetState && pGetState(pad, state) == ERROR_SUCCESS ? "connected" : "not connected");
    }
    bool known;
    bool online = SteamIsLoggedOn(&known);
    Log("steam client: %s",
        known ? (online ? "ONLINE (patched actions will be refused)" : "offline mode") : "not initialised yet");
}

static void LogStateIfChanged() {
    const GameBuild& b = *g_build;
    uint32_t now[10] = {
        Read32(b.localClientCount),
        Read32(b.clientUIActive0),
        Read32(b.clientUIActive0 + 8),
        Read32(b.cgPtr),
        Read32(b.cgsPtr),
        Read32(b.clPtr),
        Read32(b.clcPtr),
        Read32(b.controllerMap0),
        Read32(b.controllerMap0 + 8),
        Read32(b.primaryController),
    };
    if (memcmp(now, g_lastState, sizeof(now)) == 0) return;
    memcpy(g_lastState, now, sizeof(now));
    if (TraceOn())
        Log("state (frame %u): count=%u ui0.flags=%08x ui0.state=%u cg=%08x cgs=%08x cl=%08x clc=%08x map0.flags=%08x "
            "map0.ctrl=%u primary=%d sessionmode=%08x",
            g_frames, now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7], now[8], (int)now[9],
            b.timestamp == 0x6a8332d9 ? Read32(engine::kSessionMode) : 0);
}

// Second driver, for hosts where XInputGetState never reaches this proxy (Plutonium is
// expected to poll through xinput1_4; untested, only the Steam build is supported). A CBT hook on
// the loading thread catches the game window's creation; a timer on that window then calls
// OnFrame on the thread that owns the window.

static HHOOK g_cbtHook;
static constexpr UINT_PTR kTimerId = 0xB025;

static void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD) {
    OnFrame();
}

static LRESULT CALLBACK OnCbt(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        const CBT_CREATEWNDW* cw = (const CBT_CREATEWNDW*)lp;
        const bool topLevel = !(cw->lpcs->style & WS_CHILD) && cw->lpcs->hwndParent == nullptr;
        if (topLevel && SetTimer((HWND)wp, kTimerId, 100, OnTimer)) {
            Log("timer attached to window %p on thread %lu", (void*)wp, GetCurrentThreadId());
            UnhookWindowsHookEx(g_cbtHook);
            g_cbtHook = nullptr;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// The DLL may be loaded on a thread other than the game's main thread (untested), so the
// CBT hook alone can miss the window. A message hook on every existing thread fires on
// whichever thread pumps the game window's messages (once per frame).
static LRESULT CALLBACK OnGetMsg(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) OnFrame();
    return CallNextHookEx(nullptr, code, wp, lp);
}

static int HookAllThreadMessages() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    const DWORD pid = GetCurrentProcessId();
    int hooked = 0;
    THREADENTRY32 te = {sizeof(te)};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && SetWindowsHookExW(WH_GETMESSAGE, OnGetMsg, nullptr, te.th32ThreadID))
            ++hooked;
    CloseHandle(snap);
    return hooked;
}

static DWORD g_frameThread;

void OnFrame() {
    if (!g_active) return;
    // Several drivers may call in; keep to the first thread that does.
    if (!g_frameThread) {
        g_frameThread = GetCurrentThreadId();
        Log("per-frame driver: thread %lu", g_frameThread);
    } else if (g_frameThread != GetCurrentThreadId()) {
        return;
    }
    if (!g_initDone) InitOnGameThread();
    if (!g_build) return;
    if (g_passive) return;
    if (g_p2.inContext) ShadowStop("context still active in the gamepad hook");
    ++g_frames;
    DWORD t = GetTickCount();
    // Optional: trigger the Online-button action without a click (Steam build only).
    static DWORD initTick;
    static bool autoDone;
    if (!initTick) initTick = t;
    if (!autoDone && g_build->timestamp == 0x6a8332d9) {
        int after = ReadIniInt(L"auto_menu_after_ms", 1000);
        if (after > 0 && (LONG)(t - initTick) >= after) {
            autoDone = true;
            Log("[bo2ss] auto trigger after %d ms", after);
            OfflineOnlineButton();
        }
    }
    static bool startDone;
    if (!startDone && g_lobbyOpened) {
        int after = ReadIniInt(L"auto_start_after_ms", 0);
        if (after > 0 && (LONG)(t - g_lobbyOpenedTick) >= after) {
            startDone = true;
            AutoStartMatch();
        }
    }
    // Shadow client for player 2: start once player 1 is in game (connstate 10) for a while.
    static DWORD inGameSince;
    if (g_p2.enabled && !g_p2.started &&
        !(LobbyJoinOn() && LobbyJoined() == 0)) { // lobby_join: nobody joined = player 1 alone
        if (Read32(g_build->clientUIActive0 + 8) == 10) {
            if (!inGameSince) inGameSince = t;
            int after = ReadIniInt(L"p2_connect_after_ms", 300);
            if ((LONG)(t - inGameSince) >= after) {
                if (P2AllocateClient())
                    P2StartConnect();
                else
                    g_p2.enabled = false;
            }
        } else {
            inGameSince = 0;
        }
    }
    DevFrame(g_build->clientUIActive0);
    PInputFrame();       // logs the device per player when it changes
    ShadowRumbleFrame(); // vibration of the shadow players' pads
    LobbyFrame();        // lobby_join=1: pads join and leave in the lobby
    // perf_log=1: every 5 s the frame rate, the ini lookups per frame and how many of them
    // went to the file.
    {
        static LARGE_INTEGER last, freq;
        static unsigned lastFrames, lastRender;
        static LONG lastCalls, lastReads;
        static DWORD next;
        if ((LONG)(t - next) >= 0) {
            next = t + 5000;
            LARGE_INTEGER nowQ;
            QueryPerformanceCounter(&nowQ);
            if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
            unsigned frames = g_frames - lastFrames;
            LONG calls = g_iniCalls, reads = g_iniFileReads;
            if (last.QuadPart && frames && ReadIniInt(L"perf_log", 0)) {
                double sec = (double)(nowQ.QuadPart - last.QuadPart) / (double)freq.QuadPart;
                // A frame is one run of the view passes; without view passes (no match) the calls
                // of this function count. This function runs on every pad poll of the engine,
                // which is more than once per frame (those are not counted as frames).
                unsigned render = g_renderFrames - lastRender;
                unsigned n = render ? render : frames;
                Log("[perf] %.1f fps (%.2f ms per frame), ini lookups %.1f per frame, %.2f of them from the file, %.2f "
                    "pad polls per frame",
                    n / sec, 1000.0 * sec / n, (double)(calls - lastCalls) / n, (double)(reads - lastReads) / n,
                    (double)frames / n);
                if (render) {
                    double ms = 1000.0 / (double)freq.QuadPart;
                    Log("[perf] ms per frame on the view thread: pass 0 %.2f, pass 1 %.2f, pass 2 %.2f, last pass and "
                        "rest %.2f; longest frame %.1f ms, %u of %u frames over 20 ms",
                        g_perf.pass[0] * ms / render, g_perf.pass[1] * ms / render, g_perf.pass[2] * ms / render,
                        g_perf.tail * ms / render, g_perf.maxFrame * ms, g_perf.slowFrames, render);
                }
            }
            {
                LONGLONG limit = freq.QuadPart / 50;
                memset(&g_perf, 0, sizeof(g_perf));
                g_perf.slowLimit = limit;
            }
            last = nowQ;
            lastFrames = g_frames;
            lastRender = g_renderFrames;
            lastCalls = calls;
            lastReads = reads;
        }
    }
    if ((LONG)(t - g_nextStateLog) >= 0) {
        g_nextStateLog = t + 500;
        LogStateIfChanged();
        static DWORD nextHeartbeat;
        if ((LONG)(t - nextHeartbeat) >= 0) {
            nextHeartbeat = t + 15000;
            if (TraceOn())
                Log("heartbeat: frame %u, lua engine functions registered %u, distinct called %u", g_frames,
                    g_luaRegistered, g_luaStatCount);
            DevHeartbeat(g_frames);
        }
    }
}

// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    const auto* dos = (const IMAGE_DOS_HEADER*)self;
    const auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)self + dos->e_lfanew);
    g_selfBase = (uintptr_t)self;
    g_selfEnd = g_selfBase + nt->OptionalHeader.SizeOfImage;

    // Active for the Steam exe and for the Plutonium bootstrapper (untested, not supported).
    // InitOnGameThread checks the image timestamp before touching anything.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* slash = wcsrchr(exe, L'\\');
    const wchar_t* exeName = slash ? slash + 1 : exe;
    const bool steamExe = _wcsicmp(exeName, L"t6zm.exe") == 0;
    const bool plutonium = _wcsnicmp(exeName, L"plutonium-bootstrapper", 22) == 0;
    if (!steamExe && !plutonium) return TRUE; // plain proxy for anything else

    // Log next to this DLL (the game folder), not next to the host exe.
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(self, selfPath, MAX_PATH);
    const wchar_t* selfSlash = wcsrchr(selfPath, L'\\');
    lstrcpynW(g_gameDir, selfPath, selfSlash ? (int)(selfSlash - selfPath) + 2 : 1);
    wchar_t logPath[MAX_PATH];
    lstrcpynW(logPath, g_gameDir, MAX_PATH);
    lstrcatW(logPath, L"bo2ss.log");
    InitializeCriticalSection(&g_logLock);
    g_log = _wfsopen(logPath, L"w", _SH_DENYNO); // readable while the game runs
    Log("bo2ss build 2 %s loaded into %ls", DevVariant(), exe);
    Log("version v" BO2SS_VERSION);

    AddVectoredExceptionHandler(1, OnException);
    g_active = true; // work starts at the first XInputGetState call or window timer tick
    g_cbtHook = SetWindowsHookExW(WH_CBT, OnCbt, nullptr, GetCurrentThreadId());
    Log("CBT hook on loader thread %lu: %s", GetCurrentThreadId(), g_cbtHook ? "ok" : "FAILED");
    Log("message hooks installed on %d threads", HookAllThreadMessages());
    return TRUE;
}
