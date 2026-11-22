#include "core/common.h"
#include "shadow/shadow.h"
#include "core/dev.h"
#include "engine/common.h"
#include "engine/client.h"
#include "engine/offsets.h"

// The complete pad layout of player 2, and pad button glyphs instead of keyboard key names
// in the hint texts of player 2's half.
//
// Where the engine builds the usercmd buttons:
//   CL_ExecuteKeyBinding 0x00541600(lc, bindingIndex, key, time, value): jump table 0x00542404
//   over the binding index (name table 0x00c85200: 1 "+attack", 2 "-attack", 3 "+melee", ...);
//   most cases press or release one kbutton of the block 0x01074b80 (0x438 per local client,
//   0x18 per kbutton: +0x10 active, +0x11 wasPressed, +0x14 analog value).
//   CL_CmdButtons 0x00542d30 (EAX = usercmd, lc on the stack) turns the kbuttons into
//   cmd+4 (buttons) and cmd+8 (buttons2); CL_KeyMove 0x00542900 (EDI = usercmd) adds the
//   stance, speed and sprint bits. The script builtins (method table 0x00b436d8, e.g.
//   jumpbuttonpressed 0x005ed3a0) test the same bits in gclient+0x56a4 / +0x56a8.
//
//   binding               kbutton   buttons      buttons2   script builtin that tests it
//   +attack               17        0x80000000              attackbuttonpressed
//   +gas (same pad key)   34        0x00001c00 (3-bit analog, bits 19..21)
//   +speed_throw          9 and 30  0x00100080              adsbuttonpressed 0x100000, throwbuttonpressed 0x80
//   +reverse (LT)         35        0x00000200
//   +frag                 19        0x00020000              fragbuttonpressed
//   +smoke                20        0x00010000              secondaryoffhandbuttonpressed
//   +usereload            25        0x04000000
//   +activate / +reload   23 / 24   0x10000000 / 0x08000000
//   +melee                22        0x20000000              meleebuttonpressed
//   +breath_sprint        18 and 32 0x40040000              sprintbuttonpressed 0x40000000
//   +gostand              10        0x00200000              jumpbuttonpressed (only when standing, else stand up)
//   +stance               31                     0x00000002 stancebuttonpressed
//   stance crouch / prone (cl+0x2bd0 = 1 / 2, CL_SetStanceBits 0x00542890): 0x00400000 / 0x00800000
//   +switchseat           37        0x00000008              changeseatbuttonpressed
//   +handbrake            36        0x00000100
//   +actionslot 1..4      (cgame)                0x40000, 0x20000, 0x10000, 0x8000  actionslot{one..four}buttonpressed
//
// The stance is client state, not a button: +stance (0x00540aa0 press, 0x00540bd0 release,
// 0x00540f10 per frame) sets cl+0x2bd0, and every usercmd carries the crouch or prone bit
// for as long as that state lasts. A tap toggles crouch, holding for 300 ms goes prone (or
// stands up from prone); +gostand stands up first and jumps only when already standing
// (case 33 at 0x00541a84). The same state machine runs here for player 2 (StanceStep).
// The dive to prone (p2_dive=1): while the player can dive (0x0053fff0: playerState +0xc has
// 0x8000, sprinting, and the float +0x208 is at least the int dvar 0x0314eba4) a press of
// +stance changes nothing at first; held for 200 ms it sets stance 3, released earlier it
// crouches. Stance 3 is the prone bit plus buttons2 0x80000 (0x005428b9). The stance leaves 3
// when the game sets it: CL_SetStance 0x00540150(lc, stance), called for the entity events 8,
// 9 and 10 of the own client (stand, crouch, prone; 0x004c2a10) and on a respawn. In a shadow
// player's pass that call would write player 1's stance; the hook below hands it to the shadow
// player's state instead (p2_stance_events=1).
//
// The action slots are cgame work: CG_ActionSlotDown 0x0053a3e0(lc, slot 0..3) puts the
// slot's bit into cg+0x80b40, selects the slot's weapon (ps+0x550 type, ps+0x560 weapon:
// 0x0053a220) or the alt weapon (0x0053a0b0), and CG_SubmitInputAndView hands cg+0x80b3c /
// +0x80b40 to CL_AddUserCmdButtons 0x00547580(lc, int bits[2]). In the second pass that call
// writes into player 1's cl (p2_keep_cl_input discards that), so the hook below
// collects the bits of player 2's block for player 2's next usercmd instead.
//
// Hint texts: every "[{+command}]" token and every key name in a hint goes through
// UI_GetKeyBindingLocalizedString 0x0071e420(lc, command, out[0x100], bindNum, 0, 0, 0, 1, 0,
// tryAlternate), 51 callers (CG_DrawCursorHint's helper 0x004dd100, the token replacer
// 0x006f7590 called from 0x006f76e0, the Lua binding around 0x00947e77). It asks
// GPad_InUse 0x0077c180(lc), which is Com_LocalClient_LastInput_Get 0x006be7e0(lc) == 0: the
// dword at 0x02480704 + lc * 0x18 (local-client table slot +0x14; 0 = gamepad, 1 and 2 =
// mouse, 3 = keyboard, written by 0x006be700 from the key, mouse and pad handlers). With a
// gamepad it looks only at the pad keys (1..6, 0xe..0x19, 0x1c..0x1f) in
// Key_GetCommandAssignmentInternal 0x0054cb20, and Key_KeynumToString 0x0054c950(lc, key,
// translate) answers with the glyph string of the table 0x00c85420 ("^BXENONButtonA^", ...).
// Player 2's buttons are this file's table, not the engine's bindings, so in the second
// pass the hook answers from the table with the same glyph strings (p2_hint_pad=1).

