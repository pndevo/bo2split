// Shadow players (slots 0 to 2 = players 2 to 4; second to fourth view): constants, the state of
// player 2's shadow client and the entry points shared by shadow_client / view / cgame_swap /
// render_backend.
#pragma once

#include "core/common.h"

// ---------------------------------------------------------------------------
// "Shadow clients" (Steam build). The engine has one local client. Every shadow player gets its
// own client structures (cl, clc, clientUIActive block, controller-map slot) allocated by this
// DLL. Per frame, after the engine's CL_Frame(0), the engine's own client-0 network code runs
// once more per shadow client with the global pointers/blocks switched to that player's copies
// ("context switch"). Player 2 talks to the listen server through loopback channel 1 (client 0
// uses channel 0, the server channel 4), with qport+1 and its own XUID, so the server treats it
// as a separate client; players 3 and 4 do the same on channels 2 and 3 (shadow_client.cpp). A
// shadow client is a full player: it has its own cgame block (see cgame_swap.cpp), is drawn in a
// view pass of its own and takes its input from a pad or the keyboard.

// Sizes of the blocks allocated for every shadow client and the layout of its client block (cl).
namespace shadowcl {
constexpr size_t kClSize = 0x8a3e0, kClcSize = 0x358a8;
constexpr size_t kUiStateSize = 0x458;
constexpr size_t kCtrlMapSize = 0x18;
constexpr size_t kSvClientSize = 0x4e180;
constexpr size_t kSvClientSteamAuthorized = 0x13817 * 4; // byte: 1 = Steam auth done (no runframe kick)
constexpr uintptr_t kClFrame = 0x00551cd0;               // void (int lc, int msec)
constexpr uintptr_t kXuidForController = 0x0086e2d0;     // uint64* (int controller)
// cl layout used here
constexpr size_t kClSnapServerTime = 0x58, kClServerId = 0x2abc, kClTimeoutCount = 0x48;
constexpr size_t kClCmds = 0x42ca8, kClCmdSize = 0x3c, kClCmdMask = 0x7f, kClCmdNumber = 0x44aa8;
} // namespace shadowcl

// The shadow client of player 2 (g_p2); players 3 and 4 keep their state inside shadow_client.cpp.
struct ShadowClient {
    bool enabled, started, inContext;
    uint8_t* cl;
    uint8_t* clc;
    uint8_t uiState[shadowcl::kUiStateSize];
    uint8_t ctrlMap[shadowcl::kCtrlMapSize];
    char xuid[32]; // Live_GetXuidForController returns a hex string, not a number
    // saved engine state while in context
    uint32_t savedCl, savedClc;
    uint8_t savedUiState[shadowcl::kUiStateSize];
    uint8_t savedCtrlMap[shadowcl::kCtrlMapSize];
    uint32_t lastLoggedState;
    int lastCmdTime;
    DWORD startedTick;
};
extern ShadowClient g_p2;

// ---------------------------------------------------------------------------
// shadow_client.cpp
extern float g_p2Pitch, g_p2Yaw;         // player 2 view angles (degrees)
extern volatile bool g_shadowAdsHeld[3]; // per view slot: the ADS action is held in the latest usercmd
bool P2AllocateClient();
void P2StartConnect();
void ShadowStop(const char* why);
uint8_t*
ExtraCl(int index); // cl of extra client index (0 = player 3) once it is in the game with a snapshot, else null
uint8_t* ShadowSlotClc(
    int slot); // clientConnection of a shadow player (slot 0 = player 2, 1 / 2 = players 3 / 4), null while it is not in the game
uint8_t* ShadowSlotCl(int slot); // the client struct of that shadow player, null while it is not in the game
int ExtraWanted();               // players 3 and 4 that are to join this match (0..2), from the lobby or extra_players
bool ExtraAngles(int index, float* pitch, float* yaw);  // pad facing of extra client index (degrees)
void ExtraSetAngles(int index, float pitch, float yaw); // sets the pad facing of extra client index (degrees)
void ShadowSetViewKick(int slot, float pitch, float yaw);
extern float g_viewKick[3][2];
void ShadowSetViewLook(int slot, float fovScale, float adsFrac); // turn rate inputs of a shadow player's view
bool PadTurnAccel();        // the pads' turn rate has the game's run-up (look_accel)
bool PadTurnOncePerFrame(); // player 1's pad turns once per frame, not once per view pass (pad_turn_once)

