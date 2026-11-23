#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/offsets.h"

// p2_cg_swap=1: a cgame block of its own for player 2. The cgame keeps everything that is not
// playerState in cg (0x87700 bytes, pointer 0x0105f75c, see AllocateCgameArrays): viewmodel
// DObj (cg+0x7c998) and its animation state, viewbob, predicted player entity (cg+0x4aab4),
// event counters, HUD state. With one block all views would share the first-person arms (one
// player's shooting or reload would play on every view), so pass 1 runs on a copy: player 1's block is
// saved, player 2's block is copied in, and the snapshot bookkeeping that pass 0 just
// advanced (cg+0x1c latestSnapshotNum, cg+0x24 nextSnap, cg+0x28 snap, cg+0x4aab0 snapshot
// sequence of the prediction, cg+0x699d8) is taken from player 1's block so pass 1 never
// transitions a snapshot itself (entity events would run twice). cg_viewModelArray[0]
// (0xc0 bytes at *0x00fc83e8) is swapped along with it (p2_cg_swap_vm=1).
// One block, viewmodel and viewmodel DObj per shadow player (slot 0 = player 2, 1 and 2 = the
// extra views of players 3 and 4); g_curSlotCg / g_curSlotVm / g_curSlotDobj name the slot of the current swap.
constexpr int kCgSlots = 3;
static uint8_t* g_cgSlot[kCgSlots];
static uint8_t g_vmSlot[kCgSlots][0xc0];
static short g_dobjSlot[kCgSlots];
static bool g_cgSlotReset[kCgSlots] = {true, true, true};
static int g_cgCur;
#define g_curSlotCg g_cgSlot[g_cgCur]
#define g_curSlotVm g_vmSlot[g_cgCur]
#define g_curSlotDobj g_dobjSlot[g_cgCur]
static uint8_t* g_cgSave;
static uint8_t g_vmSave[0xc0];
bool g_cgSwapped;
static bool g_vmSwapped;
bool g_cgResetPending = true; // (re)build player 2's block from player 1's at the next swap (new match)
// The viewmodel DObj is one per local client: CG_SetupViewModel (0x005379c0) frees the old
// one through Com_ClientDObjSafeFree(0x700, lc) (0x006caa50) and creates the new one with
// Com_ClientDObjCreate (0x006ca920); both go through the handle map at 0x0254d0d0 (one short
// per handle, 0x701 per local client, handle 0x700 = viewmodel), which yields the DObj index
// (0x7c bytes each at 0x0250f0c8). cg_viewModelArray[0][0] holds the DObj pointer, [0x25] the
// XAnimTree, [0x26] the weapon's anim set, cg+0x696c4 the weapon the viewmodel was built for.
// With all passes on one DObj, a weapon switch in one pass would free the DObj the other pass
// still uses (crash in 0x0078c250 on the next anim call). Player 2's block therefore starts
// with no viewmodel (pointer, tree, anim set and weapon zeroed: CG_UpdateViewModel 0x0053c600
// then runs CG_SetupViewModel in pass 1 and creates a DObj and tree of its own), and the map
// entry of handle 0x700 is swapped with the block (p2_cg_swap_dobj=1).
static short g_dobjSave;
static bool g_dobjSwapped;
// The script blur (engine::kScriptBlur) is kept per local client, and every pass runs as local
// client 0: with one state the blur the server sets for player 1 (the lava) would lie on every
// view. Each shadow player has a state of its own, swapped with its block; the server commands
// that set it run on it in the player's pass (server_commands.cpp).
static uint8_t g_blurSlot[kCgSlots][engine::kScriptBlurSize];
static uint8_t g_blurSave[engine::kScriptBlurSize];
void ShadowCgSwapIn(uint8_t* cg, int slot) {
    if (!ReadIniInt(L"p2_cg_swap", 1)) return;
    if (slot < 0 || slot >= kCgSlots) return;
    g_cgCur = slot;
    if (!g_cgSave)
        g_cgSave = (uint8_t*)VirtualAlloc(nullptr, engine::kCgSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_curSlotCg)
        g_curSlotCg = (uint8_t*)VirtualAlloc(nullptr, engine::kCgSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_curSlotCg || !g_cgSave) return;
    if (g_cgResetPending) {
        g_cgResetPending = false;
        for (bool& r : g_cgSlotReset)
            r = true;
    }
    bool fresh = g_cgSlotReset[slot];
    if (g_cgSlotReset[slot]) {
        g_cgSlotReset[slot] = false;
        memcpy(g_curSlotCg, cg, engine::kCgSize);
        uint8_t* vm = *(uint8_t**)engine::kViewModel;
        if (vm) memcpy(g_curSlotVm, vm, sizeof(g_curSlotVm));
        g_curSlotDobj = 0;
        // The predicted player entity of the block (cg+0x4aab4) holds the handles of its two
        // physics bodies at +0x10 (standing) and +0x18 (down). The handles count their users
        // (0x007ba170 finds the body of an entity number and adds a user, 0x007baf00 drops
        // one). The copy would hold player 1's handles without being counted: when one of the
        // two players dies, its pass drops the body, and the other pass writes through the
        // destroyed handle (access violation at 0x009bc01c, write to 0x170). The block starts
        // without handles and gets counted ones of its own in its first pass.
        if (ReadIniInt(L"p_block_handles", 1)) {
            *(uint32_t*)(g_curSlotCg + engine::kCgPredictedEnt + 0x10) = 0;
            *(uint32_t*)(g_curSlotCg + engine::kCgPredictedEnt + 0x18) = 0;
        }
        if (ReadIniInt(L"p2_cg_swap_dobj", 1)) {
            *(uint32_t*)(g_curSlotCg + 0x696c4) = 0; // viewmodel weapon: none
            memset(g_curSlotVm, 0, 4);               // DObj pointer
            memset(g_curSlotVm + 0x25 * 4, 0, 8);    // XAnimTree, anim set
        }
        Log("[p2] cg swap: cgame block of player %d ready (%u bytes), p1 viewmodel dobj %d", slot + 2,
            (unsigned)engine::kCgSize, (int)((const short*)engine::kDObjHandleMap)[0x700]);
    }
    memcpy(g_cgSave, cg, engine::kCgSize);
    memcpy(cg, g_curSlotCg, engine::kCgSize);
    // Shared with player 1's block: the header and both snapshot buffers (cg+0x80 and
    // cg+0x24080, 0x24000 each; cg+0x24/0x28 point into them, so a stale copy would make pass 1
    // transition snapshots again and zero the clientinfo blocks, which respawns the player
    // tag entities every snapshot until "CG_Spawn: no free fake entities"), except cg+0
    // (clientNum, set per pass) and cg+0x48088..0x480a8 (frametime/time/oldTime of this
    // block), the prediction's snapshot sequence cg+0x4aab0, cg+0x699d8, and the clientinfo
    // blocks cg+0x69a60 (18 x 0x808: tag entities, player animation state).
    memcpy(cg + 4, g_cgSave + 4, engine::kCgFrametime - 4);
    memcpy(cg + 0x4aab0, g_cgSave + 0x4aab0, 4);
    memcpy(cg + 0x699d8, g_cgSave + 0x699d8, 8);
    memcpy(cg + engine::kCgClientInfo, g_cgSave + engine::kCgClientInfo, 18 * engine::kClientInfoSize);
    // p2_show_p1_ci=1: the clientinfo slot of player 1's own clientNum stays player 2's
    // block's (the player animation state of player 1's body, which only pass 2 draws and
    // advances; player 1's block never animates its own body, so a copy from it every pass
    // would restart that animation each frame).
    int p1 = *(const int*)g_cgSave;
    if (ReadIniInt(L"p2_show_p1", 1) && ReadIniInt(L"p2_show_p1_ci", 1) && p1 >= 0 && p1 < 18) {
        memcpy(cg + engine::kCgClientInfo + p1 * engine::kClientInfoSize,
               g_curSlotCg + engine::kCgClientInfo + p1 * engine::kClientInfoSize, engine::kClientInfoSize);
        // p2_hud_score=1: the kept slot still takes player 1's score (clientinfo +0x94, read by
        // the score lines of the HUD); otherwise it keeps the value of the match start in the
        // second view.
        if (ReadIniInt(L"p2_hud_score", 1))
            *(int*)(cg + engine::kCgClientInfo + p1 * engine::kClientInfoSize + 0x94) =
                *(const int*)(g_cgSave + engine::kCgClientInfo + p1 * engine::kClientInfoSize + 0x94);
    }
    // The vision sets (the colour grading of the view: the settings of the sets in use, their
    // names and blend weights) are player 1's in every pass. The game's client scripts switch
    // them, and those run for the one local client on player 1's block; a shadow player's
    // block would keep the sets of the moment it was copied (the set of the match start as a
    // layer that player 1 has long dropped: lifted blacks and a yellow cast in its view).
    // The last stand set is the exception: the server starts it for the one player that is
    // down (server_commands.cpp), so that slot stays the shadow player's own.
    static uint8_t ownSlot[2][engine::kCgVisionSetSize];
    const size_t lastStand = engine::kVisionSetSlotLastStand * engine::kCgVisionSetSize;
    memcpy(ownSlot[0], cg + engine::kCgVisionSetCurrent + lastStand, engine::kCgVisionSetSize);
    memcpy(ownSlot[1], cg + engine::kCgVisionSetFrom + lastStand, engine::kCgVisionSetSize);
    memcpy(cg + engine::kCgVisionSets, g_cgSave + engine::kCgVisionSets, engine::kCgVisionSetsSize);
    memcpy(cg + engine::kCgVisionSetCurrent + lastStand, ownSlot[0], engine::kCgVisionSetSize);
    memcpy(cg + engine::kCgVisionSetFrom + lastStand, ownSlot[1], engine::kCgVisionSetSize);
    g_cgSwapped = true;
    if (fresh) memset(g_blurSlot[slot], 0, sizeof(g_blurSlot[slot]));
    memcpy(g_blurSave, (const void*)engine::kScriptBlur, sizeof(g_blurSave));
    memcpy((void*)engine::kScriptBlur, g_blurSlot[slot], sizeof(g_blurSlot[slot]));
    ShadowRumbleSwapIn(slot, fresh);
    uint8_t* vm = *(uint8_t**)engine::kViewModel;
    if (vm && ReadIniInt(L"p2_cg_swap_vm", 1)) {
        memcpy(g_vmSave, vm, sizeof(g_vmSave));
        memcpy(vm, g_curSlotVm, sizeof(g_curSlotVm));
        g_vmSwapped = true;
    }
    if (ReadIniInt(L"p2_cg_swap_dobj", 1)) {
        short* map = (short*)engine::kDObjHandleMap;
        g_dobjSave = map[0x700];
        map[0x700] = g_curSlotDobj;
        g_dobjSwapped = true;
    }
}
const uint8_t* ShadowPlayer1Cg() {
    return g_cgSwapped ? g_cgSave : *(const uint8_t**)engine::kCg;
}
void P2CgSetWeaponSelect(int weapon) {
    uint8_t* cg = *(uint8_t**)engine::kCg;
    if (g_cgSwapped && g_cgCur == 0 && cg)
        *(int*)(cg + engine::kCgWeaponSelect) = weapon;
    else if (g_cgSlot[0])
        *(int*)(g_cgSlot[0] + engine::kCgWeaponSelect) = weapon;
}
void ShadowCgSwapOut(uint8_t* cg) {
    if (!g_cgSwapped) return;
    g_cgSwapped = false;
    memcpy(g_blurSlot[g_cgCur], (const void*)engine::kScriptBlur, sizeof(g_blurSlot[g_cgCur]));
    memcpy((void*)engine::kScriptBlur, g_blurSave, sizeof(g_blurSave));
    ShadowRumbleSwapOut();
    memcpy(g_curSlotCg, cg, engine::kCgSize);
    memcpy(cg, g_cgSave, engine::kCgSize);
    uint8_t* vm = *(uint8_t**)engine::kViewModel;
    if (vm && g_vmSwapped) {
        memcpy(g_curSlotVm, vm, sizeof(g_curSlotVm));
        memcpy(vm, g_vmSave, sizeof(g_vmSave));
    }
    g_vmSwapped = false;
    if (g_dobjSwapped) {
        short* map = (short*)engine::kDObjHandleMap;
        static short logged[kCgSlots] = {-1, -1, -1};
        if (map[0x700] != logged[g_cgCur]) {
            logged[g_cgCur] = map[0x700];
            Log("[p2] cg swap: player %d viewmodel dobj %d (player 1: %d)", g_cgCur + 2, (int)map[0x700],
                (int)g_dobjSave);
        }
        g_curSlotDobj = map[0x700];
        map[0x700] = g_dobjSave;
        g_dobjSwapped = false;
    }
}

