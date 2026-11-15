#include "core/common.h"
#include "shadow/shadow.h"
#include "core/dev.h"

// ---------------------------------------------------------------------------
// Real XInput

typedef DWORD(WINAPI* SetState_t)(DWORD, void*);
typedef DWORD(WINAPI* GetCaps_t)(DWORD, DWORD, void*);
typedef void(WINAPI* Enable_t)(BOOL);

static INIT_ONCE g_realOnce = INIT_ONCE_STATIC_INIT;
GetState_t pGetState;
static SetState_t pSetState;
static GetCaps_t pGetCaps;
static Enable_t pEnable;

static BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
    static const wchar_t* names[] = {L"\\xinput1_3.dll", L"\\xinput1_4.dll", L"\\xinput9_1_0.dll"};
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    for (const wchar_t* name : names) {
        wchar_t path[MAX_PATH];
        lstrcpynW(path, sys, MAX_PATH);
        if (n + lstrlenW(name) >= MAX_PATH) break;
        lstrcatW(path, name);
        HMODULE m = LoadLibraryW(path);
        if (!m) continue;
        pGetState = (GetState_t)GetProcAddress(m, "XInputGetState");
        pSetState = (SetState_t)GetProcAddress(m, "XInputSetState");
        pGetCaps = (GetCaps_t)GetProcAddress(m, "XInputGetCapabilities");
        pEnable = (Enable_t)GetProcAddress(m, "XInputEnable");
        Log("real XInput: %ls", path);
        return TRUE;
    }
    Log("real XInput: none found, controllers will read as disconnected");
    return TRUE;
}

static void EnsureReal() {
    InitOnceExecuteOnce(&g_realOnce, LoadReal, nullptr, nullptr);
}

volatile unsigned g_xinputCalls, g_gamepadHookCalls;

// The game polls pad 0 every frame on its main thread; all per-frame work hangs off that.

extern "C" DWORD WINAPI Proxy_XInputGetState(DWORD pad, void* state) {
    EnsureReal();
    if (pad == 0) {
        ++g_xinputCalls;
        OnFrame();
    }
    // The engine reads pad 0 only, for player 1: answer with the pad player 1 is assigned
    // (p1_pad), or with "not connected" when player 1 plays on keyboard and mouse.
    DWORD real = pad;
    if (pad == 0) {
        int p1 = P1EnginePad();
        real = p1 < 0 ? 0xffffffffu : (DWORD)p1;
    }
    DWORD rc = (pGetState && real != 0xffffffffu) ? pGetState(real, state) : ERROR_DEVICE_NOT_CONNECTED;
    if (rc != ERROR_SUCCESS && real != pad && state) memset(state, 0, 16);
    static bool loggedFirst;
    if (!loggedFirst && g_log) {
        loggedFirst = true;
        Log("XInputGetState export first called: pad %lu thread %lu, counter at %p", pad, GetCurrentThreadId(),
            (void*)&g_xinputCalls);
    }
    static DWORD nextCallLog;
    if (g_log && TraceOn() && (LONG)(GetTickCount() - nextCallLog) >= 0) {
        nextCallLog = GetTickCount() + 2000;
        Log("[xinput] GetState pad %lu rc %lu calls %u inject %d", pad, rc, g_xinputCalls, DevProbeStep());
    }
    if (pad == 0 && g_log && TraceOn()) {
        // Diagnostic: show that real pad-0 input passes through this proxy.
        struct S0 {
            DWORD packet;
            WORD buttons;
            BYTE lt, rt;
            SHORT lx, ly, rx, ry;
        }* s0 = (S0*)state;
        static WORD lastButtons;
        static DWORD lastLog;
        if (rc == ERROR_SUCCESS && s0->buttons != lastButtons && GetTickCount() - lastLog > 50) {
            lastLog = GetTickCount();
            lastButtons = s0->buttons;
            Log("[pad0] buttons=%04x lt=%u rt=%u lx=%d ly=%d thread %lu", s0->buttons, s0->lt, s0->rt, s0->lx, s0->ly,
                GetCurrentThreadId());
        }
    }
    DevXInputInject(pad, state, &rc);
    if (pad == 0 && state) {
        SHORT* sticks = (SHORT*)((uint8_t*)state + 8); // lx, ly, rx, ry
        if (DevRightStick(0, &sticks[2], &sticks[3], (BYTE*)state + 7)) rc = ERROR_SUCCESS;
    }
    return rc;
}

extern "C" DWORD WINAPI Proxy_XInputSetState(DWORD pad, void* vibration) {
    EnsureReal();
    {
        // p_rumble_log=N (diagnostic): N lines for the motor values the game itself sends.
        static int left = -1, armed = -1;
        int v = ReadIniInt(L"p_rumble_log", 0);
        if (v != armed) {
            armed = v;
            left = v;
        }
        const WORD* m = (const WORD*)vibration;
        static WORD last[4][2];
        if (left > 0 && pad < 4 && m && (m[0] != last[pad][0] || m[1] != last[pad][1] || pad != 0)) {
            --left;
            Log("[rumble] the game sends pad %lu: low %u high %u", pad, (unsigned)m[0], (unsigned)m[1]);
        }
        if (pad < 4 && m) last[pad][0] = m[0], last[pad][1] = m[1];
    }
    if (pad == 0) { // player 1's rumble goes to player 1's pad
        int p1 = P1EnginePad();
        if (p1 < 0) return ERROR_DEVICE_NOT_CONNECTED;
        pad = (DWORD)p1;
    }
    return pSetState ? pSetState(pad, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

// Vibration of a real pad (0 to 3), both motors 0 to 65535.
DWORD XInputSetVibration(DWORD pad, WORD low, WORD high) {
    EnsureReal();
    WORD v[2] = {low, high};
    return pSetState ? pSetState(pad, v) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI Proxy_XInputGetCapabilities(DWORD pad, DWORD flags, void* caps) {
    EnsureReal();
    return pGetCaps ? pGetCaps(pad, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" void WINAPI Proxy_XInputEnable(BOOL enable) {
    EnsureReal();
    if (pEnable) pEnable(enable);
}
