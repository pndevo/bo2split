// Extension points that are empty in this build. The call sites in the other sources stay
// as they are; the compiler removes the calls.
#pragma once

#include "core/common.h"

inline const char* DevVariant() {
    return "release";
}

inline void DevFrame(uintptr_t) {}
inline void DevHeartbeat(unsigned) {}
inline void DevAfterClFrame() {}
inline bool DevApplyInput(int, float*, float*) {
    return false;
}
inline unsigned DevScriptedActions(int, DWORD) {
    return 0;
}
inline void DevLobbyPad(int, bool*, WORD*) {}
inline bool DevPadInject(int) {
    return false;
}
inline void DevXInputInject(DWORD, void*, DWORD*) {}
inline bool DevRightStick(int, SHORT*, SHORT*, BYTE*) {
    return false;
}
inline int DevProbeStep() {
    return -1;
}
inline bool DevWantsPadHook() {
    return false;
}
// Called once at start, after the other hooks are installed (steamBuild: the Steam game image).
inline void DevInstallHooks(bool) {}
// Called before the local party starts.
inline void DevBeforeParty() {}
