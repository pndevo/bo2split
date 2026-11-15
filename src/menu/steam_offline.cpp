#include "core/common.h"
#include "core/dev.h"
#include "engine/common.h"
#include "engine/ui.h"

// Steam offline mode: the "xsigninlive" command (Live_ConnectToDemonware, 0x00870a80)
// shows PLATFORM_STEAM_OFFLINE when SteamUser()->BLoggedOn() is false.
const BytePatch kSignInPatches[] = {
    // jne connect_path -> jmp ret: return silently instead of opening the error popup.
    {"skip PLATFORM_STEAM_OFFLINE popup", L"offline_signin_fix", nullptr, 0x00870a8c, {0x75, 0x19}, {0xeb, 0x18}, 2},
    // Live connection state machine, state 10 (connected) handler 0x00870050: per frame it
    // checks the Demonware socket and otherwise resets to WIN_STATUS_ERROR. ret at entry;
    // Demonware services keep failing gracefully (status != BD_CONNECTED) and nothing is sent
    // anywhere.
    {"fake live: keep connected state",
     L"offline_fake_live",
     nullptr,
     0x00870050,
     {0x56, 0xe8, 0x2a, 0x10, 0xfd},
     {0xc3, 0x90, 0x90, 0x90, 0x90},
     5},
    // Lua Engine.IsSignedInToDemonware: call IsConnectedToDemonware -> mov al, 1.
    // Only what Lua sees changes; engine-side Demonware checks stay false.
    {"Lua IsSignedInToDemonware -> true",
     L"lua_demonware_true",
     nullptr,
     0x009426d1,
     {0xe8, 0xca, 0xe2, 0xf2, 0xff},
     {0xb0, 0x01, 0x90, 0x90, 0x90},
     5},
    // Steam_ServerInit (0x00863e10) calls SteamGameServer_Init(ip, 0x223e, port, 0xffff,
    // eServerModeAuthenticationAndSecure, "1.0.0.0"): the listen server asks Steam for a
    // VAC-secured session ("waiting on secure VAC policy" in the log). A local lobby needs
    // no authentication: push 3 -> push 1 (eServerModeNoAuthentication), so no VAC policy
    // is ever requested for a process that carries this DLL.
    {"steam game server: no authentication, no VAC",
     L"steam_server_no_vac",
     nullptr,
     0x00863e8d,
     {0x6a, 0x03, 0x0f, 0xb7, 0xf8},
     {0x6a, 0x01, 0x0f, 0xb7, 0xf8},
     5},
};

// Steam client state through steam_api.dll's flat API: SteamUser()->BLoggedOn(). True
// while the Steam client is online (not in offline mode). Everything that changes the
// game's behaviour is refused while online; this DLL is for Steam offline mode only.
bool SteamIsLoggedOn(bool* known) {
    *known = false;
    HMODULE api = GetModuleHandleW(L"steam_api.dll");
    if (!api) return false;
    typedef void*(__cdecl * SteamUser_t)();
    SteamUser_t fnSteamUser = (SteamUser_t)GetProcAddress(api, "SteamUser");
    if (!fnSteamUser) return false;
    void* user = fnSteamUser();
    if (!user) return false;
    // ISteamUser vtable: 0 GetHSteamUser, 1 BLoggedOn, 2 GetSteamID (every interface version).
    void** vtable = *(void***)user;
    typedef bool(__fastcall * BLoggedOn_t)(void* self, void* unused);
    *known = true;
    return ((BLoggedOn_t)vtable[1])(user, nullptr);
}

// --- Steam build trace hooks -------------------------------------------------

