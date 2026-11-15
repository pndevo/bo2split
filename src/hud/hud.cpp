#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/ui.h"
#include "engine/render.h"
#include "engine/offsets.h"

// p2_hud_own=1: a HUD of its own for player 2.
//
// The Zombies HUD is Lua (LUI). Every value reaches it as an event: LUI_BeginEvent
// (0x00930360(rootName, eventName, L)) pushes LUI.roots[rootName].processEvent, the sender
// adds fields (0x0092ce40 "controller", "ammoInClip", ...) and LUI_EndEvent 0x0092a600 runs
// it. The root name comes from 0x009317a0(controller): "UIRoot0" (0x0332588c) or
// "UIRootFull" (0x033257dc). The PC build creates exactly one controller root in LUI_CoD_Init
// 0x00935060: the struct at 0x03325800 (0xb0 bytes: +0 projection, +0x40 inverse, +0x80 view
// parms slot, +0x84/+0x88 last tick times, +0x8c name, +0xac active, +0xad opened) is set up
// by 0x00931090 (ESI = struct; x, y, w, h: matrices, then LUI.CoDRoot.new(name) and the
// userdata, "Error creating root"), and 0x00930c60(name, "HUD", controller, L) adds the HUD
// menu with an "addmenu" event. Per view pass DrawActiveFrameLocalClient0 ticks the root
// (0x009345b0 -> 0x00931240 with ESI = struct: "process_events", timing) and draws it
// (0x009315c0(lc) -> 0x00931380(lc, struct) into the current 2D viewport). The HUD values
// come from CG_SetNextSnap -> 0x0094ded0(lc, flag): it compares the snapshot playerState
// (cg->snap + 0x10) with the copy at cg+0x4ae78 and sends hud_update_weapon / _ammo /
// _scores / ... for what changed (or everything when cg->nextSnap == cg->snap).
//
// Here: a second struct "UIRoot1" is created the same way (p2_hud_slot = its view parms
// slot; UIRoot0 uses 7, UIRootFull 8) with the HUD menu as controller 1, once per match. In
// the second pass (player 2's cgame block) the tick runs on it instead of UIRoot0, every
// event that starts for UIRoot0 goes to UIRoot1 (the hook on LUI_BeginEvent swaps the name,
// events for the full root are dropped), and 0x0094ded0 runs once per frame on a copy of
// player 1's snapshot that carries player 2's playerState at +0x10 (cg->snap / cg->nextSnap
// point at the copies for the call, the first time at one copy for a full refresh). The
// draw: the first pass's LUI draw never reaches the screen and the second pass's draw shows
// up in both halves, so the second pass draws both roots with one viewport per half
// (p2_hud_draw=3, see HookRootDrawAll). With p2_hud_own=0 the second pass sends no events.
namespace {
constexpr uintptr_t kRootInit = 0x00931090;
typedef void(__cdecl* LuiSetNumber_t)(const char*, int, void*);
constexpr size_t kRootSize = 0xb0, kSnapSize = 0x24000;
constexpr size_t kSnapRoundsPlayed = 0x23b10; // int in a snapshot: the rounds played
// One HUD root per shadow player: slot 0 = player 2 (UIRoot1, controller 1), slots 1 / 2 =
// players 3 / 4 (UIRoot2 / UIRoot3, controllers 2 / 3; p_hud_extra=1). Each has its root
// struct, its two snapshot copies for the hud_update_* compare, its score cache and its
// playerState copy for the client fields.
constexpr size_t kScoreCacheSize = 0x64;
struct HudSlot {
    __declspec(align(16)) uint8_t root[kRootSize];
    __declspec(align(16)) uint8_t snapA[kSnapSize];
    __declspec(align(16)) uint8_t snapB[kSnapSize];
    bool ready, fullRefreshDone, cfPsValid;
    int w, h; // the size the root is laid out for
    DWORD match;
    uint8_t scoreCache[kScoreCacheSize];
    uint8_t cfPsOld[engine::kPlayerStateSize];
    uint32_t visOld[2]; // the HUD visibility bits this player's pass computed last (HookVisBits)
    bool dpadShow;      // the pass sent an ammo or weapon update: fade the ammo / d-pad block in again
};
HudSlot g_hud[3];
bool g_hudBusy;
int g_hudTraceLeft;
// Slot of the pass that runs right now (0 outside the shadow passes).
int HudCur() {
    return (g_inRenderViews && g_viewPass >= 1 && g_viewPass <= 3) ? g_viewPass - 1 : 0;
}
#define g_root1 (g_hud[0].root)
typedef void(__cdecl* RootDraw_t)(int, void*);

bool OwnHudActive() {
    return g_hud[g_cgSwapped ? HudCur() : 0].ready && ReadIniInt(L"p2_hud_own", 1) != 0;
}
// The pass of player 3 / 4 while that player has no root (yet): no events, no draw.
bool ExtraWithoutRoot() {
    return g_cgSwapped && ExtraPass() && !g_hud[HudCur()].ready;
}

// 0x00931090: root struct in ESI, four floats on the stack, caller cleans up.
void RootInitCall(uint8_t* root, float x, float y, float w, float h) {
    __asm {
        push h
        push w
        push y
        push x
        mov esi, root
        mov eax, kRootInit
        call eax
        add esp, 16
    }
}
} // namespace

// LUI_BeginEvent 0x00930360(rootName, eventName, L).
typedef char(__cdecl* LuiBeginEvent_t)(const char*, const char*, void*);
static LuiBeginEvent_t oLuiBeginEvent;
static char __cdecl HookLuiBeginEvent(const char* root, const char* name, void* L) {
    if (!g_hudBusy && ExtraWithoutRoot()) return 0;
    // p2_hud_trace_p1=N (diagnostic): N lines for the events of player 1's pass and of the time outside the passes.
    if (!g_cgSwapped && name && strcmp(name, "process_events") != 0) {
        static int left = -1, armed = -1;
        int v = ReadIniInt(L"p2_hud_trace_p1", 0);
        if (v != armed) {
            armed = v;
            left = v;
        }
        if (left > 0) {
            --left;
            Log("[hud] pass %d event %s -> %s%s", g_inRenderViews ? g_viewPass : -1, name, root ? root : "?",
                g_hudBusy ? " (root setup)" : "");
        }
    }
    if (g_cgSwapped && !g_hudBusy) {
        {
            static int armed = -1;
            int v = ReadIniInt(L"p2_hud_trace", 0);
            if (v != armed) {
                armed = v;
                g_hudTraceLeft = v;
            }
        }
        bool own = OwnHudActive();
        const char* to = root;
        bool drop = false;
        if (own) {
            const char* mine = (const char*)(g_hud[HudCur()].root + 0x8c);
            if (root == (const char*)engine::kLuiRoot0Name) {
                if (ReadIniInt(L"p2_hud_route", 1))
                    to = mine;
                else
                    drop = true;
            } else if (root != mine)
                drop = true; // full root (menus): first pass only
        } else
            drop = true;
        if (g_hudTraceLeft > 0 && !(name && strcmp(name, "process_events") == 0)) {
            --g_hudTraceLeft;
            Log("[hud] pass %d event %s -> %s%s", (int)g_viewPass, name ? name : "?", root ? root : "?",
                drop ? " (dropped)" : (to != root ? " (to the pass's root)" : ""));
        }
        if (drop) return 0;
        if (own && name &&
            (strcmp(name, "hud_update_ammo") == 0 || strcmp(name, "hud_update_weapon") == 0 ||
             strcmp(name, "hud_update_offhand") == 0))
            g_hud[HudCur()].dpadShow = true;
        root = to;
    }
    return oLuiBeginEvent(root, name, L);
}
static const uint8_t kProLuiBeginEvent[] = {0x56, 0x8b, 0x74, 0x24,
                                            0x10}; // 5 bytes only: the jnz that follows is relative

// Root tick 0x00931240 (ESI = root struct, lc on the stack): UIRoot1 instead of UIRoot0 in
// the second pass.
// While the player's own menu is open (player_menu.cpp) the tick runs with that player's
// profile: the menu is built in the tick after its open event and reads its values there.
static void* oRootTick;
static bool g_tickProfile;
static uint8_t* __cdecl PickTickRoot(uint8_t* root) {
    if (g_cgSwapped && root == (uint8_t*)engine::kLuiRoot0 && OwnHudActive() && ReadIniInt(L"p2_hud_tick", 1)) {
        g_tickProfile = ShadowMenuTickBegin(HudCur());
        return g_hud[HudCur()].root;
    }
    return root;
}
static void __cdecl AfterRootTick() {
    if (g_tickProfile) ShadowMenuTickEnd();
    g_tickProfile = false;
}
static __declspec(naked) void HookRootTick() {
    __asm {
        push esi
        push ecx
        push edx
        push esi
        call PickTickRoot
        add esp, 4
        mov esi, eax
        pop edx
        pop ecx
        push dword ptr [esp + 8] // the local client
        call dword ptr [oRootTick]
        add esp, 4
        call AfterRootTick
        pop esi
        ret
    }
}
static const uint8_t kProRootTick[] = {0x83, 0xec, 0x08, 0x8b, 0x0d, 0xa0, 0x08, 0x17, 0x03};