namespace {

// Pad inputs: XInput button mask (0 = trigger), the engine's key number for the glyph table
// 0x00c85420, the ini key and the default action (the console default layout of BO2).
struct PadInput {
    const char* name;
    unsigned mask;
    int trigger;
    int engineKey;
    const wchar_t* iniKey;
    int defAction;
};
const PadInput kInputs[] = {
    {"RT", 0, 2, 0x13, L"btn_rt", kActAttack},
    {"LT", 0, 1, 0x12, L"btn_lt", kActAds},
    {"RB", 0x0200, 0, 0x06, L"btn_rb", kActLethal},
    {"LB", 0x0100, 0, 0x05, L"btn_lb", kActTactical},
    {"X", 0x4000, 0, 0x03, L"btn_x", kActUse},
    {"A", 0x1000, 0, 0x01, L"btn_a", kActJump},
    {"B", 0x2000, 0, 0x02, L"btn_b", kActStance},
    {"LS", 0x0040, 0, 0x10, L"btn_ls", kActSprint},
    {"RS", 0x0080, 0, 0x11, L"btn_rs", kActMelee},
    {"Y", 0x8000, 0, 0x04, L"btn_y", kActWeapNext},
    {"DUp", 0x0001, 0, 0x14, L"btn_dpad_up", kActSlot1},
    {"DDown", 0x0002, 0, 0x15, L"btn_dpad_down", kActSlot2},
    {"DLeft", 0x0004, 0, 0x16, L"btn_dpad_left", kActSlot3},
    {"DRight", 0x0008, 0, 0x17, L"btn_dpad_right", kActSlot4},
    {"Back", 0x0020, 0, 0x0f, L"btn_back", kActScores},
    {"Start", 0x0010, 0, 0x0e, L"btn_start", kActMenu},
};
constexpr int kInputCount = sizeof(kInputs) / sizeof(kInputs[0]);

// usercmd bits per action (table in the file header). Attack, ads and sprint keep the extra
// vehicle bits the real pad key sets as well (+gas, +reverse, +holdbreath).
constexpr int kBitsAttack = (int)0x80001c00, kBitsAds = 0x00100280, kBitsLethal = 0x00020000,
              kBitsTactical = 0x00010000;
constexpr int kBitsUse = 0x04000000, kBitsJump = 0x00200000, kBitsSprint = 0x40040000, kBitsMelee = 0x20000000;
constexpr int kBitsCrouch = 0x00400000, kBitsProne = 0x00800000;
constexpr int kBits2Dive = 0x00080000;
constexpr int kBits2Stance = 0x00000002, kBits2Slot1 = 0x00040000; // slot n: kBits2Slot1 >> n

// Engine commands a hint may ask for.
const struct {
    const char* command;
    int action;
} kCommands[] = {
    {"+attack", kActAttack},       {"+speed_throw", kActAds},
    {"+toggleads_throw", kActAds}, {"toggleads", kActAds},
    {"+throw", kActAds},           {"+frag", kActLethal},
    {"+smoke", kActTactical},      {"+activate", kActUse},
    {"+usereload", kActUse},       {"+reload", kActUse},
    {"+gostand", kActJump},        {"+moveup", kActJump},
    {"+stance", kActStance},       {"gocrouch", kActStance},
    {"togglecrouch", kActStance},  {"+movedown", kActStance},
    {"goprone", kActStance},       {"toggleprone", kActStance},
    {"+prone", kActStance},        {"+breath_sprint", kActSprint},
    {"+sprint", kActSprint},       {"+holdbreath", kActSprint},
    {"+melee", kActMelee},         {"+melee_zoom", kActMelee},
    {"+melee_breath", kActMelee},  {"weapnext", kActWeapNext},
    {"weapprev", kActWeapPrev},    {"+weapnext_inventory", kActWeapNext},
    {"+actionslot 1", kActSlot1},  {"+actionslot 2", kActSlot2},
    {"+actionslot 3", kActSlot3},  {"+actionslot 4", kActSlot4},
    {"+scores", kActScores},       {"togglescores", kActScores},
    {"togglemenu", kActMenu},      {"pause", kActMenu},
};

// Ini values, re-read every 250 ms (the hint hook runs many times per frame).
struct Config {
    int map[kInputCount], actionSlot, cgButtons, startMenu, hintPad, gamepadState, stanceHoldMs, log, hintTrace, dive,
        stanceEvents;
};
Config g_cfg;
DWORD g_cfgTick;
bool g_cfgValid;
const Config& Cfg() {
    DWORD now = GetTickCount();
    if (g_cfgValid && now - g_cfgTick < 250) return g_cfg;
    g_cfgTick = now;
    g_cfgValid = true;
    for (int i = 0; i < kInputCount; ++i)
        g_cfg.map[i] = ReadIniInt(kInputs[i].iniKey, kInputs[i].defAction);
    g_cfg.actionSlot = ReadIniInt(L"p2_actionslot", 1);
    g_cfg.cgButtons = ReadIniInt(L"p2_cg_buttons", 1);
    g_cfg.startMenu = ReadIniInt(L"p2_start_menu", 1);
    g_cfg.hintPad = ReadIniInt(L"p2_hint_pad", 1);
    g_cfg.gamepadState = ReadIniInt(L"p2_hint_gamepad_state", 0);
    g_cfg.stanceHoldMs = ReadIniInt(L"stance_hold_ms", 300);
    g_cfg.log = ReadIniInt(L"p2_controls_log", 0);
    g_cfg.hintTrace = ReadIniInt(L"p2_hint_trace", 0);
    g_cfg.dive = ReadIniInt(L"p2_dive", 1);
    g_cfg.stanceEvents = ReadIniInt(L"p2_stance_events", 1);
    return g_cfg;
}

// Stance of player 2 (cl+0x2bd0 of the engine: 0 stand, 1 crouch, 2 prone).
// 3 = diving to prone. canDive: the player sprints fast enough for the dive right now.
struct Stance {
    int stance, prev;
    bool held;
    DWORD pressTick, diveTick;
};
void StanceStep(Stance& st, bool down, bool wasDown, DWORD now, int holdMs, bool canDive) {
    if (down && !wasDown) { // 0x00540aa0
        st.prev = st.stance;
        st.pressTick = now;
        st.held = true;
        if (!canDive && st.stance != 1) st.stance = 1;
    }
    if (st.held && (int)(now - st.pressTick) >= (canDive ? 200 : holdMs)) { // 0x00540f10
        if (canDive) {
            st.stance = 3;
            st.diveTick = now;
        } else
            st.stance = st.prev == 2 ? 0 : 2;
        st.held = false;
    }
    if (!down && wasDown) { // 0x00540bd0
        if (st.held && canDive)
            st.stance = 1;
        else if (st.held && st.prev == 1)
            st.stance = 0;
        st.held = false;
    }
    // The game ends the dive with a stance event (see the hook of CL_SetStance). Should none
    // arrive, the player lies after 1.5 s.
    if (st.stance == 3 && (int)(now - st.diveTick) >= 1500) st.stance = 2;
}

// State per shadow player (slot 0 = player 2, 1 / 2 = players 3 / 4).
struct PlayerCtl {
    Stance stance;
    unsigned prevActs;           // actions down in the previous usercmd
    bool jumpStoodUp;            // this press of jump stood the player up: no jump bit until released
    DWORD match;                 // g_p2.startedTick of the match the state belongs to
    volatile LONG cgBits[2];     // bits the player's cgame block handed to CL_AddUserCmdButtons
    volatile LONG slotPending;   // action slots to run on the player's block in its next pass (bit n = slot n)
    volatile LONG weaponChange;  // an action slot was pressed (the usercmd weapon follows the block's selection)
    volatile bool canDive;       // sprinting fast enough for the dive, read from the player's playerState in its pass
    volatile bool scoresDown;    // scoreboard button held
    bool adsToggled, adsWasDown; // button layouts with an aim toggle
    int lastB;
    volatile DWORD filled; // tick of the player's last usercmd (ShadowControlsButtons)
};
PlayerCtl g_ctl[3];
volatile LONG g_menuPending; // pause menu toggle for the next OnFrame
int g_logLeft, g_hintLogLeft;

bool InShadowPass() {
    return g_inRenderViews && g_viewPass >= 1;
}
int CurSlot() {
    return (g_inRenderViews && g_viewPass >= 1 && g_viewPass <= 3) ? g_viewPass - 1 : 0;
}

// Button layouts of the game's controller options (profile value gpad_buttonsConfig): which
// input's action a physical input performs, as indices into kInputs. 1 "tactical" swaps B and
// the right stick button; 2 "lefty" swaps the triggers, the bumpers and the stick buttons; 3
// "N0M4D" is tactical with the aim button as a toggle; 4 "charlie" has the lethal grenade on
// the left trigger and the aim toggle on the right bumper; 5 to 8 are 0 to 3 with triggers and
// bumpers flipped (fire and aim on the bumpers; the options menu does not offer them).
const int* ButtonLayout(int layout) {
    static int table[9][kInputCount];
    static bool built;
    if (!built) {
        built = true;
        auto swap = [](int* t, int a, int b) {
            int x = t[a];
            t[a] = t[b];
            t[b] = x;
        };
        for (int l = 0; l < 9; ++l) {
            int* t = table[l];
            for (int i = 0; i < kInputCount; ++i)
                t[i] = i;
            int base = l >= 5 ? l - 5 : l;
            if (base == 1 || base == 3) swap(t, 6, 8);
            if (l == 4) swap(t, 1, 2);
            if (base == 2) {
                swap(t, 0, 1);
                swap(t, 2, 3);
                swap(t, 7, 8);
            }
            if (l >= 5) {
                swap(t, 0, 2);
                swap(t, 1, 3);
            }
        }
    }
    return table[(layout >= 0 && layout < 9) ? layout : 0];
}

int ActionForCommand(const char* command) {
    for (const auto& c : kCommands)
        if (_stricmp(command, c.command) == 0) return c.action;
    return kActNone;
}

// Glyph string of an engine pad key from the engine's own table 0x00c85420 (16 pairs of
// key number and string, the table Key_KeynumToString 0x0054c950 searches).
const char* PadGlyph(int engineKey) {
    struct Entry {
        int key;
        const char* name;
    };
    const Entry* table = (const Entry*)engine::kPadGlyphTable;
    for (int i = 0; i < 16; ++i)
        if (table[i].key == engineKey) return table[i].name;
    return nullptr;
}

} // namespace