// Com_PrintMessage(channel, text, flags): every console line the engine prints.
// Identical consecutive lines are collapsed into a repeat count.
typedef void(__cdecl* PrintMessage_t)(int, const char*, int);
static PrintMessage_t oPrintMessage;
static char g_lastCon[512];
static unsigned g_lastConRepeats;
static void __cdecl HookPrintMessage(int channel, const char* text, int flags) {
    // Diagnostic: the moment a script asset is reported missing, list every loaded
    // scriptparsetree asset (output lands in this log through the same hook).
    static bool listed;
    if (text && !listed && strstr(text, "Could not load scriptparsetree")) {
        listed = true;
        oPrintMessage(channel, text, flags);
        Log("[bo2ss] listing loaded scriptparsetree assets");
        ExecCmd("listassetpool 48"); // 48 = scriptparsetree in the XAssetType name table at 0x00c86cc0
        return;
    }
    if (text) {
        char buf[512];
        lstrcpynA(buf, text, sizeof(buf));
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
        if (n) {
            EnterCriticalSection(&g_logLock);
            if (strcmp(buf, g_lastCon) == 0) {
                ++g_lastConRepeats;
            } else {
                if (g_lastConRepeats) Log("  (last line repeated %u more times)", g_lastConRepeats);
                g_lastConRepeats = 0;
                lstrcpynA(g_lastCon, buf, sizeof(g_lastCon));
                Log("[con %2d] %s", channel, buf);
            }
            LeaveCriticalSection(&g_logLock);
        }
    }
    oPrintMessage(channel, text, flags);
}

// Lua "Engine.<name>" registration (name, lua_CFunction). Each function is wrapped in a
// stub that logs the name, so the Lua menu scripts' engine calls become visible.
typedef void(__cdecl* LuaRegister_t)(const char*, void*);
static LuaRegister_t oLuaRegister;
uint8_t* g_stubPage;
static size_t g_stubUsed;
unsigned g_luaRegistered;

struct LuaCallStat {
    const char* name;
    unsigned calls;
};
static LuaCallStat g_luaStats[1024];
unsigned g_luaStatCount;
static const char* g_lastLuaName;
static unsigned g_luaLinesLogged;

static bool LuaNameInteresting(const char* n) {
    static const char* keys[] = {"live",    "demonware", "dw",    "party", "lobby",   "sign", "online", "exec",
                                 "session", "match",     "popup", "error", "connect", "xuid", "steam",  "lan",
                                 "system",  "playlist",  "start", "join",  "host",    "solo", "local",  "menu"};
    char low[128];
    size_t i = 0;
    for (; n[i] && i < sizeof(low) - 1; ++i)
        low[i] = (char)((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]);
    low[i] = '\0';
    for (const char* k : keys)
        if (strstr(low, k)) return true;
    return false;
}

static void __cdecl LogLuaCall(const char* name) {
    EnterCriticalSection(&g_logLock);
    LuaCallStat* s = nullptr;
    for (unsigned i = 0; i < g_luaStatCount; ++i)
        if (g_luaStats[i].name == name) {
            s = &g_luaStats[i];
            break;
        }
    if (!s && g_luaStatCount < 1024) {
        s = &g_luaStats[g_luaStatCount++];
        s->name = name;
        s->calls = 0;
        Log("[lua first] Engine.%s", name);
    }
    if (s) ++s->calls;
    if (name != g_lastLuaName && g_luaLinesLogged < 40000 && LuaNameInteresting(name)) {
        ++g_luaLinesLogged;
        Log("[lua] Engine.%s", name);
    }
    g_lastLuaName = name;
    LeaveCriticalSection(&g_logLock);
}

static void __cdecl HookLuaRegister(const char* name, void* func) {
    if (name && func && g_stubPage && g_stubUsed + 32 <= 65536) {
        size_t len = strlen(name) + 1;
        char* copy = (char*)HeapAlloc(GetProcessHeap(), 0, len);
        if (copy) {
            memcpy(copy, name, len);
            uint8_t* s = g_stubPage + g_stubUsed;
            g_stubUsed += 32;
            // push name; call LogLuaCall; add esp, 4; jmp func
            s[0] = 0x68;
            memcpy(s + 1, &copy, 4);
            s[5] = 0xe8;
            int32_t rel = (int32_t)((uintptr_t)LogLuaCall - ((uintptr_t)s + 10));
            memcpy(s + 6, &rel, 4);
            s[10] = 0x83;
            s[11] = 0xc4;
            s[12] = 0x04;
            s[13] = 0xe9;
            rel = (int32_t)((uintptr_t)func - ((uintptr_t)s + 18));
            memcpy(s + 14, &rel, 4);
            func = s;
            ++g_luaRegistered;
        }
    }
    oLuaRegister(name, func);
}