extern const Detour kShadowClientHooks[6];
extern const Detour kMoverHooks[1]; // mover.cpp: a shadow player carried by the bus

// input.cpp: GPad_IsActive hook
extern volatile int g_padReportedInactive;
extern const Detour kGpadInjectHook;

// view.cpp
extern volatile bool g_inRenderViews, g_viewOverride, g_vpHook;
extern volatile int g_viewPass, g_vpLogLeft;
// More than two views (p_views / extra_players): the number of view passes planned for this frame
// (2..4). Pass 0 is player 1, pass 1 player 2, passes 2 and 3 show the extra shadow clients
// (players 3 and 4). Two views split in halves, three or four in quarters (pass & 1 = right
// column, pass & 2 = bottom row).
extern volatile int g_viewCount;
extern volatile unsigned g_renderFrames; // frames with view passes since the start (counted at the first pass)
// perf_log: QPC ticks summed since the last [perf] line (written at each pass begin, read and
// cleared by the perf line in main.cpp): per pass, the rest of the frame after the last pass
// begin, the longest frame, and the frames longer than slowLimit.
struct PerfTimes {
    LONGLONG pass[4], tail, maxFrame, slowLimit;
    unsigned slowFrames;
};
extern PerfTimes g_perf;
void ShadowViewFractions(int pass, float fr[4]);       // x, y, w, h as fractions of the screen (pass < 0: whole screen)
void ShadowViewRect(int pass, int W, int H, int r[4]); // the same in pixels of a W x H target
uint8_t* ShadowViewCl(); // client struct whose playerState the current pass shows (pass >= 1)
inline bool ExtraPass() {
    return g_inRenderViews && g_viewPass >= 2;
}
extern bool g_psSwapped;
extern int g_p2CmdWeapon[3]; // weapon, offhand, alt id player 2's cgame block selected in the last second pass
extern volatile bool
    g_p2WeapNextPending; // pad Y / weapnext: run CG_WeapNext_f on player 2's block in the next second pass
// The same per view slot (0 = player 2, 1 / 2 = players 3 / 4) for the extra players: the weapon
// ids their cgame block selected in their last pass, and a pending weapon cycle.
extern int g_viewCmdWeapon[3][3];
// The server's last weapon switch ("h <weapon>") per shadow player (slot 0 = player 2): weapon,
// tick it was carried out, and a flag an extra player's usercmd takes once.
extern int g_srvWeapon[3];
extern DWORD g_srvWeaponTick[3];
extern volatile LONG g_srvWeaponSwitched[3];
extern volatile bool g_viewWeapNext[3];
extern volatile bool g_p2WeapPrevPending, g_viewWeapPrev[3];
// One hook table per file of the view passes: view.cpp, playerstate.cpp, entities.cpp,
// predict.cpp, scene.cpp.
extern const Detour kViewHooks[13];
extern const Detour kViewPsHooks[2];
extern const Detour kViewEntityHooks[8];
extern const Detour kViewPredictHooks[5];
extern const Detour kViewSceneHooks[4];
extern const BytePatch kViewLoopPatches[10];
void EnlargeCommandBuffers();

// hud.cpp (p2_hud_own=1: LUI root UIRoot1 for player 2, see the file header)
extern const Detour kHudHooks[6];
extern const Detour kHudScoreHook;
extern const Detour kHudHealthHook;
extern const Detour kHudVisBitsHook;
extern const Detour kHudCfHooks[2];
extern const Detour kLuiErrorHook;
void ShadowHudSecondPass(uint8_t* cg);
bool ShadowHudEvent(int player, const char* name, int controller, const char* const* fields, int count);
// A field of the Lua table global.table as its raw 8 byte value (hud.cpp).
bool LuiTableGet(const char* global, const char* table, const char* key, unsigned long long* value);
bool LuiTableSet(const char* global, const char* table, const char* key, unsigned long long value);
extern const Detour kHudFrameHook;
extern const Detour kHudMouseHook;
extern const Detour kSoundListenerHook;
extern const Detour kSoundFireHooks[3];
extern volatile LONG g_shadowShots[3];