// Root draw 0x009315c0(lc): UIRoot1 alone in the second pass.
typedef void(__cdecl* RootDrawAll_t)(int);
static RootDrawAll_t oRootDrawAll;
// p2_hud_draw: 0 = nothing drawn in the second pass, 1 = UIRoot1 through 0x00931380 directly
// (drawn over both halves: the engine's per-root viewport setup in 0x009315c0 is skipped),
// 2 = the engine's own draw with UIRoot1's struct copied over UIRoot0's for the call (the
// full root is not drawn again: the primary-controller index 0x033256e4 is parked).
// p2_hud_draw_p0 (diagnostic): the first pass's draw with the own HUD active: 1 = engine
// default (UIRoot0), 0 = nothing, 2 = UIRoot1 as in the second pass.
static void DrawRoot1ViaEngine(int lc, uint8_t* root1) {
    uint8_t save[kRootSize];
    memcpy(save, (const void*)engine::kLuiRoot0, kRootSize);
    memcpy((void*)engine::kLuiRoot0, root1, kRootSize);
    int* primary = (int*)engine::kLuiDrawPrimary;
    int savedPrimary = *primary;
    *primary = -2;
    oRootDrawAll(lc);
    *primary = savedPrimary;
    memcpy(root1, (const void*)engine::kLuiRoot0, kRootSize);
    memcpy((void*)engine::kLuiRoot0, save, kRootSize);
}
// p2_menu_center=1: while a UI menu is up (active menu id of local client 0, 0x02878080: 3 =
// pause menu, 8 = scoreboard) UIRoot0 is not drawn inside the first half. The Lua side opens
// the menu as a popup of the HUD in UIRoot0, which is laid out for one half; the menu's own
// layout is centred and wider than that, so its buttons lie outside the half (left half
// dark, no menu items). ShadowHudAfterFrame draws the root once after both views instead.
static bool g_root1Drawn; // UIRoot1 was drawn in this frame's second pass
// p_scoreboard_per_view=1: the scoreboard (menu 8) is not centred. It fits into one view, and at
// the end of a match every player's root shows one: a centred copy of player 1's would lie
// across the other players' own.
static bool MenuUp() {
    int menu = *(const int*)engine::kActiveMenu;
    if (menu == 8 && ReadIniInt(L"p_scoreboard_per_view", 1)) return false;
    return g_p2.started && menu != 0 && ReadIniInt(L"p2_menu_center", 1) != 0;
}
static bool MenuCentered() {
    return g_inRenderViews && MenuUp();
}

// LUI mouse move 0x00931da0(lc, x, y): sends "mousemove" with the cursor as a fraction of the
// screen times the root's rect (0x0092bf70), and keeps the fractions (0x03325734 + 0x24 *
// controller) for "mousedown" / "mouseup" (0x00931f50). The root is laid out for one half, so
// a cursor at screen x arrives at root x / 2. While the root is drawn centred (above), the
// pixel under the cursor is root x - W / 4: the coordinates are converted so that the engine's
// formula gives that.
typedef void(__cdecl* LuiMouseMove_t)(int, int, int);
static LuiMouseMove_t oLuiMouseMove;
static void __cdecl HookLuiMouseMove(int lc, int x, int y) {
    if (MenuUp() && OwnHudActive() && g_root1[0xac]) {
        float fr[4];
        ShadowViewFractions(0, fr); // player 1's root is as large as its view
        if (fr[2] < 1.0f) x = 2 * x - *(const int*)engine::kVidWidth / 2;
        if (fr[3] < 1.0f) y = 2 * y - *(const int*)engine::kVidHeight / 2;
    }
    oLuiMouseMove(lc, x, y);
}
static const uint8_t kProLuiMouseMove[] = {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x10};
extern const Detour kHudMouseHook = {"LUI mouse move (cursor for the centred menu root)",
                                     0x00931da0,
                                     kProLuiMouseMove,
                                     sizeof(kProLuiMouseMove),
                                     (void*)HookLuiMouseMove,
                                     (void**)&oLuiMouseMove};
// A root is laid out for its view's height: a menu takes the width of a 16:9 screen of that
// height, the scoreboard a little more than the height. In a view narrower than that (two views
// side by side) they would reach beyond its edges, so the root is drawn scaled around the middle
// of the view (matrix of column vectors: x is row 0 to 3, y row 4 to 7), with its HUD shrunk the
// same way. RootScale: the factor for the root of a pass, 1 = as it is. The menu is a shadow
// player's own (player_menu.cpp); the scoreboard is up for a shadow player that holds its
// button, and for a player whose cgame block has the scoreboard flag set (cg+0x69568: player 1's
// own button, everyone at the end of a match).
static float RootScale(int pass) {
    int slot = pass - 1;
    bool menu = slot >= 0 && ShadowMenuOpen(slot);
    const uint8_t* cg = *(const uint8_t* const*)engine::kCg;
    bool scores = (cg && *(const int*)(cg + 0x69568) != 0) || (slot >= 0 && ShadowScoresOpen(slot));
    if (!menu && !scores) return 1.0f;
    float fr[4];
    ShadowViewFractions(pass, fr);
    if (fr[2] <= 0.0f || fr[3] <= 0.0f) return 1.0f;
    float aspect = (fr[2] * (float)*(const int*)engine::kVidWidth) / (fr[3] * (float)*(const int*)engine::kVidHeight);
    int need = menu ? ReadIniInt(L"player_menu_aspect", 178) : ReadIniInt(L"player_scores_aspect", 97);
    if (need <= 0) return 1.0f;
    float scale = aspect / (need * 0.01f);
    return scale < 1.0f ? scale : 1.0f;
}
// Scales the matrix of a root struct and rebuilds its inverse (0x00962ad0(in, out)).
static bool ScaleRoot(uint8_t* root, float scale) {
    float* m = (float*)root;
    for (int j = 0; j < 8; ++j)
        m[j] *= scale;
    return ((char(__cdecl*)(float*, float*))0x00962ad0)(m, m + 16) != 0;
}
static void __cdecl HookRootDrawAll(int lc) {
    if (ExtraWithoutRoot()) return;
    uint8_t* mine = g_hud[g_cgSwapped ? HudCur() : 0].root;
    if (OwnHudActive() && mine[0xac] && *(void**)engine::kLuiState) {
        *(int*)(mine + 0x80) = ReadIniInt(L"p2_hud_slot", 6); // view parms slot, adjustable at runtime
        if (g_cgSwapped) {
            if (HudCur() == 0) g_root1Drawn = true;
            float scale = RootScale(g_viewPass);
            if (scale < 1.0f) {
                __declspec(align(16)) uint8_t tmp[kRootSize];
                memcpy(tmp, mine, kRootSize);
                if (ScaleRoot(tmp, scale)) {
                    DrawRoot1ViaEngine(lc, tmp);
                    return;
                }
            }
        } else if (MenuCentered())
            return;
        if (g_cgSwapped) {
            int mode = ReadIniInt(L"p2_hud_draw", 2);
            if (mode == 0) return;
            if (mode == 1) {
                engine::LUI_DrawRoot(lc, mine);
                return;
            }
            if (mode == 3) {
                // Both roots from this pass, each with an explicit 2D viewport (R_SetViewport
                // command 0x0041e680(x, y, w, h), as the engine does for the full root), since
                // the first pass's LUI draw never reaches the screen and this pass's draw
                // shows up in both halves.
                uint8_t* rb = *(uint8_t**)engine::kCmdBuffer;
                int* cnt = (int*)(rb + 0x18);
                int W = *(const int*)engine::kVidWidth, H = *(const int*)engine::kVidHeight;
                bool vert = ReadIniInt(L"split_vertical", 1) != 0;
                int w = vert ? W / 2 : W, h = vert ? H : H / 2;
                *cnt += 0x400;
                engine::R_AddCmdSetViewport(0, 0, w, h);
                *cnt -= 0x400;
                engine::LUI_DrawRoot(lc, (void*)engine::kLuiRoot0);
                *cnt += 0x400;
                engine::R_AddCmdSetViewport(vert ? W / 2 : 0, vert ? 0 : H / 2, w, h);
                *cnt -= 0x400;
                engine::LUI_DrawRoot(lc, mine);
                *cnt += 0x400;
                engine::R_AddCmdSetViewport(0, 0, W, H);
                *cnt -= 0x400;
                return;
            }
            DrawRoot1ViaEngine(lc, mine);
            return;
        }
        int p0 = ReadIniInt(L"p2_hud_draw_p0", 1);
        if (p0 == 0) return;
        if (p0 == 2) {
            DrawRoot1ViaEngine(lc, mine);
            return;
        }
    }
    // Player 1's root with its scoreboard up, in a view too narrow for it.
    float scale = (g_inRenderViews && g_viewPass == 0 && g_p2.started) ? RootScale(0) : 1.0f;
    if (scale < 1.0f) {
        uint8_t save[0x80];
        memcpy(save, (const void*)engine::kLuiRoot0, sizeof(save));
        if (ScaleRoot((uint8_t*)engine::kLuiRoot0, scale)) oRootDrawAll(lc);
        memcpy((void*)engine::kLuiRoot0, save, sizeof(save));
        return;
    }
    oRootDrawAll(lc);
}
// Root draw 0x00931380(lc, root struct): trace p2_hud_draw_trace=N (pass, root, slot).
static RootDraw_t oRootDraw;
static void __cdecl HookRootDraw(int lc, void* root) {
    static int left = -1;
    {
        static int armed = -1;
        int v = ReadIniInt(L"p2_hud_draw_trace", 0);
        if (v != armed) {
            armed = v;
            left = v;
        }
    }
    if (left > 0) {
        --left;
        const uint8_t* r = (const uint8_t*)root;
        Log("[hud] root draw lc %d pass %d swapped %d root %s slot %d active %d", lc, g_inRenderViews ? g_viewPass : -1,
            (int)g_cgSwapped, (const char*)(r + 0x8c), *(const int*)(r + 0x80), (int)r[0xac]);
    }
    // p2_hint_log=N: cursor hint state per pass (CG_UpdateCursorHint 0x004dce90 takes it from
    // the predicted playerState ps+0x494/+0x498 into cg+0x695f8/+0x69604; CG_DrawCursorHint
    // 0x004dd1b0 is the menu ownerdraw 0x48).
    static int hintLeft = -1;
    {
        static int armed = -1;
        int v = ReadIniInt(L"p2_hint_log", 0);
        if (v != armed) {
            armed = v;
            hintLeft = v;
        }
    }
    const uint8_t* cg = *(const uint8_t**)engine::kCg;
    if (hintLeft > 0 && cg && (*(const int*)(cg + 0x4853c) || *(const int*)(cg + 0x695f8))) {
        --hintLeft;
        Log("[hud] hint pass %d swapped %d: ps hint %d str %d ent %d | cg hint %d str %d time %d/%d | p2 snapshot ps "
            "hint %d str %d",
            g_inRenderViews ? g_viewPass : -1, (int)g_cgSwapped, *(const int*)(cg + 0x4853c),
            *(const int*)(cg + 0x48540), *(const int*)(cg + 0x48544), *(const int*)(cg + 0x695f8),
            *(const int*)(cg + 0x69604), *(const int*)(cg + 0x695fc), *(const int*)(cg + 0x69600),
            g_p2.cl ? *(const int*)(g_p2.cl + engine::kClPlayerState + 0x494) : -1,
            g_p2.cl ? *(const int*)(g_p2.cl + engine::kClPlayerState + 0x498) : -1);
    }
    oRootDraw(lc, root);
}
static const uint8_t kProRootDraw[] = {0x8b, 0x0d, 0xd8, 0x87, 0x34, 0x03};
static const uint8_t kProRootDrawAll[] = {0x83, 0xec, 0x08, 0x83, 0x3d, 0xa0, 0x08, 0x17, 0x03, 0x00};