// The menu button of a shadow player went down: the pause menu toggle, through the player's
// menu code with player_menu=1.
static void MenuPressed(int slot) {
    Log("[ctl] player %d menu button", slot + 2);
    if (ReadIniInt(L"player_menu", 1))
        ShadowMenuToggle(slot);
    else
        InterlockedExchange(&g_menuPending, 1);
}

// Once per frame (from ShadowMenuFrame): the menu button of the players whose usercmds stand
// still. While the match is paused no usercmd is built for players 3 and 4, so their pad is not
// read by ShadowControlsButtons, and their Start could not end the pause or bring their menu
// back.
void ShadowControlsMenuButtonFrame() {
    const Config& c = Cfg();
    if (!g_p2.started || !c.startMenu || !pGetState) return;
    DWORD now = GetTickCount();
    for (int slot = 0; slot < 3; ++slot) {
        PlayerCtl& s = g_ctl[slot];
        if (!s.filled || now - s.filled < 150) continue; // its usercmds run: read there
        struct {
            DWORD packet;
            WORD buttons;
            BYTE lt, rt;
            SHORT lx, ly, rx, ry;
        } st = {};
        int dev = PlayerInputDevice(slot + 1);
        if (dev < 0 || pGetState((DWORD)dev, &st) != ERROR_SUCCESS) continue;
        const int* layout = ButtonLayout(ShadowPlayerSettings(slot).buttons);
        bool down = false;
        for (int i = 0; i < kInputCount; ++i) {
            const PadInput& in = kInputs[i];
            bool held = in.trigger == 1 ? st.lt > 60 : in.trigger == 2 ? st.rt > 60 : (st.buttons & in.mask) != 0;
            if (held && c.map[layout[i]] == kActMenu) down = true;
        }
        bool was = (s.prevActs & ActBit(kActMenu)) != 0;
        s.prevActs = (s.prevActs & ~ActBit(kActMenu)) | (down ? ActBit(kActMenu) : 0);
        if (down && !was) MenuPressed(slot);
    }
}

