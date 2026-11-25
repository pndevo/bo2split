#pragma once

// Declarations shared by the files that make up the view passes (view.cpp, playerstate.cpp,
// predict.cpp, entities.cpp, scene.cpp, server_commands.cpp).

#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/offsets.h"

// playerstate.cpp: the facing of the player the pass's player watches, false while it plays itself.
bool ShadowFollowAngles(float* pitch, float* yaw);

#pragma warning(push)
// 4701 does not apply: p and y are read only when ShadowFollowAngles or ExtraAngles succeeded
// (each sets both before returning true); otherwise the values are not used.
#pragma warning(disable : 4701)
// The facing of the view pass's player (the pad angles of player 2 or of the extra client) plus
// its kick angles (recoil, HookCgSubmitInputAndView). While that player watches another one, the
// facing of the watched player.
inline float ViewPitch() {
    float p, y;
    if (ShadowFollowAngles(&p, &y)) return p;
    bool extra = ShadowViewCl() != g_p2.cl && ExtraAngles(g_viewPass - 2, &p, &y);
    return extra ? p + g_viewKick[g_viewPass == 3 ? 2 : 1][0] : g_p2Pitch + g_viewKick[0][0];
}
inline float ViewYaw() {
    float p, y;
    if (ShadowFollowAngles(&p, &y)) return y;
    bool extra = ShadowViewCl() != g_p2.cl && ExtraAngles(g_viewPass - 2, &p, &y);
    return extra ? y + g_viewKick[g_viewPass == 3 ? 2 : 1][1] : g_p2Yaw + g_viewKick[0][1];
}
#pragma warning(pop)
// Slot of the pass's player in the per-player state (0 = player 2, also for a pass without a client of its own).
inline int ViewSlot() {
    return (g_viewPass >= 2 && g_viewPass <= 3 && ShadowViewCl() != g_p2.cl) ? g_viewPass - 1 : 0;
}

// server_commands.cpp
void ShadowServerCommands(int slot, uint8_t* cg);

// playerstate.cpp
extern uint8_t g_savedPs[engine::kPlayerStateSize];
extern int g_savedCgClientNum;
const uint8_t* ShadowFramePs(const uint8_t* cg);
void ShadowSwapPs(uint8_t* cg);
void ShadowRestoreSnapPs();
void SnapFollowPrediction(bool during); // view.cpp: the snapshots of a watching shadow player around the prediction

// entities.cpp
void AddPlayer1Body(uint8_t* cg);
void WritePlayer1Pose();
extern __declspec(align(16)) uint8_t g_p1PredEnt[engine::kCentSize];
extern bool g_p1PredEntValid;
extern int g_vmAdds, g_vmTraceLeft, g_vmSceneIdx;

// view.cpp
extern int g_frameLogLeft;