// The Lua HUD keeps its state per controller, not per root: the handlers address
// LUI.roots["UIRoot" .. controller] ("roots | UIRoot | controller" in the Lua tables), and the engine
// names the roots the same way (UIRoot%d, 0x00935060). UIRoot1 therefore has to be the HUD of
// controller 1 (p2_hud_controller=1): addmenu and first_snapshot carry that controller, and
// every "controller" field the engine writes into an event of the second pass (0x0092ce40,
// LUI_SetTableNumber(name, value, L)) is rewritten from 0 to it. The Lua side then asks the
// engine about controller 1, which the PC build does not know: GetLocalClientNumForController
// (0x006be5f0, loops over the one slot of the table 0x024806f0) answers -1. In the second pass
// (and while the root is being created) that answer is 0: the local client whose cgame
// block is player 2's at that moment.
static LuiSetNumber_t oLuiSetNumber;
static void __cdecl HookLuiSetNumber(const char* name, int value, void* L) {
    if (g_cgSwapped && !g_hudBusy && value == 0 && OwnHudActive() && name && strcmp(name, "controller") == 0) {
        int c = HudCur() == 0 ? ReadIniInt(L"p2_hud_controller", 1) : HudCur() + 1;
        if (c > 0) value = c;
    }
    oLuiSetNumber(name, value, L);
}
static const uint8_t kProLuiSetNumber[] = {0x8b, 0x4c, 0x24, 0x0c, 0x8b, 0x41, 0x24};
typedef int(__cdecl* GetLcForController_t)(int);
static GetLcForController_t oGetLcForController;
static int __cdecl HookGetLcForController(int controller) {
    if (controller > 0 && (g_cgSwapped || g_hudBusy) && ReadIniInt(L"p2_hud_own", 1) &&
        (controller == ReadIniInt(L"p2_hud_controller", 1) || controller == 2 || controller == 3))
        return 0;
    return oGetLcForController(controller);
}
static const uint8_t kProGetLcForController[] = {0x8b, 0x54, 0x24, 0x04, 0x33, 0xc9};

const Detour kHudHooks[] = {
    {"LUI_BeginEvent (second-pass events to UIRoot1 or dropped)", 0x00930360, kProLuiBeginEvent,
     sizeof(kProLuiBeginEvent), (void*)HookLuiBeginEvent, (void**)&oLuiBeginEvent},
    {"LUI root tick (UIRoot1 in the second pass)", 0x00931240, kProRootTick, sizeof(kProRootTick), (void*)HookRootTick,
     &oRootTick},
    {"LUI root draw (UIRoot1 in the second pass)", 0x009315c0, kProRootDrawAll, sizeof(kProRootDrawAll),
     (void*)HookRootDrawAll, (void**)&oRootDrawAll},
    {"LUI_SetTableNumber (event field controller = player 2's HUD controller)", 0x0092ce40, kProLuiSetNumber,
     sizeof(kProLuiSetNumber), (void*)HookLuiSetNumber, (void**)&oLuiSetNumber},
    {"GetLocalClientNumForController (player 2's HUD controller -> 0 in the second pass)", 0x006be5f0,
     kProGetLcForController, sizeof(kProGetLcForController), (void*)HookGetLcForController,
     (void**)&oGetLcForController},
    {"LUI root draw by struct (diagnostics p2_hud_draw_trace)", 0x00931380, kProRootDraw, sizeof(kProRootDraw),
     (void*)HookRootDraw, (void**)&oRootDraw},
};

// A field of the Lua table `global.table` (CoD.OptionsControls.CurrentTabIndex), read and
// written as the raw 8 byte value (type in the low half: 0 nil, 3 number with a float in the
// high half, 5 table). As LUI_BeginEvent walks LUI.roots: the globals table is the value at
// L+0x38, a field is read by pushing the key (0x004f1820(L, text, length)) and
// 0x008fc8d0(L, table, key), which returns the value; the stack top is L+0x24. A field is
// written with the table and the value on the stack: 0x0059e2d0(L, -2, key) pops the value.
static bool LuiTable(const char* global, const char* table, unsigned long long* out) {
    uint8_t* L = *(uint8_t**)engine::kLuiState;
    if (!L) return false;
    typedef unsigned long long Value;
    auto field = [L](Value from, const char* name) -> Value {
        ((void(__cdecl*)(void*, const char*, size_t))0x004f1820)(L, name, strlen(name));
        uint32_t* top = *(uint32_t**)(L + 0x24);
        Value v = ((Value(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, uint32_t))0x008fc8d0)(
            L, (uint32_t)from, (uint32_t)(from >> 32), top[-2], top[-1]);
        *(uint32_t**)(L + 0x24) = top - 2;
        return v;
    };
    Value g = field(*(const Value*)(L + 0x38), global);
    if ((uint32_t)g != 5) return false;
    Value t = field(g, table);
    if ((uint32_t)t != 5) return false;
    *out = t;
    return true;
}
bool LuiTableGet(const char* global, const char* table, const char* key, unsigned long long* value) {
    uint8_t* L = *(uint8_t**)engine::kLuiState;
    unsigned long long t;
    if (!LuiTable(global, table, &t)) return false;
    ((void(__cdecl*)(void*, const char*, size_t))0x004f1820)(L, key, strlen(key));
    uint32_t* top = *(uint32_t**)(L + 0x24);
    *value = ((unsigned long long(__cdecl*)(void*, uint32_t, uint32_t, uint32_t, uint32_t))0x008fc8d0)(
        L, (uint32_t)t, (uint32_t)(t >> 32), top[-2], top[-1]);
    *(uint32_t**)(L + 0x24) = top - 2;
    return true;
}
bool LuiTableSet(const char* global, const char* table, const char* key, unsigned long long value) {
    uint8_t* L = *(uint8_t**)engine::kLuiState;
    unsigned long long t;
    if (!LuiTable(global, table, &t)) return false;
    unsigned long long* top = *(unsigned long long**)(L + 0x24);
    top[0] = t;
    top[1] = value;
    *(unsigned long long**)(L + 0x24) = top + 2;
    ((void(__cdecl*)(void*, int, const char*))0x0059e2d0)(L, -2, key);
    *(unsigned long long**)(L + 0x24) = top;
    return true;
}