// Called by the usercmd fill of a shadow player (slot 0 = player 2, 1 / 2 = players 3 / 4)
// with its pad state and the actions held on keyboard and mouse. ORs the player's usercmd
// bits into buttons / buttons2; the weapon cycle leaves as kWeapNextPseudoBit, which the
// caller handles. All pad players share the layout of the btn_* keys.
void ShadowControlsButtons(int slot, unsigned padButtons, unsigned lt, unsigned rt, unsigned kbmActs, int* buttons,
                           int* buttons2) {
    const Config& c = Cfg();
    if (slot < 0 || slot > 2) return;
    PlayerCtl& s = g_ctl[slot];
    DWORD now = GetTickCount();
    s.filled = now ? now : 1;
    if (s.match != g_p2.startedTick) { // new match: standing, nothing held
        s.match = g_p2.startedTick;
        s.stance = {};
        s.prevActs = 0;
        s.jumpStoodUp = false;
        s.scoresDown = false;
        s.adsToggled = s.adsWasDown = false;
        s.canDive = false;
        InterlockedExchange(&s.cgBits[0], 0);
        InterlockedExchange(&s.cgBits[1], 0);
        InterlockedExchange(&s.slotPending, 0);
        InterlockedExchange(&s.weaponChange, 0);
        if (slot == 0) InterlockedExchange(&g_menuPending, 0);
    }
    unsigned acts = kbmActs;
    const int* layout = ButtonLayout(ShadowPlayerSettings(slot).buttons);
    for (int i = 0; i < kInputCount; ++i) {
        const PadInput& in = kInputs[i];
        bool down = in.trigger == 1 ? lt > 60 : in.trigger == 2 ? rt > 60 : (padButtons & in.mask) != 0;
        if (down) acts |= ActBit(c.map[layout[i]]);
    }
    // Layouts 3 and 4: the aim button toggles.
    int buttonLayout = ShadowPlayerSettings(slot).buttons;
    if (buttonLayout == 3 || buttonLayout == 4) {
        bool down = (acts & ActBit(kActAds)) != 0;
        if (down && !s.adsWasDown) s.adsToggled = !s.adsToggled;
        s.adsWasDown = down;
        if (acts & ActBit(kActSprint)) s.adsToggled = false;
        acts = (acts & ~ActBit(kActAds)) | (s.adsToggled ? ActBit(kActAds) : 0);
    } else
        s.adsToggled = s.adsWasDown = false;
    acts |= DevScriptedActions(slot, now);
    if (ShadowMenuOpen(slot)) { // the pad drives the player's menu, Start closes it
        acts &= ActBit(kActMenu);
        s.adsToggled = false;
    }
    unsigned pressed = acts & ~s.prevActs;

    int b = 0, b2 = 0;
    if (acts & ActBit(kActAttack)) b |= kBitsAttack;
    if (acts & ActBit(kActAds)) b |= kBitsAds;
    if (acts & ActBit(kActLethal)) b |= kBitsLethal;
    if (acts & ActBit(kActTactical)) b |= kBitsTactical;
    if (acts & ActBit(kActUse)) b |= kBitsUse;
    if (acts & ActBit(kActSprint)) b |= kBitsSprint;
    if (acts & ActBit(kActMelee)) b |= kBitsMelee;
    if (acts & ActBit(kActWeapNext)) b2 |= kWeapNextPseudoBit;
    if (acts & ActBit(kActWeapPrev)) b2 |= kWeapPrevPseudoBit;

    // Stance button, then jump: jump stands up from crouch / prone and jumps only when standing.
    bool stanceDown = (acts & ActBit(kActStance)) != 0;
    int stanceBefore = s.stance.stance;
    StanceStep(s.stance, stanceDown, (s.prevActs & ActBit(kActStance)) != 0, now, c.stanceHoldMs, s.canDive && c.dive);
    if (s.stance.stance == 3 && stanceBefore != 3) Log("[ctl] player %d dives to prone", slot + 2);
    if (stanceDown) b2 |= kBits2Stance;
    if (pressed & ActBit(kActJump)) {
        s.jumpStoodUp = s.stance.stance != 0;
        if (s.jumpStoodUp) {
            s.stance.stance = 0;
            s.stance.held = false;
        }
    }
    if ((acts & ActBit(kActJump)) && !s.jumpStoodUp) b |= kBitsJump;
    // p2_sprint_stand=1: a sprint press stands the player up for good, as the engine does for
    // player 1 (crouch or prone, sprint, and the player stays standing afterwards). Without
    // this the stance kept here would stay crouch / prone and the player would drop back into
    // it when the sprint ends.
    if ((pressed & ActBit(kActSprint)) && s.stance.stance != 0 && ReadIniInt(L"p2_sprint_stand", 1)) {
        s.stance.stance = 0;
        s.stance.held = false;
    }
    if (s.stance.stance == 1)
        b |= kBitsCrouch;
    else if (s.stance.stance == 2)
        b |= kBitsProne;
    else if (s.stance.stance == 3) {
        b |= kBitsProne;
        b2 |= kBits2Dive;
    }

    // Action slots: the engine's handler runs in the player's next pass (p2_actionslot=1) and
    // its bits come back through the CL_AddUserCmdButtons hook; p2_actionslot=2 sets the
    // slot bit here for this one usercmd and selects nothing.
    for (int n = 0; n < 4; ++n) {
        if (!(pressed & ActBit(kActSlot1 + n))) continue;
        if (c.actionSlot == 1) {
            InterlockedOr(&s.slotPending, 1 << n);
            InterlockedExchange(&s.weaponChange, 1);
        } else if (c.actionSlot == 2)
            b2 |= kBits2Slot1 >> n;
    }
    s.scoresDown = (acts & ActBit(kActScores)) != 0;
    if ((pressed & ActBit(kActMenu)) && c.startMenu) MenuPressed(slot);

    b |= (int)InterlockedExchange(&s.cgBits[0], 0);
    b2 |= (int)InterlockedExchange(&s.cgBits[1], 0);

    {
        static int armed = -1;
        if (c.log != armed) {
            armed = c.log;
            g_logLeft = c.log;
        }
    } // p2_controls_log=N, re-armed when the value changes
    bool bitsMoved = b != s.lastB;
    s.lastB = b;
    if (g_logLeft > 0 &&
        (acts != s.prevActs || bitsMoved || (b2 & ~(kWeapNextPseudoBit | kWeapPrevPseudoBit | kBits2Stance)))) {
        --g_logLeft;
        Log("[ctl] player %d actions %05x -> buttons %08x / %08x, stance %d", slot + 2, acts, (unsigned)b, (unsigned)b2,
            s.stance.stance);
    }
    s.prevActs = acts;
    *buttons |= b;
    *buttons2 |= b2;
}

