// The cgame's prediction and input hooks while a shadow pass runs: the replay of player 1's
// commands is kept off the shadow playerState, the pad facing is written back after the
// prediction, weapon cycling, player 1's input state is kept out of the shadow pass, and the
// viewmodel diagnostics.

#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "view/view_private.h"

// CG_DrawActiveFrame -> 0x004ee3c0 (prediction wrapper, calls CG_PredictPlayerState 0x004edb70
// and the viewmodel update) runs AFTER 0x00523080. The prediction replays player 1's usercmds
// on the predicted playerState (PM_UpdateViewAngles: cmd angles + delta_angles) or, without
// a replay, copies the cached viewangles cg+0x80b70 (player 1's) into +0x1f8. In the second
// pass the pad facing is written back after it, before the view calc (0x00522500).
typedef void(__cdecl* CgPredict_t)(int);
static CgPredict_t oCgPredict;
static void __cdecl HookCgPredict(int lc) {
    // The replay loop in CG_PredictPlayerState runs Pmove with every usercmd of player 1
    // whose serverTime is newer than the predicted playerState's commandTime (cg+0x480a8,
    // first dword). In the second pass that playerState is player 2's, so player 1's
    // buttons (attack, sprint, ads) would drive player 2's weapon state and animations. With
    // commandTime at INT_MAX no command qualifies and the playerState stays what the server
    // sent for player 2 (p2_no_replay=1); the original value is restored afterwards.
    uint8_t* cg = *(uint8_t**)engine::kCg;
    // p_weapon_adopt=1: a weapon in hand without a selection becomes the selection. With
    // three or four players a view can start without weapon model and crosshair until the
    // first weapon switch, player 1's too; shooting works. The block's weaponSelect (cg+0x69674)
    // stays 0 while the playerState holds a weapon (ps+0x1b8); why it is not set is not known.
    // The log line below records each adoption.
    if (cg && g_inRenderViews && (g_viewPass == 0 || (g_cgSwapped && g_psSwapped)) &&
        ReadIniInt(L"p_weapon_adopt", 1)) {
        int held = *(const int*)(cg + engine::kCgPredictedPs + engine::kPsWeapon);
        if (held && !*(const int*)(cg + engine::kCgWeaponSelect)) {
            *(int*)(cg + engine::kCgWeaponSelect) = held;
            Log("[view] pass %d: weapon %d in hand without a selection, selected it", (int)g_viewPass, held);
        }
    }
    bool noReplay = g_inRenderViews && g_viewPass >= 1 && g_psSwapped && cg && ReadIniInt(L"p2_no_replay", 1);
    int savedCommandTime = 0;
    if (noReplay) {
        savedCommandTime = *(int*)(cg + engine::kCgPredictedPs);
        *(int*)(cg + engine::kCgPredictedPs) = 0x7fffffff;
    }
    // Player 2's weapon cycle (pad Y or "p2 btn weapnext"): the engine's weapnext handler
    // CG_WeapNext_f 0x0053be10 runs here on player 2's block and playerState (CG_CycleWeapon
    // 0x00539d30 walks ps+0x248.. of the predicted playerState and CG_SelectWeapon 0x005369a0
    // sets cg+0x69674); CG_SubmitInputAndView then hands the selection to g_p2CmdWeapon.
    if (g_cgSwapped && g_psSwapped && cg && g_viewPass == 1 && g_p2WeapNextPending) {
        g_p2WeapNextPending = false;
        if (ReadIniInt(L"p2_weapnext", 1)) {
            int before = *(int*)(cg + engine::kCgWeaponSelect);
            engine::CG_WeapNext_f();
            Log("[p2] weapnext: weaponSelect %d -> %d (ps weapon %d, time %d select time %d, snap flags %02x)", before,
                *(int*)(cg + engine::kCgWeaponSelect), *(int*)(cg + engine::kCgPredictedPs + engine::kPsWeapon),
                *(int*)(cg + engine::kCgTime), *(int*)(cg + 0x69678),
                *(uint8_t*)(*(uint8_t**)(cg + engine::kCgSnap) + 0x28));
        }
    }
    // weapprev the same way: CG_WeapPrev_f 0x0053be50.
    if (g_cgSwapped && g_psSwapped && cg && g_viewPass == 1 && g_p2WeapPrevPending) {
        g_p2WeapPrevPending = false;
        int before = *(int*)(cg + engine::kCgWeaponSelect);
        engine::CG_WeapPrev_f();
        Log("[p2] weapprev: weaponSelect %d -> %d (ps weapon %d)", before, *(int*)(cg + engine::kCgWeaponSelect),
            *(int*)(cg + engine::kCgPredictedPs + engine::kPsWeapon));
    }
    if (g_cgSwapped && g_psSwapped && cg && g_viewPass >= 2 && g_viewPass <= 3 && ViewSlot() == g_viewPass - 1 &&
        g_viewWeapPrev[g_viewPass - 1]) {
        g_viewWeapPrev[g_viewPass - 1] = false;
        int before = *(int*)(cg + engine::kCgWeaponSelect);
        engine::CG_WeapPrev_f();
        Log("[p%d] weapprev: weaponSelect %d -> %d (ps weapon %d)", g_viewPass + 1, before,
            *(int*)(cg + engine::kCgWeaponSelect), *(int*)(cg + engine::kCgPredictedPs + engine::kPsWeapon));
    }
    // The same for the extra players (pad Y of players 3 / 4), on their own block.
    if (g_cgSwapped && g_psSwapped && cg && g_viewPass >= 2 && g_viewPass <= 3 && ViewSlot() == g_viewPass - 1 &&
        g_viewWeapNext[g_viewPass - 1]) {
        g_viewWeapNext[g_viewPass - 1] = false;
        int before = *(int*)(cg + engine::kCgWeaponSelect);
        engine::CG_WeapNext_f();
        Log("[p%d] weapnext: weaponSelect %d -> %d (ps weapon %d)", g_viewPass + 1, before,
            *(int*)(cg + engine::kCgWeaponSelect), *(int*)(cg + engine::kCgPredictedPs + engine::kPsWeapon));
    }
    if (g_cgSwapped && g_psSwapped && cg && g_viewPass >= 1 && g_viewPass <= 3 &&
        (g_viewPass == 1 || ViewSlot() == g_viewPass - 1))
        ShadowServerCommands(g_viewPass - 1, cg);
    // Player 2's action slots (d-pad) and scoreboard: the engine's handlers on player 2's block.
    if (g_cgSwapped && g_psSwapped && cg && g_viewPass >= 1 && g_viewPass <= 3) ShadowControlsSecondPass(cg);
    // p2_pred_ground=1: CG_PredictPlayerState (0x004edce4 ..) copies the snapshot's playerState
    // (0x2a08 bytes) over the predicted one when the snapshot is new OR the predicted
    // playerState stands on an entity (groundEntityNum ps+0xa4 neither 0x3ff none nor 0x3fe
    // world). The snapshot is player 1's in both passes: with player 2 standing on a mover
    // (the bus), the second pass would get player 1's playerState every frame and show player
    // 1's position with a broken viewmodel. The ground
    // entity reads "none" for the duration of the call; player 2's position comes from its own
    // snapshots anyway (ShadowFramePs), so the mover adjustment of the prediction is not needed.
    int* groundEnt = noReplay ? (int*)(cg + engine::kCgPredictedPs + 0xa4) : nullptr;
    int savedGround = 0x3ff;
    if (groundEnt && *groundEnt != 0x3ff && *groundEnt != 0x3fe && ReadIniInt(L"p2_pred_ground", 1)) {
        savedGround = *groundEnt;
        *groundEnt = 0x3ff;
    }
    // The other condition of that copy is a snapshot the block has not seen (snapshot server
    // time against cg+0x4aab0). After the copy the command time is the snapshot's again and
    // the replay runs player 1's commands on that playerState: while a dead player 1 watched
    // somebody, its "next player" button fired the weapons in the views of the shadow players.
    // The block reads as having seen the snapshot, so nothing is copied and nothing replayed.
    if (noReplay) {
        const uint8_t* snap = *(const uint8_t**)(cg + engine::kCgSnap);
        if (snap) *(int*)(cg + 0x4aab0) = *(const int*)(snap + 8);
    }
    if (noReplay) SnapFollowPrediction(true);
    oCgPredict(lc);
    if (noReplay) SnapFollowPrediction(false);
    if (savedGround != 0x3ff && *groundEnt == 0x3ff) *groundEnt = savedGround;
    if (noReplay) *(int*)(cg + engine::kCgPredictedPs) = savedCommandTime;
    // While a dead player 1 watches another player, the prediction copies the snapshot's
    // playerState (the watched player's) over the predicted one in every frame and replays
    // player 1's commands on it. A shadow player that is not the watched one then stood at the
    // watched player's place for the rest of the pass, and the difference to its own place was
    // added up as a prediction error (cg+0x4ae68, a vector the view calc adds to the camera
    // origin): its camera drifted hundreds of units away. The playerState is the shadow
    // player's again right after the call, and a shadow pass has no prediction error.
    if (g_inRenderViews && g_viewPass >= 1 && g_cgSwapped && g_psSwapped && cg) {
        if (memcmp(cg + engine::kCgPredictedPs + engine::kPsOrigin, ShadowFramePs(cg) + engine::kPsOrigin, 12) != 0) {
            static int count;
            static DWORD nextLog;
            ++count;
            if (TraceOn() && (LONG)(GetTickCount() - nextLog) >= 0) {
                nextLog = GetTickCount() + 1000;
                Log("[view] pass %d: the prediction replaced the playerState (%d so far)", (int)g_viewPass, count);
            }
            ShadowSwapPs(cg);
        }
        memset(cg + engine::kCgPredictionError, 0, 12);
    }
    // p_damage_feedback=1: the hit feedback of a shadow player (view kick, damage direction
    // indicator, red flash). Right after this call the cgame compares the damage counter of the
    // predicted playerState with the one it kept from before the prediction (0x004ea830: on a
    // change with an amount, CG_SetDamageViewKick 0x004ea0c0 fills one of the eight indicator
    // entries at cg+0x69710). In a shadow pass the playerState is swapped in before the
    // prediction, so the kept counter already is the new one and no hit is ever seen; in a frame
    // with a new snapshot the prediction has put player 1's playerState there, and the
    // comparison runs against player 1's counter. Here the four damage fields are the shadow
    // player's own snapshot's and the kept counter is the one its last pass saw.
    if (g_inRenderViews && g_viewPass >= 1 && g_cgSwapped && g_psSwapped && cg && ReadIniInt(L"p_damage_feedback", 1)) {
        static int seen[3];
        static bool known[3];
        int slot = ViewSlot();
        const uint8_t* own = ShadowViewCl() + engine::kClPlayerState + engine::kPsDamageEvent;
        memcpy(cg + engine::kCgPredictedPs + engine::kPsDamageEvent, own, engine::kPsDamageFieldsSize);
        int event = *(const int*)own;
        if (!known[slot]) {
            known[slot] = true;
            seen[slot] = event;
        }
        *(int*)(cg + engine::kCgOldPsDamageEvent) = seen[slot];
        if (event != seen[slot] && TraceOn())
            Log("[view] pass %d: hit %d, yaw %d pitch %d amount %d", (int)g_viewPass, event, ((const int*)own)[1],
                ((const int*)own)[2], ((const int*)own)[3]);
        seen[slot] = event;
    }
    // Player 1's body into the scene of the second view (the scene is open until R_RenderScene
    // at the end of the frame; the snapshot entities were added before the prediction).
    if (g_inRenderViews && g_viewPass >= 1 && g_cgSwapped && g_psSwapped && cg) AddPlayer1Body(cg);
    if (g_inRenderViews && g_viewPass >= 1 && g_psSwapped && ReadIniInt(L"p2_cam_native", 1)) {
        if (cg) {
            float* nps = (float*)(cg + engine::kCgPredictedPs);
            nps[0x1f8 / 4] = ViewPitch() + nps[0x98 / 4];
            nps[0x1fc / 4] = ViewYaw() + nps[0x9c / 4];
            nps[0x200 / 4] = 0.0f;
        }
    }
}
static const uint8_t kProCgPredict[] = {0x83, 0xec, 0x44, 0x53, 0x8b, 0x1d, 0x5c, 0xf7, 0x05, 0x01};
// The prediction wrapper 0x004ee3c0 ends with CL_CopyPlayerStateFieldsForPacket 0x00545570
// (lc, ps+0x104): it copies ps+0x104..+0x144 (floats) and ps+0x14c..+0x154 of the predicted
// playerState into cl+0x2c24..0x2c74, which CL_WritePacket (0x0054c2a0) sends with the next
// packet. In the second pass that playerState is player 2's, so the copy is skipped while the
// pass runs on player 2's block (p2_skip_cmd_weapons=1; the function copies playerState
// fields into the packet and does not set usercmd weapons, see below).
typedef void(__cdecl* CopyPsFieldsForPacket_t)(int, void*);
static CopyPsFieldsForPacket_t oCopyPsFieldsForPacket;
static void __cdecl HookCopyPsFieldsForPacket(int lc, void* ps) {
    if (g_cgSwapped && ReadIniInt(L"p2_skip_cmd_weapons", 1)) return;
    oCopyPsFieldsForPacket(lc, ps);
}
static const uint8_t kProCopyPsFieldsForPacket[] = {0x8b, 0x4c, 0x24, 0x04, 0x33, 0xc0};
// Every CG_DrawActiveFrame (so every pass) first runs CL_CreateNewCommands (0x00544680 ->
// 0x005445c0 -> CL_CreateCmd 0x005442e0, usercmd into cl+0x42ca8[cmdNumber & 0x7f]) and ends
// with CG_SubmitInputAndView 0x004b1ec0(lc, ..), which writes the cgame's input state for the
// next usercmd into cl: CL_SetUserCmdWeapons 0x00545470 (cg+0x69674 weaponSelect, cg+0x696cc
// offhand, the alt id of the slot -> cl+0x2be8..0x2bf0, CL_FinishMove 0x0054bf60 puts them
// into cmd+0x18..0x20), the sensitivity scale (0x00545640 -> cl+0x2bf4), the view angle
// offsets added to cl.viewangles (0x005454b0 -> cl+0x2c00..0x2c08) and the cgame button bits
// (0x00547580: cg+0x80b3c/0x80b40 OR-ed into cl+0x2c80/0x2c84). In the second pass that is
// player 2's block: its weaponSelect (the weapon player 1 held when the block was copied, or
// what the pass-2 validation 0x00524220 picked from player 2's list) would land in player 1's
// cl, the next frame's first usercmd would ask the server for it and the second usercmd of the frame
// (built in pass 2 from pass 1's values, same serverTime) is dropped by the server: player 1
// could never switch weapons. With p2_keep_cl_input=1 the range cl+0x2be8..0x2c8c is saved
// before the call and restored after it in the swapped pass; the weapon ids player 2's block
// selected are kept in g_p2CmdWeapon for player 2's own usercmd (p2_own_weapon=2).
typedef void(__cdecl* CgSubmitInputAndView_t)(int, int, int);
static CgSubmitInputAndView_t oCgSubmitInputAndView;
int g_p2CmdWeapon[3];
int g_viewCmdWeapon[3][3];
volatile bool g_viewWeapNext[3];
static void KeepPlayer1AimState();
static void __cdecl HookCgSubmitInputAndView(int lc, int a, int b) {
    uint8_t* cl = (uint8_t*)*(uint32_t*)engine::kClPtr;
    bool keep = g_cgSwapped && cl && ReadIniInt(L"p2_keep_cl_input", 1);
    uint8_t saved[0xa4];
    if (keep) memcpy(saved, cl + 0x2be8, sizeof(saved));
    oCgSubmitInputAndView(lc, a, b);
    KeepPlayer1AimState();
    // The turn rate of a shadow player's pad (look_native) needs what the game keeps for player
    // 1's: the field of view factor the pass just stored in the aim state of local client 0
    // (0x0048cad0: the view's tanHalfFovY against the one of a field of view of 65) and the
    // aim-down-sights fraction of the pass's predicted playerState.
    if (g_cgSwapped && g_viewPass >= 1 && g_viewPass <= 3) {
        const uint8_t* cg = *(const uint8_t* const*)engine::kCg;
        if (cg)
            ShadowSetViewLook(g_viewPass - 1, *(const float*)(engine::kAimState + engine::kAimStateFovTurnScale),
                              *(const float*)(cg + engine::kCgPredictedPs + engine::kPsWeaponPosFrac));
    }
    if (keep) {
        // p2_recoil=1: the call hands the kick angles of this frame (CG_KickAngles
        // 0x00521290, integrated from the kick velocity BG_WeaponFireRecoil set, at most 10
        // degrees) to CL_SetUserCmdAngleOffsets 0x005454b0, which stores them in the client
        // struct (cl+0x2c00 pitch, +0x2c04 yaw, +0x2c08 roll); the usercmd angles get them on
        // top of the facing, which is how recoil turns the view and the aim of player 1. In a
        // shadow pass the client struct is player 1's and its input is restored below, so the
        // offsets are kept for the shadow player: its usercmd and its camera add them.
        if (g_viewPass >= 1 && g_viewPass <= 3) {
            float kp = *(const float*)(cl + 0x2c00), ky = *(const float*)(cl + 0x2c04);
            if (!ReadIniInt(L"p2_recoil", 1) || fabsf(kp) > 10.5f || fabsf(ky) > 10.5f) kp = ky = 0.0f;
            ShadowSetViewKick(g_viewPass - 1, kp, ky);
            if (kp != 0.0f || ky != 0.0f) {
                static int left = -1, armed = -1;
                {
                    int v = ReadIniInt(L"p2_recoil_log", 0);
                    if (v != armed) {
                        armed = v;
                        left = v;
                    }
                }
                if (left > 0) {
                    --left;
                    Log("[p2] usercmd angle offsets from the cgame in pass %d: pitch %.3f yaw %.3f", g_viewPass, kp,
                        ky);
                }
            }
        }
        if (g_viewPass == 1)
            memcpy(g_p2CmdWeapon, cl + 0x2be8, sizeof(g_p2CmdWeapon)); // the extra views' selection is not player 2's
        if (g_viewPass >= 1 && g_viewPass <= 3)
            memcpy(g_viewCmdWeapon[g_viewPass - 1], cl + 0x2be8, sizeof(g_viewCmdWeapon[0]));
        memcpy(cl + 0x2be8, saved, sizeof(saved));
    }
}
static const uint8_t kProCgSubmitInputAndView[] = {0x83, 0xec, 0x0c, 0x56, 0x8b, 0x35, 0x5c, 0xf7, 0x05, 0x01};
// The pad's part of a usercmd, 0x0048eea0(input, output): it copies the view angles of the
// input (pitch at +4, yaw at +0x10) to the output (two floats) and then turns them by the
// right stick (0x0048d4e0) and the aim assist, each by the whole frame time. The game builds a
// usercmd at the start of every CG_DrawActiveFrame, so with N views player 1 would turn N times
// per frame: N times the turn rate of the game's dvars, and up to speed N * N times as fast.
// It also works on the aim state of local client 0 (engine::kAimState), which every pass fills
// from its own view (field of view factor, aim fraction, targets on the screen): player 1's
// turn at the start of a frame would use what the last shadow pass left there, and slow down
// while that player aims with a zoomed weapon.
// pad_turn_once=1: only the usercmd of pass 0 turns; in the other passes the angles pass
// through. The aim state is kept at the end of pass 0 and put back before player 1's turn of
// the next frame.
typedef void(__cdecl* AimPadInput_t)(const uint8_t*, float*);
static AimPadInput_t oAimPadInput;
static uint8_t g_p1AimState[engine::kAimStateSize];
static unsigned g_p1AimStateFrame; // g_renderFrames of the kept state, 0 = none
bool PadTurnAccel() {
    return ReadIniInt(L"look_accel", 1) != 0;
}
bool PadTurnOncePerFrame() {
    return ReadIniInt(L"pad_turn_once", 1) != 0;
}
static void KeepPlayer1AimState() {
    if (!g_inRenderViews || g_viewPass != 0 || g_viewCount < 2) return;
    memcpy(g_p1AimState, (const void*)engine::kAimState, sizeof(g_p1AimState));
    g_p1AimStateFrame = g_renderFrames;
}
static void __cdecl HookAimPadInput(const uint8_t* in, float* out) {
    if (g_inRenderViews && PadTurnOncePerFrame()) {
        if (g_viewPass >= 1) {
            out[0] = *(const float*)(in + 4);
            out[1] = *(const float*)(in + 0x10);
            return;
        }
        // the state of the frame before only: after a pause or a new match it is stale
        if (g_p1AimStateFrame && g_renderFrames - g_p1AimStateFrame <= 1 && *(const int*)(in + 0x2c) == 0)
            memcpy((void*)engine::kAimState, g_p1AimState, sizeof(g_p1AimState));
    }
    // look_accel=0: no run-up of the turn rate. The turn (0x0048d4e0) raises a growing rate by
    // aim_accel_turnrate_lerp per second while the dvar aim_accel_turnrate_enabled is set; the
    // dvar reads as off for the length of the call.
    uint8_t* accel = *(uint8_t**)engine::kAimAccelTurnrateEnabledDvar;
    if (!accel || !accel[0x18] || PadTurnAccel()) {
        oAimPadInput(in, out);
        return;
    }
    accel[0x18] = 0;
    oAimPadInput(in, out);
    accel[0x18] = 1;
}
static const uint8_t kProAimPadInput[] = {0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x56, 0x8b, 0x74, 0x24, 0x0c};
// Diagnostics (p2_vm_log=N): CG_SetupViewModel 0x005379c0 (cdecl, 9 stack args, the viewmodel
// xmodel handle arrives in EAX) rebuilds the viewmodel DObj; CG_UpdateViewModel 0x0053c600
// calls it when the DObj pointer is 0, the weapon differs from cg+0x696c4, 0x00532e30 asks
// for it (dirty byte vm+4, or vm+8 != weaponDef+0x39c) or the hand flag cg+0x696c8 differs
// from (cg+0x48288 >= dvar 0x00fc8404). Logs the inputs per call in the second pass.
static void* oCgSetupViewModel;
static int g_vmLogLeft = -1;
static void __cdecl LogSetupViewModel(uintptr_t ret) {
    if (!g_cgSwapped) return; // only the second pass on player 2's block
    {
        static int armed = -1;
        int v = ReadIniInt(L"p2_vm_log", 0);
        if (v != armed) {
            armed = v;
            g_vmLogLeft = v;
        }
    } // re-armed when the ini value changes
    if (g_vmLogLeft <= 0) return;
    --g_vmLogLeft;
    uint8_t* cg = *(uint8_t**)engine::kCg;
    const int* vm = *(const int**)engine::kViewModel;
    if (!cg || !vm) return;
    const uint8_t* dv = *(const uint8_t**)engine::kVmTraceDvar;
    float dvf = dv ? *(const float*)(dv + 0x18) : 0.0f;
    unsigned weapon = engine::BG_GetWeapon(cg + engine::kCgPredictedPs);
    Log("[vm] setup pass %d from %08x: dobj %08x dirty %d alt %08x vmWeapon %u psWeapon %u hand %d/%d (%.2f vs %.2f)",
        g_inRenderViews ? g_viewPass : -1, (unsigned)ret, (unsigned)vm[0], (int)*((const uint8_t*)vm + 4),
        (unsigned)vm[2], *(const unsigned*)(cg + 0x696c4), weapon, (int)*(const int8_t*)(cg + 0x696c8),
        *(const float*)(cg + 0x48288) < dvf ? 0 : 1, *(const float*)(cg + 0x48288), dvf);
}
static __declspec(naked) void HookCgSetupViewModel() {
    __asm {
        pushad
        mov eax, [esp + 32]
        push eax
        call LogSetupViewModel
        add esp, 4
        popad
        jmp dword ptr [oCgSetupViewModel]
    }
}
static const uint8_t kProCgSetupViewModel[] = {0x81, 0xec, 0x78, 0x01, 0x00, 0x00};