// An event for the LUI root of a player (0 = player 1's UIRoot0, 1 to 3 = the roots of players
// 2 to 4), sent outside the view passes. Fields: pairs of key and value; a value that is a
// whole number goes in as a number, "true" / "false" as a bool, anything else as a string. The
// Lua side may ask the engine about the controller while it handles the event, so the
// controller of a shadow player answers as local client 0 for the call (g_hudBusy).
bool ShadowHudEvent(int player, const char* name, int controller, const char* const* fields, int count) {
    void* L = *(void**)engine::kLuiState;
    if (!L || player < 0 || player > 3) return false;
    if (player > 0 && !g_hud[player - 1].ready) return false;
    const char* root = player == 0 ? (const char*)engine::kLuiRoot0Name : (const char*)(g_hud[player - 1].root + 0x8c);
    bool busy = g_hudBusy;
    g_hudBusy = true;
    bool sent = false;
    if (oLuiBeginEvent(root, name, L)) {
        oLuiSetNumber("controller", controller, L);
        for (int i = 0; i + 1 < count * 2; i += 2) {
            const char* key = fields[i];
            const char* value = fields[i + 1];
            char* end = nullptr;
            long number = strtol(value, &end, 10);
            if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0)
                engine::LUI_SetTableBool(key, value[0] == 't', L);
            else if (*value && end && !*end)
                oLuiSetNumber(key, (int)number, L);
            else
                engine::LUI_SetTableString(key, value, L);
        }
        engine::LUI_EndEvent(L);
        sent = true;
    }
    g_hudBusy = busy;
    return sent;
}

// p2_hud_score=1: the score lines of the HUD ("hud_update_competitive_scoreboard", sender
// 0x0094c650) are sent when a client's score in the clientinfo (cg+0x69a60 + n*0x808, valid
// +0, clientNum +8, team +0x2c, score +0x94) differs from a cache per local client: 0x03329d58
// client numbers, 0x03329d78 valid, 0x03329d98 scores (0x20 bytes each), 0x03329db8 the own
// client number. 0x0094d910(controller, demoJump, force) compares and fills it, called once
// per pass from CG_DrawActiveFrame through 0x0094da10(lc, demoJump) and by the full refresh
// in CG_SendHudUpdateEvents. Both passes are local client 0 and would share the cache, so
// whatever differs between the two blocks would send the event in both passes every frame.
// Each shadow pass therefore gets a cache of its own.
static uint8_t g_scoreCacheSave[kScoreCacheSize];
static bool g_scoreCacheIn;
static int g_scoreCacheSlot;

static void ScoreCacheIn() {
    if (g_scoreCacheIn || !ReadIniInt(L"p2_hud_score", 1)) return;
    memcpy(g_scoreCacheSave, (const void*)engine::kScoreCache, kScoreCacheSize);
    g_scoreCacheSlot = HudCur();
    memcpy((void*)engine::kScoreCache, g_hud[g_scoreCacheSlot].scoreCache, kScoreCacheSize);
    g_scoreCacheIn = true;
}
static void ScoreCacheOut() {
    if (!g_scoreCacheIn) return;
    memcpy(g_hud[g_scoreCacheSlot].scoreCache, (const void*)engine::kScoreCache, kScoreCacheSize);
    memcpy((void*)engine::kScoreCache, g_scoreCacheSave, kScoreCacheSize);
    g_scoreCacheIn = false;
}

typedef void(__cdecl* ScoreFrame_t)(int, int);
static ScoreFrame_t oScoreFrame;
static void __cdecl HookScoreFrame(int lc, int demoJump) {
    if (ExtraWithoutRoot()) return; // its events would be dropped and the cache would swallow the change
    bool own = g_cgSwapped && OwnHudActive();
    if (own) ScoreCacheIn();
    oScoreFrame(lc, demoJump);
    if (own) ScoreCacheOut();
}
static const uint8_t kProScoreFrame[] = {0x8b, 0x44, 0x24, 0x04, 0x50}; // 5 bytes only: a relative call follows
extern const Detour kHudScoreHook = {"HUD score lines per frame (a score cache of its own for the second pass)",
                                     0x0094da10,
                                     kProScoreFrame,
                                     sizeof(kProScoreFrame),
                                     (void*)HookScoreFrame,
                                     (void**)&oScoreFrame};

// p_health_log=N (diagnostic): the health fraction the low-health overlay works with. The
// overlay draw (0x004dbf20) and the check that opens and closes its menu (0x004afdd0) take
// the fraction from 0x004dbb40(playerState) (health +0x238 over max health +0x240, 0 when
// dead) and pass the playerState of the cgame's snapshot (cg+0x28, +0x10); in a shadow pass
// that is the pass's own player's (p_snap_ps, playerstate.cpp). The hook changes nothing; it
// logs the result per pass and caller, twice per second.
typedef float(__cdecl* HealthFraction_t)(const uint8_t* ps);
static HealthFraction_t oHealthFraction;
static float __cdecl HookHealthFraction(const uint8_t* ps) {
    const uint8_t* given = ps;
    float fraction = oHealthFraction(ps);
    if (ReadIniInt(L"p_health_log", 0) > 0) {
        // One line per pass and caller (0 = the draw of the overlay, 1 = the check that opens
        // and closes the overlay's menu), twice per second.
        static DWORD next[4][2];
        int pass = g_inRenderViews ? (g_viewPass & 3) : 0;
        bool fromDraw = (uintptr_t)_ReturnAddress() == 0x004dc05e;
        if ((LONG)(GetTickCount() - next[pass][fromDraw ? 0 : 1]) >= 0) {
            next[pass][fromDraw ? 0 : 1] = GetTickCount() + 500;
            const uint8_t* cg = *(const uint8_t**)engine::kCg;
            Log("[health] fraction from %s (%p): in views %d pass %d swapped %d -> %.2f | given ps health %d, used ps "
                "health %d | cg time %d lastDamage %d",
                fromDraw ? "draw" : "menu check", _ReturnAddress(), (int)g_inRenderViews, g_viewPass, (int)g_psSwapped,
                fraction, *(const int*)(given + 0x238), *(const int*)(ps + 0x238),
                cg ? *(const int*)(cg + engine::kCgTime) : -1, cg ? *(const int*)(cg + 0x6986c) : -1);
        }
    }
    return fraction;
}
static const uint8_t kProHealthFraction[] = {0x8b, 0x4c, 0x24, 0x04, 0x8b, 0x91, 0x38, 0x02, 0x00, 0x00};
extern const Detour kHudHealthHook = {"health fraction of the low-health overlay (diagnostic p_health_log)",
                                      0x004dbb40,
                                      kProHealthFraction,
                                      sizeof(kProHealthFraction),
                                      (void*)HookHealthFraction,
                                      (void**)&oHealthFraction};