bool ShadowControlsTakeWeaponChange(int slot) {
    return slot >= 0 && slot <= 2 && InterlockedExchange(&g_ctl[slot].weaponChange, 0) != 0;
}

// Keyboard and mouse layout of a shadow player (fixed): W A S D move (shadow_client.cpp), mouse
// look, left button fire, right button aim, G lethal, Q tactical, F or R use / reload, space
// jump, C or left ctrl crouch (tap) / prone (hold), left shift sprint, V or E melee, 1 or
// middle button next weapon, 2 previous weapon, 3 4 5 6 action slots, tab scoreboard. Escape stays with the
// engine (pause menu).
unsigned ShadowControlsKbmActions() {
    static const struct {
        int vk;
        int action;
    } keys[] = {
        {VK_LBUTTON, kActAttack}, {VK_RBUTTON, kActAds}, {'G', kActLethal},
        {'Q', kActTactical},      {'F', kActUse},        {'R', kActUse},
        {VK_SPACE, kActJump},     {'C', kActStance},     {VK_LCONTROL, kActStance},
        {VK_LSHIFT, kActSprint},  {'V', kActMelee},      {'E', kActMelee},
        {'1', kActWeapNext},      {'2', kActWeapPrev},   {VK_MBUTTON, kActWeapNext},
        {'3', kActSlot1},         {'4', kActSlot2},      {'5', kActSlot3},
        {'6', kActSlot4},         {VK_TAB, kActScores},
    };
    unsigned acts = 0;
    for (const auto& k : keys)
        if (GetAsyncKeyState(k.vk) & 0x8000) acts |= ActBit(k.action);
    return acts;
}