// cgame_swap.cpp
extern bool g_cgResetPending;
extern bool g_cgSwapped;                    // a shadow pass currently runs on its own cg block
void ShadowCgSwapIn(uint8_t* cg, int slot); // slot 0 = player 2, 1 / 2 = players 3 / 4
void ShadowCgSwapOut(uint8_t* cg);
const uint8_t* ShadowPlayer1Cg();     // player 1's cgame block (the saved copy while a shadow pass runs on its own)
void P2CgSetWeaponSelect(int weapon); // cg+0x69674 of player 2's block (whichever buffer holds it right now)
extern const Detour kCgSwapHooks[1];  // a shadow player's viewmodel hands (p2_vm_own_hands)

// gump.cpp (p2_gump=1: TranZit area fastfile of player 2 in gump slot 1)
extern const Detour kGumpHooks[3];

// weapnext is not a usercmd button bit: pad and script set this pseudo bit in buttons2,
// P2FillUsercmdFromPad turns its rising edge into g_p2WeapNextPending and masks it out.
constexpr int kWeapNextPseudoBit = 0x40000000;
constexpr int kWeapPrevPseudoBit = 0x20000000; // the same for weapprev (g_p2WeapPrevPending, g_viewWeapPrev)

// controls.cpp (pad layout of player 2, pad glyphs in its hint texts)
// Actions a pad input can be mapped to (values of the btn_* ini keys).
enum Action {
    kActNone = 0,
    kActAttack,
    kActAds,
    kActLethal,
    kActTactical,
    kActUse,
    kActJump,
    kActStance,
    kActSprint,
    kActMelee,
    kActWeapNext,
    kActSlot1,
    kActSlot2,
    kActSlot3,
    kActSlot4,
    kActScores,
    kActMenu,
    kActWeapPrev,
    kActCount
};
inline unsigned ActBit(int a) {
    return (a > kActNone && a < kActCount) ? 1u << a : 0u;
}
// slot: 0 = player 2, 1 / 2 = players 3 / 4. kbmActs: actions held on keyboard and mouse
// (ShadowControlsKbmActions) when the player's device is keyboard and mouse, else 0.
void ShadowControlsButtons(int slot, unsigned padButtons, unsigned lt, unsigned rt, unsigned kbmActs, int* buttons,
                           int* buttons2);