// p_hud_bits=1: the HUD visibility bits reach every player's root. CG_UpdateVisibilityBits
// 0x0055b750(lc), called once per pass from CG_DrawActiveFrame, builds a 64-bit mask from the
// cgame block (HUD visible, scoreboard up, in a vehicle, ...) in 0x028a5f28 + lc * 8 and hands
// the mask of its previous call and the new one to 0x0094b890, which sends "hud_update_bit_%d"
// for each bit that differs; the Lua HUD shows and hides its parts on those. Every pass is
// local client 0, so the engine's previous mask is the one of the pass before: a bit that
// changes for all players at once (bit 0 about 15 s after the spawn) would reach player 1's
// root only, and a root created later would never get the bits that were set already. The HUD
// of players 2 to 4 would show nothing until a full refresh made it ask again (with three or
// four players their roots are created after that change). Here each pass gets the mask of its
// own last call as the previous one; a new root starts from 0 and gets every set bit.
static uint32_t g_visOldP1[2];
typedef void(__cdecl* VisBits_t)(int);
static VisBits_t oVisBits;
static void __cdecl HookVisBits(int lc) {
    if (lc != 0 || !g_p2.started || !ReadIniInt(L"p_hud_bits", 1) || !ReadIniInt(L"p2_hud_own", 1)) {
        oVisBits(lc);
        return;
    }
    uint32_t* cur = (uint32_t*)engine::kVisBits;
    uint32_t* mine = g_cgSwapped ? g_hud[HudCur()].visOld : g_visOldP1;
    cur[0] = mine[0];
    cur[1] = mine[1];
    uint32_t old0 = cur[0], old1 = cur[1];
    oVisBits(lc);
    mine[0] = cur[0];
    mine[1] = cur[1];
    // p_hud_bits_log=N (diagnostic): N lines, one per change of a pass's mask.
    if (old0 != cur[0] || old1 != cur[1]) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p_hud_bits_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0) {
            --left;
            Log("[hud] vis bits pass %d (swapped %d): %08x %08x -> %08x %08x", g_inRenderViews ? (int)g_viewPass : -1,
                (int)g_cgSwapped, old1, old0, cur[1], cur[0]);
        }
    }
}
static const uint8_t kProVisBits[] = {0x83, 0xec, 0x10, 0x56, 0x8b, 0x74, 0x24, 0x18};
extern const Detour kHudVisBitsHook = {"CG_UpdateVisibilityBits (previous HUD visibility bits per pass)",
                                       0x0055b750,
                                       kProVisBits,
                                       sizeof(kProVisBits),
                                       (void*)HookVisBits,
                                       (void**)&oVisBits};

// p2_cf_log=N (diagnostic): N lines for the client field events the HUD gets. The client
// script names fields with setupclientfieldcodecallbacks (0x0050ca90), which registers one of
// two code callbacks per field: 0x004d7af0 (integer) and 0x004d7bd0 (float), both
// cdecl(lc, entNum, oldValue, newValue, newEnt, initialSnap, name, wasDemoJump). Each sends an
// LUI event called like the field (controller, wasDemoJump, initialSnap, oldValue, newValue)
// to the root of the controller: power-up icons, collected parts.
typedef void(__cdecl* CfCallback_t)(int, int, int, int, int, int, const char*, int);
static CfCallback_t oCfInt, oCfFloat;
static void CfLog(const char* kind, void* from, int ent, int oldValue, int newValue, int newEnt, int initialSnap,
                  const char* name) {
    static int left = -1, armed = -1;
    {
        int v = ReadIniInt(L"p2_cf_log", 0);
        if (v != armed) {
            armed = v;
            left = v;
        }
    }
    if (left <= 0) return;
    --left;
    Log("[cf] %s %s pass %d swapped %d ent %d old %08x new %08x newEnt %d initial %d from %p", kind,
        name && IsReadable((uintptr_t)name, 4) ? name : "?", g_viewPass, (int)g_cgSwapped, ent, oldValue, newValue,
        newEnt, initialSnap, from);
}
static void __cdecl HookCfInt(int lc, int ent, int oldValue, int newValue, int newEnt, int initialSnap,
                              const char* name, int demoJump) {
    CfLog("int", _ReturnAddress(), ent, oldValue, newValue, newEnt, initialSnap, name);
    oCfInt(lc, ent, oldValue, newValue, newEnt, initialSnap, name, demoJump);
}
static void __cdecl HookCfFloat(int lc, int ent, int oldValue, int newValue, int newEnt, int initialSnap,
                                const char* name, int demoJump) {
    CfLog("float", _ReturnAddress(), ent, oldValue, newValue, newEnt, initialSnap, name);
    oCfFloat(lc, ent, oldValue, newValue, newEnt, initialSnap, name, demoJump);
}
static const uint8_t kProCfCallback[] = {0x8b, 0x44, 0x24, 0x04, 0x56, 0x50}; // a relative call follows
extern const Detour kHudCfHooks[2] = {
    {"client field to LUI, integer (diagnostic p2_cf_log)", 0x004d7af0, kProCfCallback, sizeof(kProCfCallback),
     (void*)HookCfInt, (void**)&oCfInt},
    {"client field to LUI, float (diagnostic p2_cf_log)", 0x004d7bd0, kProCfCallback, sizeof(kProCfCallback),
     (void*)HookCfFloat, (void**)&oCfFloat},
};

// LUI error report 0x009291a0(what, L): the engine's handler for a failed Lua call ("Error
// processing event", "Error creating root", ...); the message is the string on top of the Lua
// stack (0x009015b0(L, top - 8, 0)). The engine prints it to a debug channel only; here it
// goes to the log (at most 200 lines).
typedef void(__cdecl* LuiError_t)(const char*, uint8_t*);
static LuiError_t oLuiError;
static void __cdecl HookLuiError(const char* what, uint8_t* L) {
    static int left = 200;
    if (left > 0 && L) {
        --left;
        uintptr_t top = *(const uintptr_t*)(L + 0x24) - 8;
        const char* msg = top >= *(const uintptr_t*)(L + 0x28)
                              ? ((const char*(__cdecl*)(uint8_t*, uintptr_t, int))0x009015b0)(L, top, 0)
                              : nullptr;
        Log("[lui] %s: %s (pass %d swapped %d)", what && IsReadable((uintptr_t)what, 4) ? what : "?",
            msg && IsReadable((uintptr_t)msg, 4) ? msg : "?", g_inRenderViews ? g_viewPass : -1, (int)g_cgSwapped);
    }
    oLuiError(what, L);
}
static const uint8_t kProLuiError[] = {0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10};
extern const Detour kLuiErrorHook = {"LUI error report (Lua error messages to the log)",
                                     0x009291a0,
                                     kProLuiError,
                                     sizeof(kProLuiError),
                                     (void*)HookLuiError,
                                     (void**)&oLuiError};

// p2_hud_cf=1: player 2's own playerState client fields ("toplayer" set, number 5: collected
// parts "buildable", power-up icons) reach player 2's HUD. The engine compares them only for
// the local client: CG_SetNextSnap -> 0x004d8440(cent, lc, force, demoJump, oldPs, newPs) ->
// 0x00493a30(set, lc, entNum, oldBase, newBase, force, demoJump), which queues one entry per
// changed field (0x18 bytes: field, old value, new value, lc, entNum, three flag bytes) in the
// pending array of the cgame's script context (thread block +0x1a50 -> +4 -> +0x1e9c: 0x100
// entries per local client, counts at +0x1800); 0x00493770(lc) runs the queue from
// CG_AddPacketEntities: the client script's callback (field +0) and the code callback (field
// +8 when field +4 bit 0 is set; name id at +0xc, type at +0x14). Here the same compare runs
// on player 2's playerState in the second pass, and only the code callbacks of the new
// entries are called (they send the LUI event, which the second pass routes to UIRoot1); the
// entries are then dropped, so the client script never sees player 2's values as local
// client 0's.
static void ShadowHudClientFields(HudSlot& hs, const uint8_t* cl) {
    if (!ReadIniInt(L"p2_hud_cf", 1)) return;
    const uint8_t* ps = cl + engine::kClPlayerState;
    uint8_t* g_cfPsOld = hs.cfPsOld;
    bool& g_cfPsValid = hs.cfPsValid;
    uint8_t* tls = *(uint8_t**)(__readfsdword(0x2c) + *(const int*)engine::kTlsIndex * 4);
    if (!tls) return;
    // The engine points the thread block at the cgame's script context (cg+0x699c8) only
    // inside its own frame functions (0x00524b60 sets and clears it); this runs before.
    uint8_t* savedCtx = *(uint8_t**)(tls + 0x1a50);
    uint8_t* cgNow = *(uint8_t**)engine::kCg;
    if (!savedCtx && cgNow) *(uint8_t**)(tls + 0x1a50) = cgNow + 0x699c8;
    struct Restore {
        uint8_t* tls;
        uint8_t* saved;
        ~Restore() { *(uint8_t**)(tls + 0x1a50) = saved; }
    } restore{tls, savedCtx};
    uint8_t* ctx = *(uint8_t**)(tls + 0x1a50);
    uint8_t* inner = ctx ? *(uint8_t**)(ctx + 4) : nullptr;
    uint8_t* pending = inner ? *(uint8_t**)(inner + 0x1e9c) : nullptr;
    {
        static DWORD next;
        if (ReadIniInt(L"p2_cf_log", 0) && (LONG)(GetTickCount() - next) >= 0) {
            next = GetTickCount() + 5000;
            Log("[cf] player 2 compare: context %p (engine's %p) inner %p pending %p count %d valid %d", ctx, savedCtx,
                inner, pending, pending ? *(const int*)(pending + 0x1800) : -1, (int)g_cfPsValid);
        }
    }
    if (!pending) return;
    int* count = (int*)(pending + 0x1800);
    int before = *count;
    bool force = !g_cfPsValid;
    engine::CompareClientFields(5, 0, *(const int*)(ps + engine::kPsClientNum), force ? ps : g_cfPsOld, ps,
                                force ? 1 : 0, 0);
    for (int i = before; i < *count && i < 0x100; ++i) {
        const uint32_t* e = (const uint32_t*)(pending + i * 0x18);
        const uint32_t* field = (const uint32_t*)e[0];
        if (!field || !(field[1] & 1) || !field[2]) continue;
        const uint8_t* b = (const uint8_t*)e;
        unsigned id = *(const uint16_t*)((const uint8_t*)field + 0xc);
        const char* name = id ? (const char*)(*(const uintptr_t*)engine::kStringTable + id * 0x18 + 4) : nullptr;
        ((CfCallback_t)field[2])((int)e[3], (int)e[4], (int)e[1], (int)e[2], b[0x15], b[0x14], name, 0);
    }
    *count = before;
    memcpy(g_cfPsOld, ps, engine::kPlayerStateSize);
    g_cfPsValid = true;
}