// Called in the second pass on player 2's cg block and playerState, next to the weapon
// cycle in HookCgPredict (predict.cpp).
void ShadowControlsSecondPass(uint8_t* cg) {
    const Config& c = Cfg();
    if (!cg || !*(void**)(cg + engine::kCgSnap)) return; // cg->snap, as the handlers check themselves
    PlayerCtl& s = g_ctl[CurSlot()];
    { // 0x0053fff0 on this player's playerState
        const uint8_t* ps = cg + engine::kCgPredictedPs;
        int minSpeed = engine::DvarIntValue(*(void**)engine::kDiveMinSpeedDvar);
        s.canDive = (*(const unsigned*)(ps + 0xc) & 0x8000) &&
                    *(const float*)(ps + engine::kPsViewHeightCurrent) >= (float)minSpeed;
    }
    LONG slots = InterlockedExchange(&s.slotPending, 0);
    for (int slot = 0; slot < 4 && c.actionSlot == 1; ++slot) {
        if (!(slots & (1 << slot))) continue;
        int before = *(const int*)(cg + engine::kCgWeaponSelect);
        engine::CG_ActionSlotDown(0, slot); // CG_ActionSlotDown(lc, slot)
        Log("[ctl] player %d action slot %d: type %d weapon %d, weaponSelect %d -> %d, cgame bits %08x / %08x",
            CurSlot() + 2, slot + 1, *(const int*)(cg + 0x485f8 + slot * 4), *(const int*)(cg + 0x48608 + slot * 4),
            before, *(const int*)(cg + engine::kCgWeaponSelect), *(const unsigned*)(cg + 0x80b3c),
            *(const unsigned*)(cg + 0x80b40));
    }
}

