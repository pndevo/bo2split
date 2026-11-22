// Shared declarations for the XINPUT1_3.dll proxy: logging, ini access, memory and
// patch helpers, detours, and the globals and cross-module entry points.
#pragma once

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <share.h>

#include <tlhelp32.h>
#include <intrin.h>

#pragma comment(lib, "user32.lib")

// ---------------------------------------------------------------------------
// Logging (common.cpp)

extern CRITICAL_SECTION g_logLock;
extern FILE* g_log;
void Log(const char* fmt, ...);

// ---------------------------------------------------------------------------
// Real XInput and the per-frame driver (xinput_exports.cpp, main.cpp)

typedef DWORD(WINAPI* GetState_t)(DWORD, void*);
extern GetState_t pGetState;
DWORD XInputSetVibration(DWORD pad, WORD low,
                         WORD high); // a real pad 0 to 3, both motors 0 to 65535; the XInput result

void OnFrame();
extern volatile unsigned g_xinputCalls, g_gamepadHookCalls;
extern "C" DWORD WINAPI Proxy_XInputGetState(DWORD pad, void* state);

// This DLL's image range (main.cpp)
extern uintptr_t g_selfBase, g_selfEnd;

// ---------------------------------------------------------------------------
// Memory helpers (common.cpp)

bool IsReadable(uintptr_t addr, size_t len);
bool ReadSafe(uintptr_t addr, void* out, size_t len);
uint32_t Read32(uintptr_t addr);
bool BytesMatch(uintptr_t addr, const uint8_t* expect, size_t len);
bool WriteCode(uintptr_t addr, const uint8_t* bytes, size_t len);
uint32_t ImageTimestamp(uintptr_t base);

// ---------------------------------------------------------------------------
// Settings and patching (common.cpp)

extern wchar_t g_gameDir[MAX_PATH];
int ReadIniInt(const wchar_t* key, int def);
bool TraceOn(); // bo2ss.ini trace=1 (default 0): the repeating log lines and the console / command / Lua call log
extern volatile LONG g_iniCalls, g_iniFileReads; // counters for perf_log

struct BytePatch {
    const char* name;
    const wchar_t* iniKey;    // applied when this ini value is nonzero (default 1)
    const wchar_t* unlessKey; // skipped when this ini value is nonzero (optional)
    uintptr_t addr;
    uint8_t original[8];
    uint8_t replacement[8];
    size_t len;
};

void ApplyBytePatch(const BytePatch& p);

// ---------------------------------------------------------------------------
// Detours: 5-byte jmp at the function start, original prologue copied into a trampoline.
// Prologue lengths below cover only position-independent instructions.

struct Detour {
    const char* name;
    uintptr_t target;
    const uint8_t* prologue; // expected original bytes
    size_t prologueLen;      // >= 5
    void* hook;
    void** trampoline; // receives the callable original
};

void* MakeTrampoline(uintptr_t target, size_t len);
void InstallDetour(const Detour& d);

// ---------------------------------------------------------------------------
// Steam build: steam_offline.cpp

extern const BytePatch kSignInPatches[4];
extern const Detour kGamepadDriverHook;  // per-frame driver, always installed on the Steam build
extern const Detour kSteamTraceHooks[3]; // logging hooks, installed when trace=1
extern uint8_t* g_stubPage;              // Lua stub page, allocated by InitOnGameThread
extern unsigned g_luaRegistered, g_luaStatCount;
extern void* oGamepadUpdate;
extern bool g_lobbyOpened;
extern DWORD g_lobbyOpenedTick;
bool SteamIsLoggedOn(bool* known);
bool ScriptUpdateOnDisk(); // zone\all\ffotd_tu17_zm_147.ff exists
void ScriptUpdateWatch();  // Steam online: writes the script update to that file once the game has fetched it
void ExecCmd(const char* text);
void AutoStartMatch();
void __cdecl OfflineOnlineButton();
void InstallFakeLiveJump();
// LUI events to the menu scripts of a controller's root (see steam_offline.cpp).
bool LuiOpenMenu(int controller, const char* menuName);
bool LuiEvent(int controller, const char* eventName); // an event with the controller field only
bool LuiButton(int controller, const char* button, const char* qualifier, bool down, int rootController = -1);