unsigned ShadowControlsKbmActions();
bool ShadowControlsTakeWeaponChange(int slot); // an action slot was pressed since the last call
void ShadowControlsSecondPass(uint8_t* cg);
void ShadowControlsMenuButtonFrame(); // the menu button of players whose usercmds stand still (pause)
bool ShadowControlsTakePauseToggle(); // a pad asked for the pause menu toggle since the last call
bool ShadowControlsScoresHeld(int slot);
extern const Detour kControlsHooks[4];
// player_menu.cpp (player_menu=1: the game's controller options for a shadow player, inside its view)
struct ShadowSettings {
    float sensitivity;                   // look speed factor, 1.0 = the default
    int invert, buttons, sticks, rumble; // look inversion, button layout 0..8, stick layout 0..3, vibration
};
bool ShadowMenuOpen(int slot);                        // slot 0 = player 2
bool ShadowScoresOpen(int slot);                      // the player holds its scoreboard open
const ShadowSettings& ShadowPlayerSettings(int slot); // the values the player chose in its menu
void ShadowMenuToggle(int slot);                      // Start on the player's pad
void ShadowControlsPauseToggle();                     // asks for the toggle of player 1's pause menu
bool ShadowHudLuaRunning();                           // the tick or an event of a shadow player's root runs
bool ShadowHudReady(int slot);                        // the player has a HUD root of its own
void ShadowHudResend(int slot);                       // sends that root every score line and client field again
void ShadowHudRebuild(int slot);                      // drops that root; the player's next pass creates it anew
bool ShadowMenuButton(int slot, const char* button);  // a menu button, press and release
void ShadowMenuFrame();
bool ShadowMenuTickBegin(int slot); // the player's profile for the tick of its root; true = call ShadowMenuTickEnd
void ShadowMenuTickEnd();
bool ShadowMenuHandling(); // a shadow player's menu handles an event or its root ticks right now
extern const Detour kPlayerMenuHooks[8];
// input.cpp: input device per player (p1_pad .. p4_pad), see the file header
constexpr int kDevAuto = -3, kDevKbm = -2, kDevNone = -1; // kDevAuto: an ini value of player 1 only
int PlayerDevice(int player);                             // ini value of player 0..3: kDevKbm, kDevNone or a pad 0..3
int PlayerInputDevice(int player); // the device the player really gets (a shared one goes to the lowest player)
int KbmOwner();                    // player that has keyboard and mouse (0 = player 1)
int P1EnginePad();                 // XInput pad the engine's pad 0 reads for player 1, -1 = none
struct KbmState {
    float fwd, right;
    int dx, dy;
    unsigned acts;
    bool ads;
};
bool KbmRead(int player, KbmState* k); // false: not this player's, or the game is not taking input
void PInputFrame();
// rumble.cpp (player_rumble=1: vibration on the pads of the shadow players)
void ShadowRumbleSwapIn(int slot, bool fresh); // the player's own list of playing rumbles, with its cgame block
void ShadowRumbleSwapOut();
void ShadowRumbleFrame();
void ShadowRumbleShot(const uint8_t* cg); // the fire rumble of the weapon in hand, in the player's own pass
extern const Detour kRumbleHooks[4];
// lobby.cpp (lobby_join=1: players join in the lobby with A on their pad, B leaves)
bool LobbyJoinOn();
int LobbyJoined();        // players 2..4 that joined: 0..3
int LobbyPad(int player); // pad of player 1..3 (players 2..4), -1 = not joined
void LobbyFrame();
extern const Detour kLobbyHintHook; // "press A to join" line in the lobby (lobby_hint)
extern const Detour kInputHooks[3];
// fx.cpp: effect culling against both players' cameras.
void ShadowFxCaptureCamera(const uint8_t* sys);
void ShadowFxLog(int pass);
void ShadowViewFlash(const uint8_t* cg);
bool WeaponFxInPass(int pass); // the effects of the first-person weapon are drawn in this pass (render thread)
extern const Detour kFxHooks[7];

// render_backend.cpp
typedef void(__cdecl* IssueRenderCommands_t)(unsigned);
typedef LONG(__cdecl* DequeueRenderBuffer_t)();
extern void* oSetTargetViewport;
extern IssueRenderCommands_t oIssueRenderCommands;
extern DequeueRenderBuffer_t oDequeueRenderBuffer;
void HookSetTargetViewport();
void __cdecl HookIssueRenderCommands(unsigned flags);
LONG __cdecl HookDequeueRenderBuffer();
extern const uint8_t kProSetTargetViewport[7];
extern const uint8_t kProIssueRenderCommands[10];
extern const uint8_t kProDequeueRenderBuffer[7];
typedef void(__cdecl* SingleViewBackdrop_t)();
extern SingleViewBackdrop_t oSingleViewBackdrop;
void __cdecl HookSingleViewBackdrop();
extern const uint8_t kProSingleViewBackdrop[6];
typedef void(__cdecl* ResolveViews_t)();
extern ResolveViews_t oResolveViews;
void __cdecl HookResolveViews();
extern const uint8_t kProResolveViews[6];
typedef void(__cdecl* RbDraw3D_t)();
extern RbDraw3D_t oRbDraw3D;
void __cdecl HookRbDraw3D();
extern const uint8_t kProRbDraw3D[6];
extern volatile int g_beLogLeft;
extern volatile LONG g_drawCount;