// p_hud_dump=N (diagnostic): every change of the value logs the element trees of UIRoot0 to
// UIRoot3. LUI elements are 0x148 byte structs in the array 0x031708d0 (0x0092d2b0 turns a Lua
// userdata into one): flags +0xc, rect +0x14..+0x20 (set by LUI_SetRootRectAndMatrix for a
// root), next sibling +0xd0, first child +0xd4 (the Lua methods getNextSibling 0x0092d880 and
// getFirstChild 0x0092d820). 0x00930550 (L in ESI, the root's name on the stack) returns the
// root element of LUI.roots[name].
static const uint8_t* LuiRootElement(const char* name, void* L) {
    const uint8_t* e;
    __asm {
        push esi
        mov esi, L
        push name
        mov eax, 0x00930550
        call eax
        add esp, 4
        pop esi
        mov e, eax
    }
    return e;
}
static int LuiDumpElement(const uint8_t* e, int depth, int& lines) {
    int total = 0;
    for (; e && IsReadable((uintptr_t)e, 0x148); e = *(const uint8_t* const*)(e + 0xd0)) {
        ++total;
        int kids = 0;
        for (const uint8_t* c = *(const uint8_t* const*)(e + 0xd4); c && IsReadable((uintptr_t)c, 0x148) && kids < 1000;
             c = *(const uint8_t* const*)(c + 0xd0))
            ++kids;
        const float* f = (const float*)e;
        if (lines > 0 && depth <= 9) {
            --lines;
            Log("[luitree] %*s#%d flags %08x rect %.1f %.1f %.1f %.1f | %.2f %.2f %.2f %.2f | %.1f %.1f %.1f %.1f kids "
                "%d",
                depth * 2, "", (int)(((uintptr_t)e - engine::kLuiElements) / 0x148), *(const uint32_t*)(e + 0xc), f[5],
                f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15], f[16], kids);
        }
        if (depth < 40) total += LuiDumpElement(*(const uint8_t* const*)(e + 0xd4), depth + 1, lines);
    }
    return total;
}
static void LuiDumpRoots() {
    static int seen;
    int v = ReadIniInt(L"p_hud_dump", 0);
    if (v == seen) return;
    seen = v;
    void* L = *(void**)engine::kLuiState;
    if (!L) return;
    for (int i = 0; i < 4; ++i) {
        char name[16];
        sprintf_s(name, "UIRoot%d", i);
        if (i > 0 && !g_hud[i - 1].ready) continue;
        const uint8_t* e = LuiRootElement(name, L);
        if (!e) {
            Log("[luitree] %s: no element", name);
            continue;
        }
        int lines = v > 1 ? v : 400;
        Log("[luitree] %s:", name);
        int total = LuiDumpElement(*(const uint8_t* const*)(e + 0xd4), 1, lines);
        Log("[luitree] %s: %d elements below the root (root #%d flags %08x rect %.1f %.1f %.1f %.1f)", name, total,
            (int)(((uintptr_t)e - engine::kLuiElements) / 0x148), *(const uint32_t*)(e + 0xc), ((const float*)e)[5],
            ((const float*)e)[6], ((const float*)e)[7], ((const float*)e)[8]);
    }
}

// p2_recoil=1: weapon recoil for the shadow players. CG_FireWeapon (0x0053c0c0) calls
// BG_WeaponFireRecoil 0x008e0a90(ps, cg+0x64a68, cg+0x697bc, cg+0x64a80, cg+0x695ec > 0,
// cg+0x64a84, 0x00ca1588) only for the entity of the snapshot's own player: it adds the gun
// kick (+0x64a68, two floats) and sets the view kick velocity (+0x697bc, three floats) from
// the weapon's hip or ads view kick values; the view calc of the following frames turns the
// camera by it. The fire events run in player 1's cgame, where players 2 to 4 are other
// entities, so their cameras would not kick. Here the
// call runs in the shadow player's own pass, on its block and playerState, once per shot the
// fire sound hook counted (sound.cpp). The function reads the script time through the
// thread block's context, which is set as in ShadowHudClientFields.
static void ShadowRecoil(int slot, uint8_t* cg) {
    LONG shots = InterlockedExchange(&g_shadowShots[slot], 0);
    if (shots > 0) ShadowRumbleShot(cg);
    if (shots <= 0 || !ReadIniInt(L"p2_recoil", 1)) return;
    uint8_t* tls = *(uint8_t**)(__readfsdword(0x2c) + *(const int*)engine::kTlsIndex * 4);
    if (!tls) return;
    uint8_t* savedCtx = *(uint8_t**)(tls + 0x1a50);
    if (!savedCtx) *(uint8_t**)(tls + 0x1a50) = cg + 0x699c8;
    if (shots > 3) shots = 3;
    // p2_recoil_fpu=1: the call runs with an empty x87 stack. The function builds the kick
    // values on the x87 stack (random lerp between the weapon's min and max, three deep). In
    // some frames the stack is not empty when this hook runs (the engine keeps values on it
    // across the hooked call), the loads overflow and the kick velocity comes out as -nan(ind):
    // the camera rises, cuts off and resets (dual wield pistols fired fast, either hand). The
    // state is saved (which empties the stack), the control word put back for the call, and
    // everything restored afterwards.
    // The hand of a dual wield shot needs nothing here: the weapon states ps+0x1d0 / +0x1d4
    // still say which hand fires when this runs (6 / 0, 0 / 6 or 6 / 6), and the fire events
    // 0x21 / 0x23 do not come for a shadow player's entity.
    uint8_t* ps = cg + engine::kCgPredictedPs;
    __declspec(align(16)) uint8_t fpu[128];
    bool fpuSave = ReadIniInt(L"p2_recoil_fpu", 1) != 0;
    if (fpuSave) {
        __asm { fnsave fpu }
        __asm {
            fldcw word ptr fpu
        }
    }
    unsigned tags = fpuSave ? *(const uint16_t*)(fpu + 8) : 0xffff; // 0xffff = the stack was empty
    // p2_recoil_dw_hip=1: a dual wield weapon always kicks from the hip. The function takes
    // its aiming branch when the aim fraction ps+0x1e0 is exactly 1.0 and scales the kick by
    // limit / length there; a dual wield weapon has no aiming kick values (it never aims), the
    // length is 0 and the result is not a number. A shadow player's left weapon fires with the
    // aim button and its aim fraction rises to 1.0 while it is held (every call that gives
    // not-a-number has exactly 1.000, none below; the right pistol kicks correctly, the left one
    // does not). With the fraction at 0 for the call the hip branch runs, which picks the left or
    // the right weapon's values by the weapon states.
    int kickWeapon = engine::BG_GetWeapon(ps);
    bool dwHip = ReadIniInt(L"p2_recoil_dw_hip", 1) && kickWeapon && engine::BG_IsDualWield(kickWeapon);
    float savedAds = *(const float*)(ps + engine::kPsWeaponPosFrac);
    if (dwHip) *(float*)(ps + engine::kPsWeaponPosFrac) = 0.0f;
    for (LONG i = 0; i < shots; ++i)
        engine::BG_WeaponFireRecoil(ps, cg + 0x64a68, cg + 0x697bc, cg + 0x64a80, *(const int*)(cg + 0x695ec) > 0,
                                    cg + 0x64a84, (const void*)engine::kFireRecoilParam);
    if (fpuSave) {
        __asm { frstor fpu }
    }
    if (dwHip) *(float*)(ps + engine::kPsWeaponPosFrac) = savedAds;
    *(uint8_t**)(tls + 0x1a50) = savedCtx;
    {
        static int left2 = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_recoil_log", 0);
            if (v != armed) {
                armed = v;
                left2 = v;
            }
        }
        const float* k = (const float*)(cg + 0x697bc);
        static int badLeft = 20;
        bool bad = k[0] != k[0] || k[1] != k[1];
        if ((left2 > 0 && left2--) || (bad && badLeft > 0 && badLeft--))
            Log("[p2] recoil player %d: %ld shot(s), weapon states %d / %d, x87 tag word %04x, ads fraction %.3f, view "
                "kick velocity %.2f %.2f %.2f%s",
                slot + 2, shots, *(const int*)(ps + 0x1d0), *(const int*)(ps + 0x1d4), tags, savedAds, k[0], k[1], k[2],
                bad ? " (NOT A NUMBER, set to 0)" : "");
        if (bad) memset(cg + 0x697bc, 0, 12); // never hand a not-a-number to the view calc: this shot then has no kick
    }
    static int logLeft = 6;
    if (logLeft > 0) {
        --logLeft;
        const float* k = (const float*)(cg + 0x697bc);
        Log("[p2] recoil for player %d: %ld shot(s), view kick velocity %.2f %.2f %.2f", slot + 2, shots, k[0], k[1],
            k[2]);
    }
}