// The scoreboard button of a shadow player is held (player_menu.cpp shows the scoreboard).
bool ShadowControlsScoresHeld(int slot) {
    return slot >= 0 && slot <= 2 && g_ctl[slot].scoresDown;
}

// Start on the pad of a player 2 to 4 asks for the pause menu toggle; the menu code takes the
// request once per frame (ShadowMenuFrame) and carries it out.
void ShadowControlsPauseToggle() {
    InterlockedExchange(&g_menuPending, 1);
}
bool ShadowControlsTakePauseToggle() {
    return InterlockedExchange(&g_menuPending, 0) != 0;
}

// CL_AddUserCmdButtons 0x00547580(lc, int bits[2]): cl+0x2c80 |= bits[0], cl+0x2c84 |=
// bits[1]. Its one caller is CG_SubmitInputAndView (0x004b207b) with cg+0x80b3c. In the
// second pass the bits are player 2's (action slots, the weapon cycle's 0x4000): kept for
// player 2's next usercmd, not written into player 1's cl.
typedef void(__cdecl* AddUserCmdButtons_t)(int, const int*);
static AddUserCmdButtons_t oAddUserCmdButtons;
static void __cdecl HookAddUserCmdButtons(int lc, const int* bits) {
    if (g_cgSwapped && bits && Cfg().cgButtons) {
        PlayerCtl& s = g_ctl[CurSlot()];
        if (bits[0]) InterlockedOr(&s.cgBits[0], bits[0]);
        if (bits[1]) InterlockedOr(&s.cgBits[1], bits[1]);
        return;
    }
    oAddUserCmdButtons(lc, bits);
}
static const uint8_t kProAddUserCmdButtons[] = {0x8b, 0x4c, 0x24, 0x04, 0x33, 0xc0};

// CL_SetStance 0x00540150(lc, stance): cl+0x2bd0 = stance (0 stand, 1 crouch, 2 prone). In a
// shadow player's pass the stance is that player's: it goes into the state of this file.
typedef void(__cdecl* SetStance_t)(int, int);
static SetStance_t oSetStance;
static void __cdecl HookSetStance(int lc, int stance) {
    if (g_cgSwapped && InShadowPass() && Cfg().stanceEvents) {
        Stance& st = g_ctl[CurSlot()].stance;
        if (st.stance != stance)
            Log("[ctl] player %d stance %d -> %d (set by the game)", CurSlot() + 2, st.stance, stance);
        st.stance = stance;
        st.held = false;
        return;
    }
    oSetStance(lc, stance);
}
static const uint8_t kProSetStance[] = {0x8b, 0x4c, 0x24, 0x04, 0x8b, 0xc1};