// p2_vm_own_hands=1: the viewmodel hands of player 2. CG_UpdateViewModel (0x0053c600) takes the
// viewmodel xmodel from CG_GetViewModelXModel (0x005300d0, cdecl(cg)): snapshot ps viewmodel
// index *(cg+0x24)+0x204 (nextSnap, ps at snap+0x10, so ps+0x1f4), configstring idx+0x436
// (CL_GetConfigString 0x00545860), R_RegisterModel (0x0040ffe0). cg+0x24 is player 1's
// snapshot in both passes (copied with the header on swap-in), so player 2 would get player 1's
// character hands. In the swapped pass the index of player 2's snapshot ps (g_p2.cl+0x70)
// is written into that field around the call and restored after it (the field lives in the
// snapshot buffer of the swapped-in block, which the swap-out copies back anyway).
typedef int(__cdecl* CgGetViewModelXModel_t)(uint8_t*);
static CgGetViewModelXModel_t oCgGetViewModelXModel;
static int __cdecl HookCgGetViewModelXModel(uint8_t* cg) {
    const uint8_t* vcl = ShadowViewCl();
    if (!(g_cgSwapped && vcl && *(const uint32_t*)(vcl + engine::kClSnapshot) && ReadIniInt(L"p2_vm_own_hands", 1)))
        return oCgGetViewModelXModel(cg);
    uint8_t* snap = *(uint8_t**)(cg + engine::kCgNextSnap);
    if (!snap) return oCgGetViewModelXModel(cg);
    int* field = (int*)(snap + 0x204);
    int p1 = *field, p2 = *(const int*)(vcl + engine::kClPlayerState + 0x1f4);
    *field = p2;
    int model = oCgGetViewModelXModel(cg);
    *field = p1;
    static int logged = -1;
    if (p2 != logged) {
        logged = p2;
        Log("[vm] hands: player 2 viewmodel index %d (player 1: %d), xmodel %d", p2, p1, model);
    }
    return model;
}
static const uint8_t kProCgGetViewModelXModel[] = {0x8b, 0x4c, 0x24, 0x04, 0x8b, 0x51, 0x24};
const Detour kCgSwapHooks[] = {
    {"CG_GetViewModelXModel (player 2's hands in the second pass)", 0x005300d0, kProCgGetViewModelXModel,
     sizeof(kProCgGetViewModelXModel), (void*)HookCgGetViewModelXModel, (void**)&oCgGetViewModelXModel},
};