// Called at the start of every shadow pass, after the cg block and playerState swap.
void ShadowHudSecondPass(uint8_t* cg) {
    if (g_cgSwapped && cg && g_viewPass >= 1 && g_viewPass <= 3) ShadowRecoil(g_viewPass - 1, cg);
    if (g_viewPass == 1) LuiDumpRoots();
    // p_health_log=N (diagnostic): N lines, two per second, with what the low-health overlay
    // of the pass reads: cgame time, the time of the last damage (cg+0x6986c), the gate
    // cg+0x695b4, the playerState's damage counter, damage amount and health, and the damage
    // counter the cgame block last handled (cg+0x80b7c + 4).
    if (cg && g_viewPass >= 1) {
        static int left = -1, armed = -1;
        static DWORD next;
        int v = ReadIniInt(L"p_health_log", 0);
        if (v != armed) {
            armed = v;
            left = v;
        }
        if (left > 0 && (LONG)(GetTickCount() - next) >= 0) {
            next = GetTickCount() + 500;
            --left;
            const uint8_t* ps = cg + engine::kCgPredictedPs;
            Log("[health] pass %d swapped %d time %d lastDamage %d gate %d | ps count %d amount %d health %d / %d | "
                "handled %d %d",
                g_viewPass, (int)g_psSwapped, *(const int*)(cg + engine::kCgTime), *(const int*)(cg + 0x6986c),
                *(const int*)(cg + 0x695b4), *(const int*)(ps + 0x228), *(const int*)(ps + 0x234),
                *(const int*)(ps + 0x238), *(const int*)(ps + 0x240), *(const int*)(cg + 0x80b7c),
                *(const int*)(cg + 0x80b80));
        }
    }
    if (!g_cgSwapped || !cg || !ReadIniInt(L"p2_hud_own", 1)) return;
    int slot = HudCur();
    HudSlot& hs = g_hud[slot];
    const uint8_t* cl = slot == 0 ? g_p2.cl : ExtraCl(slot - 1);
    if (!cl || (slot > 0 && !ReadIniInt(L"p_hud_extra", 1))) return;
    uint8_t* root1 = hs.root;
    if (hs.ready && hs.match != g_p2.startedTick) {
        hs.ready = false;
        Log("[hud] UIRoot%d dropped (new match)", slot + 1);
    }
    void* L = *(void**)engine::kLuiState;
    if (!hs.ready) {
        if (!*(const uint8_t*)engine::kLuiRoot0Active || !L) return;
        memcpy(root1, (const void*)engine::kLuiRoot0, kRootSize);
        memset(root1 + 0x80, 0, kRootSize - 0x80);
        *(int*)(root1 + 0x80) = ReadIniInt(L"p2_hud_slot", 6);
        sprintf_s((char*)root1 + 0x8c, 0x20, "UIRoot%d", slot + 1);
        int vr[4];
        ShadowViewRect(slot + 1, *(const int*)engine::kVidWidth, *(const int*)engine::kVidHeight, vr);
        float w = (float)vr[2], h = (float)vr[3];
        int controller = slot == 0 ? ReadIniInt(L"p2_hud_controller", 1) : slot + 1;
        g_hudBusy = true;
        RootInitCall(root1, 0.0f, 0.0f, w, h);
        engine::LuiRootAddMenu((const char*)root1 + 0x8c, "HUD", controller, L);
        // The HUD menu starts with the loading / level-intro card and hides it on the
        // "first_snapshot" event, which the engine sends once per match from the UI pass
        // (0x0055b370, flags cg+0x68854/0x68855); the new root needs its own.
        if (ReadIniInt(L"p2_hud_first_snapshot", 1) &&
            engine::LUI_BeginEvent((const char*)root1 + 0x8c, "first_snapshot", L)) {
            engine::LUI_SetTableNumber("controller", controller, L);
            engine::LUI_SetTableBool("activateDemoStartScreen", 0, L);
            engine::LUI_SetTableBool("openHighlightStartScreen", 0, L);
            engine::LUI_EndEvent(L);
        }
        g_hudBusy = false;
        hs.ready = true;
        hs.match = g_p2.startedTick;
        hs.fullRefreshDone = false;
        memset(hs.scoreCache, 0, sizeof(hs.scoreCache)); // new root: every score line is sent once
        hs.cfPsValid = false;                            // and every client field that is set
        hs.visOld[0] = hs.visOld[1] = 0;                 // and every visibility bit that is set
        hs.w = (int)w;
        hs.h = (int)h;
        Log("[hud] UIRoot%d created: %.0f x %.0f, slot %d, controller %d, active %d (UIRoot0 slot %d)", slot + 1, w, h,
            *(int*)(root1 + 0x80), controller, (int)root1[0xac], *(const int*)(engine::kLuiRoot0 + 0x80));
    }
    // p2_lui_resize=1: the half's size changed during the match (resolution, split direction):
    // the root is laid out anew with the engine's resize 0x00931800(root, x, y, w, h), the one
    // LUI_SetRootSize uses for UIRoot0.
    if (L && ReadIniInt(L"p2_lui_resize", 1)) {
        int vr[4];
        ShadowViewRect(slot + 1, *(const int*)engine::kVidWidth, *(const int*)engine::kVidHeight, vr);
        int w = vr[2], h = vr[3];
        if (w > 0 && h > 0 && (w != hs.w || h != hs.h)) {
            g_hudBusy = true;
            engine::LUI_ResizeRootStruct(root1, 0.0f, 0.0f, (float)w, (float)h);
            g_hudBusy = false;
            Log("[hud] UIRoot%d resized: %d x %d -> %d x %d", slot + 1, hs.w, hs.h, w, h);
            hs.w = w;
            hs.h = h;
        }
    }
    // hud_update_* events from player 2's playerState.
    uint8_t* snap = *(uint8_t**)(cg + engine::kCgSnap);
    if (!snap || !*(const uint32_t*)(cl + engine::kClSnapshot) || !ReadIniInt(L"p2_hud_update", 1)) return;
    // CG_SendHudUpdateEvents reads the playerState events from cg->snap (+0x28, ps at +0x10)
    // and the score / clientinfo events from cg->nextSnap (+0x24, table at +0x21a28) after
    // comparing that table with cg->snap's. So: nextSnap = this frame's copy (new scores),
    // snap = last frame's copy (old scores) carrying this frame's playerState (the ps events
    // compare against cg+0x4ae78 themselves). With the copies the other way round the score
    // events would carry the previous value (points in half 2 would be one step behind).
    uint8_t* savedNext = *(uint8_t**)(cg + engine::kCgNextSnap);
    uint8_t* savedSnap = *(uint8_t**)(cg + engine::kCgSnap);
    if (!hs.fullRefreshDone) {
        hs.fullRefreshDone = true;
        memcpy(hs.snapA, snap, kSnapSize);
        memcpy(hs.snapA + 0x10, cl + engine::kClPlayerState, engine::kPlayerStateSize);
        *(uint8_t**)(cg + engine::kCgNextSnap) = hs.snapA; // nextSnap == snap: full refresh
        *(uint8_t**)(cg + engine::kCgSnap) = hs.snapA;
    } else {
        memcpy(hs.snapB, hs.snapA, kSnapSize); // last frame's copy
        memcpy(hs.snapA, snap, kSnapSize);
        memcpy(hs.snapA + 0x10, cl + engine::kClPlayerState, engine::kPlayerStateSize);
        memcpy(hs.snapB + 0x10, hs.snapA + 0x10, engine::kPlayerStateSize); // old scores, new playerState
        *(uint8_t**)(cg + engine::kCgNextSnap) = hs.snapA;
        *(uint8_t**)(cg + engine::kCgSnap) = hs.snapB;
    }
    // p_hud_round=1: the round counter. CG_SendHudUpdateEvents compares the rounds played of
    // cg->nextSnap and cg->snap (snapshot +0x23b10) and sends "hud_update_rounds_played" with
    // the value of cg->snap (0x0094dfe8 .. 0x0094dff5, sender 0x0094cf40). Here cg->snap is
    // last frame's copy, so the event would carry the round before the change and the counter
    // of a shadow player would stay one round behind. The two values are exchanged for the call.
    int* roundNew = (int*)(hs.snapA + kSnapRoundsPlayed);
    int* roundOld = (int*)(hs.snapB + kSnapRoundsPlayed);
    bool roundSwap =
        *(uint8_t**)(cg + engine::kCgSnap) == hs.snapB && *roundNew != *roundOld && ReadIniInt(L"p_hud_round", 1);
    if (roundSwap) {
        int t = *roundNew;
        *roundNew = *roundOld;
        *roundOld = t;
    }
    ScoreCacheIn();
    engine::CG_SendHudUpdateEvents(0, 0);
    ScoreCacheOut();
    if (roundSwap) {
        Log("[hud] UIRoot%d: round counter %d -> %d", slot + 1, *roundNew, *roundOld);
        *roundNew = *roundOld; // the copy of this frame carries the new round again
    }
    *(uint8_t**)(cg + engine::kCgNextSnap) = savedNext;
    *(uint8_t**)(cg + engine::kCgSnap) = savedSnap;
    // p_hud_dpad_show=1: the ammo counter, the grenade icons and the d-pad block (Lua DPadArea)
    // fade out 15 s after the last weapon action: 0x004afe70 sends "hud_fade_dpad" with alpha 0
    // when cg+0x69870 (time of the last show) is that old and clears the flag cg+0x87680.
    // CG_SendHudInfoEvent (0x0050fe30(lc, 1)) sets both and sends alpha 1 when the flag was 0;
    // the engine calls it for the local player's shots, weapon switches and snapshot changes
    // from code that runs in player 1's cgame only. A shadow player's block gets the fade but
    // not the show: the ammo counter of players 2 to 4 would go away 15 s after their root is
    // created and stay away (two players; with three or four the roots are created after
    // that fade event and keep the counter for good). Here the show follows an ammo, weapon or
    // offhand update of the pass.
    if (hs.dpadShow) {
        hs.dpadShow = false;
        if (ReadIniInt(L"p_hud_dpad_show", 1)) {
            bool was = *(const int*)(cg + 0x87680) != 0;
            engine::CG_SendHudInfoEvent(0, 1);
            static int logLeft = 6;
            if (!was && logLeft > 0) {
                --logLeft;
                Log("[hud] UIRoot%d: ammo / d-pad block shown again after an ammo or weapon update", slot + 1);
            }
        }
    }
    ShadowHudClientFields(hs, cl);
}