// Cmd_ExecuteSingleCommand(localClientNum, controllerIndex, source, text).
typedef void(__cdecl* ExecCommand_t)(int, int, int, const char*);
static ExecCommand_t oExecCommand;
static void __cdecl HookExecCommand(int lc, int controller, int source, const char* text) {
    Log("[cmd lc=%d ctrl=%d] %s", lc, controller, text ? text : "(null)");
    oExecCommand(lc, controller, source, text);
}

// Not hooked: 0x009317a0 looks like an event start but is LUI_GetRootNameForController(controller),
// one argument, 108 callers.

// UpdateGamepadStatePad0 (0x0077c820) runs every frame on the main thread. Naked hook:
// registers, flags and the original's arguments stay untouched.
void* oGamepadUpdate;
static __declspec(naked) void HookGamepadUpdate() {
    __asm {
        pushad
        pushfd
        inc dword ptr [g_gamepadHookCalls]
        call OnFrame
        popfd
        popad
        jmp dword ptr [oGamepadUpdate]
    }
}
static const uint8_t kProGamepadUpdate[] = {0xa1, 0x14, 0x85, 0xa8, 0x02};

static const uint8_t kProPrintMessage[] = {0x83, 0xec, 0x40, 0x55, 0x8b, 0x6c, 0x24, 0x48};
static const uint8_t kProExecCommand[] = {0xb8, 0x0c, 0x10, 0x00, 0x00};
static const uint8_t kProLuaRegister[] = {0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c};

const Detour kGamepadDriverHook = {"UpdateGamepadStatePad0 (per-frame driver)",
                                   0x0077c820,
                                   kProGamepadUpdate,
                                   sizeof(kProGamepadUpdate),
                                   (void*)HookGamepadUpdate,
                                   &oGamepadUpdate};

const Detour kSteamTraceHooks[] = {
    {"Lua Engine function registration", 0x009485e0, kProLuaRegister, sizeof(kProLuaRegister), (void*)HookLuaRegister,
     (void**)&oLuaRegister},
    {"Com_PrintMessage", 0x006c33c0, kProPrintMessage, sizeof(kProPrintMessage), (void*)HookPrintMessage,
     (void**)&oPrintMessage},
    {"Cmd_ExecuteSingleCommand", 0x006bd2d0, kProExecCommand, sizeof(kProExecCommand), (void*)HookExecCommand,
     (void**)&oExecCommand},
};

// --- Steam build: the Online button opens the local lobby (offline_fake_live=1) ---------
//
// Online, the button runs xsigninlive -> Demonware connect -> a lobby. Offline we jump here
// from 0x00870a8e (nothing pushed yet, so plain ret works) and open the local split-screen
// lobby without any network.

namespace steam {
constexpr uintptr_t kOnlineButtonPopupSite = 0x00870a8e;
} // namespace steam

bool g_lobbyOpened;
DWORD g_lobbyOpenedTick;

void AutoStartMatch() {
    // Engine.PartyHostToggleStart: if Party_IsActive(party) && Party_IsHost(party) toggle the
    // host "start game" flag.
    bool active = engine::Party_IsActive(engine::kParty);
    bool host = engine::Party_IsHost(engine::kParty);
    Log("[bo2ss] auto start: party active=%d host=%d", active, host);
    if (active && host) *(uint8_t*)engine::kPartyHostStartFlag = 1;
}

void ExecCmd(const char* text) {
    engine::ExecCommandText(0, 0, text);
}

// LUI events, built the way the engine does it (open_menu in 0x0070d1e0, gamepad_button in
// the LUI key handler 0x00934670): begin an event on the controller's root, set the fields
// of its table, send. Game thread only.
namespace lui {
static void* Begin(int controller, const char* name, int rootController = -1) {
    void* L = *(void**)engine::kLuiState;
    if (!L) return nullptr;
    int root = engine::LUI_GetRootNameForController(rootController < 0 ? controller : rootController);
    if (!engine::LUI_BeginEvent((const char*)root, name, L)) return nullptr;
    engine::LUI_SetTableNumber("controller", controller, L);
    return L;
}
} // namespace lui

