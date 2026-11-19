// Engine globals shared by every area (the image, the party and session state, dvar access and the dvar
// pointers, message buffers, the download buffer of the fix-of-the-day zone, the import table) and the
// typed calls of the engine functions of those areas.
// Addresses are those of the one supported game build (image base 0x00400000).
#pragma once

#include <cstdint>

namespace engine {
constexpr uintptr_t kImageBase = 0x00400000;     // image base of the game executable
constexpr uintptr_t kFlushGumpSlot = 0x005525c0; // void (int slot): flushes the gump held by a slot
constexpr uintptr_t kParty_CountMembers =
    0x005600c0; // int (party*): the members with a status above 2 and without flag bit 2
constexpr uintptr_t kParty_IsActive = 0x00562380; // char (party*)
constexpr uintptr_t kParty_IsHost = 0x005623a0;   // char (party*)
constexpr uintptr_t kExecCommandText =
    0x006bd5f0; // void (int lc, int controller, const char* text): runs one console command text
constexpr uintptr_t kGameModeSetMode = 0x006bed20;   // void (mode, value): 2 = local split-screen
constexpr uintptr_t kSetGametype = 0x006bfe90;       // void (const char* gametype, 1, 0)
constexpr uintptr_t kSessionModeSetBit = 0x006c05e0; // void (bit, value): bit 0 = offline, 1 = system link
constexpr uintptr_t kMSG_Init = 0x006cfbb0;     // void (msg*, void* buffer, int size): sets up a message over a buffer
constexpr uintptr_t kDvar_GetBool = 0x00755e50; // char (dvar*): type at dvar+0x10 == 1 -> byte at dvar+0x18
constexpr uintptr_t kDvarIntValue =
    0x00755e80; // int (dvar*): the int value of the dvar passed (read for gpad_debug, the dive minimum speed, the client slot count)
constexpr uintptr_t kDvarSetInt = 0x00758b20;    // void (dvar*, int value): sets the int value of a dvar
constexpr uintptr_t kDvarSetString = 0x00758c40; // void (dvar*, const char* value): sets the string value of a dvar
constexpr uintptr_t kScr_AddInt = 0x007ed870;    // void (int inst, int value): adds an int to the script call
constexpr uintptr_t kScriptArgInt =
    0x007f2ef0; // int (int inst, int index): the int argument of the running script builtin
constexpr uintptr_t kScriptArgString =
    0x007f3480; // const char* (int inst, int index): the string argument of the running script builtin
constexpr uintptr_t kFfotdLoadStep = 0x0086c240;      // void (): loads the fix-of-the-day zone, then the settings file
constexpr uintptr_t kIatXInputGetState = 0x00b07504;  // import table slot of XInputGetState (the next two slots follow)
constexpr uintptr_t kParty = 0x011c7550;              // party (members from +0x208, 0x148 bytes each)
constexpr uintptr_t kPartyHostStartFlag = 0x011d0aad; // byte: the host "start game" flag
constexpr uintptr_t kGumpNameDvars = 0x0123c598;      // dvar_t* gump_name0..3
constexpr uintptr_t kSessionMode = 0x02482944;        // session mode word (bit 0x10 = zombies)
constexpr uintptr_t kClPausedDvar =
    0x024829f0; // dvar* cl_paused, read with kDvarIntValue: nonzero while the match is paused (the pause menu sets 1)
constexpr uintptr_t kSvRunningDvar = 0x02482a48; // dvar*, tested with kDvar_GetBool: the listen server is running
constexpr uintptr_t kMaxClientsDvar =
    0x02504468;                              // dvar*, read with kDvarIntValue: the number of client slots (maxClients)
constexpr uintptr_t kQportBase = 0x0270cb38; // Netchan qport = base + lc
constexpr uintptr_t kFfotdBuffer = 0x02edd9c8;       // fix-of-the-day download buffer (0x20000 bytes)
constexpr uintptr_t kFfotdSize = 0x02f1da10;         // size of the downloaded fix-of-the-day zone
constexpr uintptr_t kFfotdFetched = 0x02f1da24;      // byte
constexpr uintptr_t kFfotdZoneLoaded = 0x02f1da33;   // byte
constexpr uintptr_t kFfotdSettingsDone = 0x02f1da38; // byte
constexpr uintptr_t kXuid0 = 0x02f51048;             // live table, controller 0 (stride 0x1070, one slot)
constexpr uintptr_t kDiveMinSpeedDvar =
    0x0314eba4; // int dvar: the dive needs the float at playerState +0x208 to be at least this

// Typed calls: one function per engine function above, with its signature and calling convention.
// Forced inline, so a call compiles to the same code as a cast to a function pointer.
__forceinline void FlushGumpSlot(int slot) {
    ((void(__cdecl*)(int))kFlushGumpSlot)(slot);
}
__forceinline int Party_CountMembers(uintptr_t party) {
    return ((int(__cdecl*)(uintptr_t))kParty_CountMembers)(party);
}
__forceinline bool Party_IsActive(uintptr_t party) {
    return ((char(__cdecl*)(uintptr_t))kParty_IsActive)(party) != 0;
}
__forceinline bool Party_IsHost(uintptr_t party) {
    return ((char(__cdecl*)(uintptr_t))kParty_IsHost)(party) != 0;
}
__forceinline void ExecCommandText(int lc, int controller, const char* text) {
    ((void(__cdecl*)(int, int, const char*))kExecCommandText)(lc, controller, text);
}
__forceinline void GameModeSetMode(int mode, int value) {
    ((void(__cdecl*)(int, int))kGameModeSetMode)(mode, value);
}
__forceinline void SetGametype(const char* gametype, int arg1, int arg2) {
    ((void(__cdecl*)(const char*, int, int))kSetGametype)(gametype, arg1, arg2);
}
__forceinline void SessionModeSetBit(int bit, int value) {
    ((void(__cdecl*)(int, int))kSessionModeSetBit)(bit, value);
}
__forceinline void MSG_Init(void* msg, void* buffer, int size) {
    ((void(__cdecl*)(void*, void*, int))kMSG_Init)(msg, buffer, size);
}
__forceinline char Dvar_GetBool(void* dvar) {
    return ((char(__cdecl*)(void*))kDvar_GetBool)(dvar);
}
__forceinline int DvarIntValue(void* dvar) {
    return ((int(__cdecl*)(void*))kDvarIntValue)(dvar);
}
__forceinline void DvarSetInt(void* dvar, int value) {
    ((void(__cdecl*)(void*, int))kDvarSetInt)(dvar, value);
}
__forceinline void DvarSetString(void* dvar, const char* value) {
    ((void(__cdecl*)(void*, const char*))kDvarSetString)(dvar, value);
}
__forceinline void Scr_AddInt(int inst, int value) {
    ((void(__cdecl*)(int, int))kScr_AddInt)(inst, value);
}
__forceinline int ScriptArgInt(int inst, int index) {
    return ((int(__cdecl*)(int, int))kScriptArgInt)(inst, index);
}
__forceinline const char* ScriptArgString(int inst, int index) {
    return ((const char*(__cdecl*)(int, int))kScriptArgString)(inst, index);
}
__forceinline void FfotdLoadStep() {
    ((void(__cdecl*)())kFfotdLoadStep)();
}
} // namespace engine