// Called once per frame from the hook on 0x005459c0 (UpdateScreenRenderLocalViews calls it
// after both view passes and the screen's own 2D, 0x0055b4d0, and before the frame's last
// issue; it sets the whole screen as 2D viewport). Commands added here belong to the frame's
// own 2D list: drawn after the per-view resolve, over the whole screen (p2_full_2d). After
// UpdateScreenRenderLocalViews returned it is too late, the buffer is issued (nothing drawn).
// With a menu up (p2_menu_center=1): UIRoot0 with the whole screen as viewport. Its matrix
// (root struct +0, built by 0x0092f8e0) maps the root's pixel rect onto the viewport; with the
// clip x (left/right split) or clip y (top/bottom) halved the root keeps its size and sits in
// the middle of the screen, and what the menu lays out beyond the root's edges shows on both
// sides. The inverse (+0x40) is rebuilt with the engine's 0x00962ad0(in, out). Then the full
// root as the engine draws it after UIRoot0, and UIRoot1 in its half when the second pass did
// not draw it (the engine skips the cgame's 2D while a menu is up).
static void ShadowHudAfterFrame() {
    bool root1Drawn = g_root1Drawn;
    g_root1Drawn = false;
    if (g_viewPass < 2 || !OwnHudActive() || !g_root1[0xac] || !*(void**)engine::kLuiState || !MenuCentered()) return;
    uint8_t* rb = *(uint8_t**)engine::kCmdBuffer;
    if (!rb) return;
    int* cnt = (int*)(rb + 0x18);
    int W = *(const int*)engine::kVidWidth, H = *(const int*)engine::kVidHeight;
    static int logLeft = 3;
    if (!root1Drawn && ReadIniInt(L"p2_menu_center", 1) >= 2) {
        int vr[4];
        ShadowViewRect(1, W, H, vr);
        *cnt += 0x400;
        engine::R_AddCmdSetViewport(vr[0], vr[1], vr[2], vr[3]);
        *cnt -= 0x400;
        engine::LUI_DrawRoot(0, g_root1);
        *cnt += 0x400;
        engine::R_AddCmdSetViewport(0, 0, W, H);
        *cnt -= 0x400;
    }
    __declspec(align(16)) uint8_t tmp[kRootSize];
    memcpy(tmp, (const void*)engine::kLuiRoot0, kRootSize);
    float* m = (float*)tmp;
    if (logLeft > 0)
        Log("[hud] menu %d up, UIRoot0 matrix: %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f "
            "%.4f %.4f %.4f (UIRoot1 drawn in pass 1: %d)",
            *(const int*)engine::kActiveMenu, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11],
            m[12], m[13], m[14], m[15], (int)root1Drawn);
    // Clip x is row 0 with column vectors and column 0 with row vectors; the other row's and
    // column's entries of a scale / translate / perspective matrix are zero, so both are scaled.
    static const int kX[] = {0, 1, 2, 3, 4, 8, 12}, kY[] = {5, 4, 6, 7, 1, 9, 13};
    // The root is as large as player 1's view: half the screen in one direction (two views, or
    // the wide view of layout_three_players=1) or in both (quarters).
    float fr[4];
    ShadowViewFractions(0, fr);
    if (fr[2] < 1.0f)
        for (int i : kX)
            m[i] *= 0.5f;
    if (fr[3] < 1.0f)
        for (int i : kY)
            m[i] *= 0.5f;
    bool ok = ((char(__cdecl*)(float*, float*))0x00962ad0)(m, m + 16) != 0;
    if (logLeft > 0) {
        --logLeft;
        Log("[hud] centred UIRoot0 draw, inverse %s", ok ? "ok" : "failed");
    }
    if (!ok) return;
    *cnt += 0x400;
    engine::R_AddCmdSetViewport(0, 0, W, H);
    *cnt -= 0x400;
    engine::LUI_DrawRoot(0, tmp);
    uint8_t* full = (uint8_t*)engine::kLuiRootFull;
    if (full[0xac]) {
        *cnt += 0x400;
        engine::R_AddCmdSetViewport(0, 0, W, H);
        *cnt -= 0x400;
        engine::LUI_DrawRoot(0, full);
    }
}
typedef void(__cdecl* FrameViewport_t)();
static FrameViewport_t oFrameViewport;
static void __cdecl HookFrameViewport() {
    oFrameViewport();
    ShadowMenuFrame(); // pads of the shadow players whose own menu is open, Start presses
    ShadowHudAfterFrame();
}
static const uint8_t kProFrameViewport[] = {0xa1, 0xdc, 0x87, 0x34, 0x03}; // 5 bytes: mov eax, [0x033487dc]
extern const Detour kHudFrameHook = {"full-screen 2D viewport after the views (centred menu root)",
                                     0x005459c0,
                                     kProFrameViewport,
                                     sizeof(kProFrameViewport),
                                     (void*)HookFrameViewport,
                                     (void**)&oFrameViewport};