// Opens a Lua menu by its name ("SelectMapZM", "SplitscreenGameLobby", ...).
bool LuiOpenMenu(int controller, const char* menuName) {
    void* L = lui::Begin(controller, "open_menu");
    if (!L) return false;
    engine::LUI_SetTableString("menuName", menuName, L);
    engine::LUI_EndEvent(L);
    return true;
}

bool LuiEvent(int controller, const char* eventName) {
    void* L = lui::Begin(controller, eventName);
    if (!L) return false;
    engine::LUI_EndEvent(L);
    return true;
}

// A menu button as the LUI key handler sends it: button = primary, secondary, alt1, alt2,
// start, select, up, down, left, right, shoulderl, shoulderr; qualifier = "dpad", "mwheel" or null.
// rootController: whose menu root gets the event (-1 = the controller's own).
bool LuiButton(int controller, const char* button, const char* qualifier, bool down, int rootController) {
    void* L = lui::Begin(controller, "gamepad_button", rootController);
    if (!L) return false;
    engine::LUI_SetTableString("button", button, L);
    engine::LUI_SetTableBool("down", down ? 1 : 0, L);
    if (qualifier && *qualifier) engine::LUI_SetTableString("qualifier", qualifier, L);
    engine::LUI_SetTableBool("immediate", 1, L);
    engine::LUI_EndEvent(L);
    return true;
}

// Online, Demonware streams the "fix of the day" fastfile (ffotd_tu17_zm_147) at sign-in
// in 128 KB chunks that end with a 16-byte "SKID" footer (timestamp, size, chunk count,
// chunk index). The zone loader never reads this zone from disk: a zone with the ffotd
// name is read from the download buffer. So we fill that buffer from
// zone\all\ffotd_tu17_zm_147.ff and run the engine's own FFOTD load step.
namespace ffotd {
constexpr size_t kMaxSize = 0x1ffe0;
} // namespace ffotd

static void LoadFfotdFromDisk() {
    wchar_t path[MAX_PATH];
    lstrcpynW(path, g_gameDir, MAX_PATH);
    lstrcatW(path, L"zone\\all\\ffotd_tu17_zm_147.ff");
    FILE* f = _wfsopen(path, L"rb", _SH_DENYNO);
    if (!f) {
        Log("[bo2ss] ffotd: %ls not found", path);
        return;
    }
    uint8_t* buf = (uint8_t*)engine::kFfotdBuffer;
    size_t n = fread(buf, 1, ffotd::kMaxSize, f);
    fclose(f);
    size_t size = n;
    if (n >= 16 && memcmp(buf + n - 16, "SKID", 4) == 0) {
        // Assembled from chunks: the last chunk's footer is still attached.
        const uint8_t* s = buf + n - 8;
        size = ((size_t)s[0] << 24) | ((size_t)s[1] << 16) | ((size_t)s[2] << 8) | s[3];
        if (size > n) size = n - 16;
    }
    *(uint32_t*)engine::kFfotdSize = (uint32_t)size;
    *(uint8_t*)engine::kFfotdFetched = 1;
    *(uint8_t*)engine::kFfotdZoneLoaded = 0;
    *(uint8_t*)engine::kFfotdSettingsDone = 0;
    Log("[bo2ss] ffotd: %u bytes read (%u used), running the engine's FFOTD load step", (unsigned)n, (unsigned)size);
    engine::FfotdLoadStep(); // loads the zone
    engine::FfotdLoadStep(); // second pass: ffotd_settings.cfg + Lua
    Log("[bo2ss] ffotd: zone loaded=%u settings=%u", *(uint8_t*)engine::kFfotdZoneLoaded,
        *(uint8_t*)engine::kFfotdSettingsDone);
}