const Detour kViewPredictHooks[] = {
    {"CG prediction wrapper (pad facing after the prediction)", 0x004ee3c0, kProCgPredict, sizeof(kProCgPredict),
     (void*)HookCgPredict, (void**)&oCgPredict},
    {"CL_CopyPlayerStateFieldsForPacket (skipped in the second pass)", 0x00545570, kProCopyPsFieldsForPacket,
     sizeof(kProCopyPsFieldsForPacket), (void*)HookCopyPsFieldsForPacket, (void**)&oCopyPsFieldsForPacket},
    {"CG_SubmitInputAndView (player 1's cl input state kept in the second pass)", 0x004b1ec0, kProCgSubmitInputAndView,
     sizeof(kProCgSubmitInputAndView), (void*)HookCgSubmitInputAndView, (void**)&oCgSubmitInputAndView},
    {"pad part of a usercmd (player 1 turns once per frame)", 0x0048eea0, kProAimPadInput, sizeof(kProAimPadInput),
     (void*)HookAimPadInput, (void**)&oAimPadInput},
    {"CG setup viewmodel (diagnostics p2_vm_log)", 0x005379c0, kProCgSetupViewModel, sizeof(kProCgSetupViewModel),
     (void*)HookCgSetupViewModel, (void**)&oCgSetupViewModel},
};