// UI_GetKeyBindingLocalizedString 0x0071e420: writes the key name(s) bound to a command into
// out (0x100 bytes) and returns how many keys it found (0 = "KEY_UNBOUND", 2 = "a or b").
// p2_hint_pad: 1 = in the second pass, while player 2's cg block is in, a command player 2
// has on the pad gets that button's glyph; 2 = the same for the whole second pass, and a
// command this file does not know is looked up by the engine as if a gamepad were in use
// (the engine's own pad bindings, which may be empty on this build); 0 = engine.
// p2_hint_trace=N logs the next N lookups of the second pass.
typedef int(__cdecl* GetKeyBindingString_t)(int, const char*, char*, int, int, int, int, int, int, int);
static GetKeyBindingString_t oGetKeyBindingString;
static int __cdecl HookGetKeyBindingString(int lc, const char* command, char* out, int bindNum, int a5, int a6, int a7,
                                           int a8, int a9, int tryAlternate) {
    const Config& c = Cfg();
    if (!c.hintPad || !InShadowPass() || !command || !out || (c.hintPad == 1 && !g_cgSwapped) ||
        PlayerInputDevice(CurSlot() + 1) == kDevKbm)
        return oGetKeyBindingString(lc, command, out, bindNum, a5, a6, a7, a8, a9, tryAlternate);
    {
        static int armed = -1;
        if (c.hintTrace != armed) {
            armed = c.hintTrace;
            g_hintLogLeft = c.hintTrace;
        }
    }
    int action = ActionForCommand(command);
    const int* layout = ButtonLayout(ShadowPlayerSettings(CurSlot()).buttons);
    for (int i = 0; i < kInputCount && action != kActNone; ++i) {
        if (c.map[layout[i]] != action) continue;
        const char* glyph = PadGlyph(kInputs[i].engineKey);
        if (!glyph) break;
        strncpy_s(out, 0x100, glyph, _TRUNCATE);
        if (g_hintLogLeft > 0) {
            --g_hintLogLeft;
            Log("[ctl] hint %s -> pad %s (%s)", command, kInputs[i].name, glyph);
        }
        return 1;
    }
    int result;
    if (c.hintPad >= 2) {
        int* source = (int*)(engine::kCtrlMap + 0x14); // last input of local client 0: 0 = gamepad
        int saved = *source;
        *source = 0;
        result = oGetKeyBindingString(lc, command, out, bindNum, a5, a6, a7, a8, a9, tryAlternate);
        *source = saved;
    } else {
        result = oGetKeyBindingString(lc, command, out, bindNum, a5, a6, a7, a8, a9, tryAlternate);
    }
    if (g_hintLogLeft > 0) {
        --g_hintLogLeft;
        Log("[ctl] hint %s -> engine: %d key(s), '%s'", command, result, out);
    }
    return result;
}
static const uint8_t kProGetKeyBindingString[] = {0x81, 0xec, 0x04, 0x01, 0x00, 0x00};

// Com_LocalClient_LastInput_Get 0x006be7e0(lc): the dword 0x02480704 + lc * 0x18 (0 =
// gamepad, 1 / 2 = mouse, 3 = keyboard). GPad_InUse 0x0077c180 (50 callers) is "== 0" of it;
// the Lua side asks it too (0x00930186, 0x00931c8a, 0x00931d2b, 0x0094f506).
// p2_hint_gamepad_state=1 (diagnostic, default 0): the whole second pass sees a gamepad, so
// whatever else depends on the input source (Lua HUD prompts, aim assist paths of the
// cgame) follows.
typedef int(__cdecl* LastInputGet_t)(int);
static LastInputGet_t oLastInputGet;
static int __cdecl HookLastInputGet(int lc) {
    if (lc == 0 && InShadowPass() && g_cgSwapped && Cfg().gamepadState) return 0;
    if (lc == 0 && ShadowMenuHandling()) return 0; // the player's own menu: button prompts of a gamepad
    return oLastInputGet(lc);
}
static const uint8_t kProLastInputGet[] = {0x8b, 0x44, 0x24, 0x04, 0x8d, 0x04, 0x40};

const Detour kControlsHooks[] = {
    {"CL_AddUserCmdButtons (player 2's cgame button bits kept for its own usercmd)", 0x00547580, kProAddUserCmdButtons,
     sizeof(kProAddUserCmdButtons), (void*)HookAddUserCmdButtons, (void**)&oAddUserCmdButtons},
    {"UI_GetKeyBindingLocalizedString (pad glyphs in player 2's hints)", 0x0071e420, kProGetKeyBindingString,
     sizeof(kProGetKeyBindingString), (void*)HookGetKeyBindingString, (void**)&oGetKeyBindingString},
    {"CL_SetStance (a shadow player's stance events go to its own stance)", 0x00540150, kProSetStance,
     sizeof(kProSetStance), (void*)HookSetStance, (void**)&oSetStance},
    {"Com_LocalClient_LastInput_Get (gamepad in the second pass, p2_hint_gamepad_state)", 0x006be7e0, kProLastInputGet,
     sizeof(kProLastInputGet), (void*)HookLastInputGet, (void**)&oLastInputGet},
};