// Replays CoD.SwitchToLocalLobby + the map pick of SelectMapZM, then opens the
// split-screen game lobby. Nothing here touches Live/Demonware.
static void OpenLocalSplitscreenLobby(int menu) {
    char buf[128];
    Log("[bo2ss] opening local split-screen lobby (menu %d), session mode word=%08x", menu,
        Read32(engine::kSessionMode));
    ExecCmd("leaveAllParties");
    // Session mode bits: 0 offline, 1 system link, 2 online, 3 private, 4 zombies.
    // Bit 4 is normally set in WinMain from the command line; without it the engine uses
    // the MP script/zone rules (3-arg playsoundontag fails, zombie zones load partially).
    if (ReadIniInt(L"force_zombies_mode", 1) && !(Read32(engine::kSessionMode) & 0x10)) {
        engine::SessionModeSetBit(4, 1);
        Log("[bo2ss] zombies session mode bit was clear; set it. word=%08x", Read32(engine::kSessionMode));
    }
    // Engine side of the Lua calls made by CoD.SwitchToLocalLobby (ui/t6/switchlobbies.lua):
    engine::SessionModeSetBit(0, 1); // SessionModeSetOffline(true)
    engine::GameModeSetMode(2, 1);   // GameModeSetMode(LOCAL_SPLITSCREEN)
    ExecCmd("exec default_splitscreen.cfg");
    wchar_t wmap[64], wloc[64], wgt[64];
    char map[64], loc[64], gt[64];
    wchar_t ini[MAX_PATH];
    lstrcpynW(ini, g_gameDir, MAX_PATH);
    lstrcatW(ini, L"bo2ss.ini");
    GetPrivateProfileStringW(L"bo2ss", L"map", L"zm_transit", wmap, 64, ini);
    GetPrivateProfileStringW(L"bo2ss", L"startlocation", L"transit", wloc, 64, ini);
    GetPrivateProfileStringW(L"bo2ss", L"gametype", L"zclassic", wgt, 64, ini);
    WideCharToMultiByte(CP_ACP, 0, wmap, -1, map, 64, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, wloc, -1, loc, 64, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, wgt, -1, gt, 64, nullptr, nullptr);
    engine::SetGametype(gt, 1, 0);
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "set ui_mapname %s", map);
    ExecCmd(buf);
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "set ui_zm_mapstartlocation %s", loc);
    ExecCmd(buf);
    if (ReadIniInt(L"load_ffotd", 1)) LoadFfotdFromDisk();
    DevBeforeParty();
    ExecCmd("xstartlocalprivateparty");
    ExecCmd("xstartpartyhost");
    int maxPlayers = ReadIniInt(L"party_maxplayers", 4);
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "set party_maxplayers %d", maxPlayers);
    ExecCmd(buf);
    // UI_SetActiveMenu(localClientNum, menuId): 0xb = "SplitscreenGameLobby" (lobby type 2),
    // 0x14 = "PrivateLocalGameLobby" (system link, lobby type 1). Both are the console
    // offline paths; the PC main menu hides the button that leads there.
    int rc = engine::UI_SetActiveMenu(0, menu);
    Log("[bo2ss] UI_SetActiveMenu(0, %d) returned %d", menu, rc);
    g_lobbyOpened = true;
    g_lobbyOpenedTick = GetTickCount();
    if (ReadIniInt(L"list_scripts", 0)) {
        Log("[bo2ss] listing loaded scriptparsetree assets (lobby)");
        ExecCmd("listassetpool 48"); // 48 = scriptparsetree in the XAssetType name table at 0x00c86cc0
    }
}

void __cdecl OfflineOnlineButton() {
    bool known;
    bool online = SteamIsLoggedOn(&known);
    if (!known || online) {
        Log("[bo2ss] REFUSED: Steam client is %s. Put Steam into offline mode and restart the game.",
            known ? "ONLINE" : "in an unknown state (steam_api not ready)");
        return;
    }
    int menu = ReadIniInt(L"offline_menu", 11);
    OpenLocalSplitscreenLobby(menu);
}

void InstallFakeLiveJump() {
    static const uint8_t expect[] = {0x68, 0xa0, 0xd7, 0xbb, 0x00};
    if (!BytesMatch(steam::kOnlineButtonPopupSite, expect, sizeof(expect))) {
        Log("fake live: unexpected bytes at %08x, skipped", (unsigned)steam::kOnlineButtonPopupSite);
        return;
    }
    uint8_t jmp[5] = {0xe9};
    int32_t rel = (int32_t)((uintptr_t)OfflineOnlineButton - (steam::kOnlineButtonPopupSite + 5));
    memcpy(jmp + 1, &rel, 4);
    Log("fake live: Online button jump at %08x: %s", (unsigned)steam::kOnlineButtonPopupSite,
        WriteCode(steam::kOnlineButtonPopupSite, jmp, 5) ? "ok" : "FAILED");
}
