#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/common.h"
#include "engine/offsets.h"
#include "engine/ui.h"

// The game's own controller options for players 2 to 4 (player_menu=1).
//
// Start on a shadow player's pad opens a menu of the game's Lua interface in that player's own
// LUI root (hud.cpp), so it shows inside the player's view only; the match is paused meanwhile (PauseUpdate). The
// HUD menu of every root handles the event "open_ingame_menu" (fields menuName, controller) by
// opening the named menu as a popup; the menu here is the controls options
// ("OptionsControlsMenu"), switched to its gamepad page: look inversion, vibration, stick
// layout, button layout, look sensitivity. The player's pad drives it with "gamepad_button"
// events (button = up, down, left, right, primary, secondary, ..., down = true / false).
//
// The menu stores its values in the player profile: Lua calls SetProfileVar(controller, name,
// value) 0x009376a0 -> 0x00672ed0(name, controller, value text) and reads them back through
// the profile getters (0x00671d10(index, controller) for an int, 0x00671d70 for a bool; the
// names of the 56 values are the table 0x00c8f0e0). The PC build keeps one profile: a block of
// 0x7d3 bytes at 0x023fbe48, indexed by controller * 0x7d3 in 84 places, with the block of a
// second controller lying on top of other data. So the menu of a shadow player is opened for
// controller 0, and while one of its events is handled the block holds that player's profile
// (a copy per player, swapped in and out around the event). Two things the engine does for
// controller 0 are kept away meanwhile: the layout change of a button or stick setting
// (0x00672b80, value index in EAX: it runs the layout's config file for player 1's pad), and
// UI_SetActiveMenu(0, 0), which the HUD calls when its menu closes (that call tells this file
// that the menu is gone).
//
// The values this file takes from the profile (sensitivity 0.4 to 4.0 with 1.0 as the
// default, inversion, button layout 0 to 8, stick layout 0 to 3) are kept per player in
// bo2ss_players.ini in the game folder and used by the pad code (shadow_client.cpp,
// controls.cpp).

namespace {

constexpr uintptr_t kProfile = 0x023fbe48; // profile block of controller 0
constexpr size_t kProfileSize = 0x7d3;
constexpr int kVarRumble = 1, kVarInvert = 3, kVarSensitivity = 4, kVarButtons = 11, kVarSticks = 12;

struct Slot {
    bool open;
    bool closeSeen;     // the menu closed itself while an event was handled
    DWORD closingSince; // Start was pressed: "back" is sent until the menu has closed (0 = not closing)
    DWORD closingNext;
    int row, page;    // focused row of the gamepad page (0 to 5), page 1 = a layout page is open
    DWORD skipAt;     // when to step from the first row to the second (0 = not pending)
    bool scoresShown; // the scoreboard of the player's HUD is open
    DWORD scoresMatch;
    bool profileReady; // profile holds this player's copy
    DWORD openedTick;
    DWORD match;      // g_p2.startedTick of the match the menu belongs to
    WORD prevButtons; // menu buttons held in the last frame (bits of kNav)
    DWORD repeatAt;   // next repeat of a held direction
    volatile LONG toggle;
    ShadowSettings settings;
    bool settingsLoaded;
    uint8_t profile[kProfileSize];
};
Slot g_slot[3];
int g_scope = -1; // slot whose menu handles an event right now
uint8_t g_profileSaved[kProfileSize];

int ProfileInt(int index) {
    __try {
        return ((int(__cdecl*)(int, int))0x00671d10)(index, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[menu] profile read %d failed: exception %08lx", index, GetExceptionCode());
        return 0;
    }
}
bool ProfileSet(int index, const char* value) {
    const char* name = *(const char* const*)(0x00c8f0e0 + index * 4);
    __try {
        return ((char(__cdecl*)(const char*, int, const char*))0x00672ed0)(name, 0, value) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[menu] profile write %s = %s failed: exception %08lx", name, value, GetExceptionCode());
        return false;
    }
}

void SettingsPath(wchar_t* path) {
    lstrcpynW(path, g_gameDir, MAX_PATH);
    lstrcatW(path, L"bo2ss_players.ini");
}
void LoadSettings(int slot) {
    Slot& s = g_slot[slot];
    if (s.settingsLoaded) return;
    wchar_t path[MAX_PATH], section[16];
    SettingsPath(path);
    _snwprintf_s(section, _TRUNCATE, L"player%d", slot + 2);
    ShadowSettings v = {};
    int sens = (int)GetPrivateProfileIntW(section, L"sensitivity", 100, path);
    if (sens < 10 || sens > 1000) sens = 100;
    v.sensitivity = sens * 0.01f;
    v.invert = GetPrivateProfileIntW(section, L"invert", 0, path) != 0 ? 1 : 0;
    v.buttons = (int)GetPrivateProfileIntW(section, L"buttons", 0, path);
    v.sticks = (int)GetPrivateProfileIntW(section, L"sticks", 0, path);
    v.rumble = GetPrivateProfileIntW(section, L"rumble", 1, path) != 0 ? 1 : 0;
    if (v.buttons < 0 || v.buttons > 8) v.buttons = 0;
    if (v.sticks < 0 || v.sticks > 3) v.sticks = 0;
    s.settings = v; // the pad code reads the values from another thread: complete before the flag
    s.settingsLoaded = true;
}
void SaveSettings(int slot) {
    const ShadowSettings& v = g_slot[slot].settings;
    wchar_t path[MAX_PATH], section[16], text[16];
    SettingsPath(path);
    _snwprintf_s(section, _TRUNCATE, L"player%d", slot + 2);
    _snwprintf_s(text, _TRUNCATE, L"%d", (int)(v.sensitivity * 100.0f + 0.5f));
    WritePrivateProfileStringW(section, L"sensitivity", text, path);
    WritePrivateProfileStringW(section, L"invert", v.invert ? L"1" : L"0", path);
    _snwprintf_s(text, _TRUNCATE, L"%d", v.buttons);
    WritePrivateProfileStringW(section, L"buttons", text, path);
    _snwprintf_s(text, _TRUNCATE, L"%d", v.sticks);
    WritePrivateProfileStringW(section, L"sticks", text, path);
    WritePrivateProfileStringW(section, L"rumble", v.rumble ? L"1" : L"0", path);
}

// The player's values as the profile block holds them right now (the block is swapped in).
ShadowSettings ReadProfile() {
    ShadowSettings v = {};
    int raw = ProfileInt(kVarSensitivity);
    memcpy(&v.sensitivity, &raw, 4);
    v.invert = ProfileInt(kVarInvert) != 0 ? 1 : 0;
    v.buttons = ProfileInt(kVarButtons);
    v.sticks = ProfileInt(kVarSticks);
    v.rumble = ProfileInt(kVarRumble) != 0 ? 1 : 0;
    return v;
}

// Between ScopeEnter and ScopeLeave the profile block of controller 0 is the slot's.
bool ScopeEnter(int slot) {
    if (g_scope >= 0) return false;
    Slot& p = g_slot[slot];
    memcpy(g_profileSaved, (const void*)kProfile, kProfileSize);
    g_scope = slot;
    if (!p.profileReady) {
        // First use: player 1's profile with this player's own values.
        p.profileReady = true;
        LoadSettings(slot);
        char text[32];
        sprintf_s(text, "%.2f", p.settings.sensitivity);
        ProfileSet(kVarSensitivity, text);
        ProfileSet(kVarInvert, p.settings.invert ? "1" : "0");
        sprintf_s(text, "%d", p.settings.buttons);
        ProfileSet(kVarButtons, text);
        sprintf_s(text, "%d", p.settings.sticks);
        ProfileSet(kVarSticks, text);
        ProfileSet(kVarRumble, p.settings.rumble ? "1" : "0");
        ShadowSettings v = ReadProfile();
        Log("[menu] player %d profile: sensitivity %.2f, invert %d, buttons %d, sticks %d, rumble %d", slot + 2,
            v.sensitivity, v.invert, v.buttons, v.sticks, v.rumble);
    } else
        memcpy((void*)kProfile, p.profile, kProfileSize);
    return true;
}
void ScopeLeave() {
    int slot = g_scope;
    if (slot < 0) return;
    Slot& p = g_slot[slot];
    ShadowSettings v = ReadProfile();
    const ShadowSettings& old = p.settings;
    bool changed = fabsf(v.sensitivity - old.sensitivity) > 0.005f || v.invert != old.invert ||
                   v.buttons != old.buttons || v.sticks != old.sticks || v.rumble != old.rumble;
    if (v.sensitivity >= 0.1f && v.sensitivity <= 10.0f && v.buttons >= 0 && v.buttons <= 8 && v.sticks >= 0 &&
        v.sticks <= 3 && changed) {
        Log("[menu] player %d set: sensitivity %.2f, invert %d, buttons %d, sticks %d, rumble %d", slot + 2,
            v.sensitivity, v.invert, v.buttons, v.sticks, v.rumble);
        p.settings = v;
        SaveSettings(slot);
    }
    memcpy(p.profile, (const void*)kProfile, kProfileSize);
    memcpy((void*)kProfile, g_profileSaved, kProfileSize);
    g_scope = -1;
}
struct Scope {
    bool entered;
    explicit Scope(int slot) : entered(ScopeEnter(slot)) {}
    ~Scope() {
        if (entered) ScopeLeave();
    }
};

bool SendEventGuarded(int slot, const char* name, const char* const* fields, int count) {
    __try {
        return ShadowHudEvent(slot + 1, name, 0, fields, count);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[menu] player %d event %s failed: exception %08lx", slot + 2, name, GetExceptionCode());
        return false;
    }
}
bool SendEvent(int slot, const char* name, const char* const* fields, int count) {
    Scope scope(slot);
    return SendEventGuarded(slot, name, fields, count);
}
bool SendButton(int slot, const char* button, bool down) {
    const char* fields[] = {"button", button, "down", down ? "true" : "false", "immediate", "true"};
    return SendEvent(slot, "gamepad_button", fields, 3);
}

// A button press of the player in its menu. The gamepad page has six rows and the focus wraps
// around; the first row, "enable gamepad", is the dvar of the whole game (its change is kept
// away by the hook of the hardware profile below), so the focus is stepped past it: the row
// the focus is on is counted here. A (primary) on the stick layout or button layout row opens
// that layout's page, B (secondary) there builds the gamepage page again with the focus on the
// first row.
constexpr int kRows = 6, kRowSticks = 3, kRowButtons = 4;
bool Press(int slot, const char* button) {
    Slot& s = g_slot[slot];
    bool up = strcmp(button, "up") == 0, down = strcmp(button, "down") == 0;
    bool back = strcmp(button, "secondary") == 0;
    if (s.page != 0) {
        bool sent = SendButton(slot, button, true);
        if (back) {
            s.page = 0;
            s.row = 0;
            s.skipAt = GetTickCount() + 150; // the page is built in the next tick of the root
        }
        return sent;
    }
    if (up || down) {
        bool sent = SendButton(slot, button, true);
        s.row = (s.row + (down ? 1 : kRows - 1)) % kRows;
        if (s.row == 0) {
            SendButton(slot, button, false);
            SendButton(slot, button, true);
            s.row = down ? 1 : kRows - 1;
        }
        return sent;
    }
    if (s.row == 0 && !back) return false;
    bool sent = SendButton(slot, button, true);
    if (strcmp(button, "primary") == 0 && (s.row == kRowSticks || s.row == kRowButtons)) s.page = 1;
    return sent;
}

// "Back" on the controls options leads to the options menu above them (graphics, sound: the
// settings of the whole game). A menu takes its handlers from the table CoD.OptionsControls
// when it is built, so while a shadow player's menu is open the entry Back is the entry
// CloseMenu (which closes the options and tells the HUD). The original stays in the table
// under another name: a function nothing refers to would be collected.
void BackClosesMenu(bool on) {
    unsigned long long back = 0, saved = 0, close = 0;
    if (!LuiTableGet("CoD", "OptionsControls", "Back", &back) ||
        !LuiTableGet("CoD", "OptionsControls", "BackOfTheMenu", &saved) ||
        !LuiTableGet("CoD", "OptionsControls", "CloseMenu", &close))
        return;
    if (on) {
        if ((uint32_t)saved == 0 && (uint32_t)back != 0 && (uint32_t)close != 0) {
            LuiTableSet("CoD", "OptionsControls", "BackOfTheMenu", back);
            LuiTableSet("CoD", "OptionsControls", "Back", close);
        }
    } else if ((uint32_t)saved != 0) {
        LuiTableSet("CoD", "OptionsControls", "Back", saved);
        LuiTableSet("CoD", "OptionsControls", "BackOfTheMenu", 0);
    }
}

void Close(int slot, const char* why) {
    Slot& s = g_slot[slot];
    if (!s.open) return;
    s.open = false;
    s.prevButtons = 0;
    s.closingSince = 0;
    // The HUD menu of the root stopped taking events when the popup covered it and starts again
    // on this event; a popup that goes back to its parent sends it, one that just closes does not.
    const char* fields[] = {"occluded", "false"};
    SendEvent(slot, "occlusion_change", fields, 1);
    Log("[menu] player %d menu closed (%s)", slot + 2, why);
    if (!g_slot[0].open && !g_slot[1].open && !g_slot[2].open) BackClosesMenu(false);
}

void Open(int slot) {
    Slot& s = g_slot[slot];
    const char* fields[] = {"menuName", "OptionsControlsMenu"};
    s.closeSeen = false;
    s.closingSince = 0;
    // The controls options open on the page they showed last (CoD.OptionsControls.CurrentTabIndex);
    // the gamepad page is the fifth.
    float tabNumber = 5.0f;
    unsigned long long tabValue = 3; // a number
    memcpy((uint8_t*)&tabValue + 4, &tabNumber, 4);
    if (!LuiTableSet("CoD", "OptionsControls", "CurrentTabIndex", tabValue))
        Log("[menu] player %d: the options page could not be chosen", slot + 2);
    BackClosesMenu(true);
    if (!SendEvent(slot, "open_ingame_menu", fields, 1)) {
        Log("[menu] player %d: no HUD root, menu not opened", slot + 2);
        return;
    }
    s.open = true;
    s.row = s.page = 0;
    s.skipAt = GetTickCount() + 150; // the page is built in the next tick of the root
    s.openedTick = GetTickCount();
    s.match = g_p2.startedTick;
    s.prevButtons = 0xffff; // no press from a button that is still held
    Log("[menu] player %d menu opened", slot + 2);
}

// The match is paused while the menu of a shadow player is open, as player 1's
// pause menu pauses it: the dvar cl_paused is 1 (UI_SetActiveMenu sets it for menu 3 and clears
// it for menu 0). Player 1 closing its own pause menu clears the dvar, so it is set again in
// every frame a menu is open; when the last menu closes it is cleared unless player 1's pause
// menu is up.
//
// The client's server time is its clock plus a delta, computed per frame while the match is not
// paused (0x00546e30), and the clock moves on during a pause. The engine's own pause marks the
// client (0x00546df0), and the first frame after the pause then sets the time to that of the
// newest snapshot: a step of up to one server frame (50 ms) in one picture, seen as a short
// stutter. Here the delta is taken back by the clock's advance in every paused frame instead
// so the time goes on from where it stood. The mark is used only where
// that cannot hold: the dvar was cleared by someone else while a menu was open, so a frame may
// have run with the clock ahead.
bool g_pauseHeld;
int g_pauseClock; // the client clock up to which the pause is taken out of the delta
void PauseUpdate() {
    void* paused = *(void**)engine::kClPausedDvar;
    if (!paused) return;
    uint8_t* cl = *(uint8_t**)engine::kClPtr;
    bool want = cl && g_p2.started && (g_slot[0].open || g_slot[1].open || g_slot[2].open);
    if (!want && !g_pauseHeld) return;
    int clock = *(const int*)engine::kClClock;
    bool running = engine::DvarIntValue(paused) == 0;
    if (g_pauseHeld && !running && cl) *(int*)(cl + engine::kClServerTimeDelta) -= clock - g_pauseClock;
    g_pauseClock = clock;
    if (want) {
        if (running) {
            engine::DvarSetInt(paused, 1);
            if (g_pauseHeld) ((void(__cdecl*)())engine::kClResyncAfterPause)();
            if (!g_pauseHeld) Log("[menu] match paused for a player's menu");
        }
        g_pauseHeld = true;
    } else {
        g_pauseHeld = false;
        if (*(const int*)engine::kActiveMenu != 3) {
            engine::DvarSetInt(paused, 0);
            Log("[menu] match resumed");
        }
    }
}

// Pad buttons that drive the menu: XInput mask and the LUI button name.
const struct {
    WORD mask;
    const char* name;
    bool repeat;
} kNav[] = {
    {0x0001, "up", true},    {0x0002, "down", true},     {0x0004, "left", true},
    {0x0008, "right", true}, {0x1000, "primary", false}, {0x2000, "secondary", false},
};

} // namespace

// Around the tick of a shadow player's root while its menu is open: the menu is built and its
// timers run there, with the player's profile in place.
bool ShadowMenuTickBegin(int slot) {
    return ShadowMenuOpen(slot) && ScopeEnter(slot);
}
void ShadowMenuTickEnd() {
    ScopeLeave();
}
bool ShadowScoresOpen(int slot) {
    return slot >= 0 && slot <= 2 && g_slot[slot].scoresShown;
}
bool ShadowMenuHandling() {
    return g_scope >= 0;
}
bool ShadowMenuOpen(int slot) {
    return slot >= 0 && slot <= 2 && g_slot[slot].open;
}
const ShadowSettings& ShadowPlayerSettings(int slot) {
    static const ShadowSettings kDefault = {1.0f, 0, 0, 0, 1};
    if (slot < 0 || slot > 2) return kDefault;
    LoadSettings(slot);
    return g_slot[slot].settings;
}
void ShadowMenuToggle(int slot) {
    if (slot >= 0 && slot <= 2) InterlockedExchange(&g_slot[slot].toggle, 1);
}
// A menu button from a script: press and release.
bool ShadowMenuButton(int slot, const char* button) {
    if (!ShadowMenuOpen(slot)) return false;
    bool sent = Press(slot, button);
    SendButton(slot, button, false);
    if (g_slot[slot].closeSeen) Close(slot, "closed by the menu");
    return sent;
}

// Once per frame after the view passes (from the hook of hud.cpp that draws the menus), with
// player 1's cgame block in place.
void ShadowMenuFrame() {
    DWORD now = GetTickCount();
    for (int slot = 0; slot < 3; ++slot) {
        Slot& s = g_slot[slot];
        if (s.open && (!g_p2.started || s.match != g_p2.startedTick)) Close(slot, "match over");
        if (s.open && s.closeSeen) Close(slot, "closed by the menu");
        if (!g_p2.started) InterlockedExchange(&s.toggle, 0);
        if (!g_p2.started || g_p2.inContext) continue;
        bool toggle = InterlockedExchange(&s.toggle, 0) != 0;
        if (toggle) {
            if (!s.open) {
                if (ReadIniInt(L"player_menu", 1)) Open(slot);
                continue;
            }
            if (!s.closingSince) {
                s.closingSince = now ? now : 1;
                s.closingNext = now;
            }
        }
        // The scoreboard while its button is held (player_scores=1). The HUD menu of every root
        // opens its scoreboard on the event "open_scoreboard_menu" and closes it on
        // "close_scoreboard_menu"; the engine sends them to player 1's root for its own button.
        // Sent here to the player's root alone, nothing changes for the others.
        {
            if (s.scoresMatch != g_p2.startedTick) {
                s.scoresMatch = g_p2.startedTick;
                s.scoresShown = false;
            }
            bool want = !s.open && ShadowControlsScoresHeld(slot) && ReadIniInt(L"player_scores", 1) != 0;
            if (want != s.scoresShown &&
                ShadowHudEvent(slot + 1, want ? "open_scoreboard_menu" : "close_scoreboard_menu", slot + 1, nullptr,
                               0)) {
                s.scoresShown = want;
                static int logLeft = 6;
                if (logLeft > 0 && logLeft--) Log("[menu] player %d scoreboard %s", slot + 2, want ? "open" : "closed");
            }
        }
        if (!s.open) continue;
        // Start closes the menu the way B does: "back" until the menu is gone. From a layout page
        // that takes two steps, and the page in between is built in the tick after the first.
        if (s.closingSince) {
            if ((LONG)(now - s.closingNext) >= 0) {
                s.closingNext = now + 150;
                SendButton(slot, "secondary", true);
                SendButton(slot, "secondary", false);
            }
            if (s.closeSeen)
                Close(slot, "start button");
            else if (now - s.closingSince > 2000)
                Close(slot, "start button, the menu did not answer");
            continue;
        }
        if (s.skipAt && (LONG)(now - s.skipAt) >= 0) {
            s.skipAt = 0;
            if (s.page == 0 && s.row == 0) {
                SendButton(slot, "down", true);
                SendButton(slot, "down", false);
                s.row = 1;
            }
        }
        // The player's pad: d-pad and left stick move, A confirms, B goes back.
        struct PadState {
            DWORD packet;
            WORD buttons;
            BYTE lt, rt;
            SHORT lx, ly, rx, ry;
        } st = {};
        int dev = PlayerInputDevice(slot + 1);
        WORD held = 0;
        if (dev >= 0 && pGetState && pGetState((DWORD)dev, &st) == ERROR_SUCCESS) {
            held = st.buttons;
            if (st.ly > 16000) held |= 0x0001;
            if (st.ly < -16000) held |= 0x0002;
            if (st.lx < -16000) held |= 0x0004;
            if (st.lx > 16000) held |= 0x0008;
        }
        WORD before = s.prevButtons;
        s.prevButtons = held;
        for (const auto& n : kNav) {
            bool down = (held & n.mask) != 0, was = (before & n.mask) != 0;
            if (down && !was) {
                Press(slot, n.name);
                if (n.repeat) s.repeatAt = now + 400;
            } else if (down && was && n.repeat && (LONG)(now - s.repeatAt) >= 0 && before != 0xffff) {
                Press(slot, n.name);
                s.repeatAt = now + 130;
            } else if (!down && was && before != 0xffff)
                SendButton(slot, n.name, false);
            if (s.closeSeen) break;
        }
        if (s.closeSeen) Close(slot, "closed by the menu");
    }
    PauseUpdate();
}

// UI_SetActiveMenu 0x0070d370(lc, menuId). While a shadow player's menu handles an event the
// call is that menu's: nothing changes for player 1, and menu 0 means the menu closed itself.
typedef int(__cdecl* SetActiveMenu_t)(int, int);
static SetActiveMenu_t oSetActiveMenu;
static int __cdecl HookSetActiveMenu(int lc, int menu) {
    if (g_scope >= 0) {
        Log("[menu] player %d: the menu asks for UI menu %d", g_scope + 2, menu);
        return 0;
    }
    return oSetActiveMenu(lc, menu);
}
static const uint8_t kProSetActiveMenu[] = {0x51, 0x53, 0x55, 0x56, 0x57};

// 0x00672b80 (EAX = profile value index, controller on the stack): applies a changed button or
// stick layout to the pad of the controller's local client. Skipped while the profile block is
// a shadow player's.
static void* oProfileApply;
static __declspec(naked) void HookProfileApply() {
    __asm {
        cmp dword ptr [g_scope], 0
        jl original
        ret
    original:
        jmp dword ptr [oProfileApply]
    }
}
static const uint8_t kProProfileApply[] = {0x83, 0xc0, 0xf7, 0x57, 0x8b, 0x7c, 0x24, 0x08};

// 0x0077cb90(name, value): sets a value of the hardware profile (the page's first row, "enable
// gamepad", is one: the dvar of the whole game). Not from a shadow player's menu.
typedef void(__cdecl* SetHardwareValue_t)(const char*, const char*);
static SetHardwareValue_t oSetHardwareValue;
static void __cdecl HookSetHardwareValue(const char* name, const char* value) {
    if (g_scope >= 0) {
        if (name && strcmp(name, "gpad_enabled") == 0)
            Log("[menu] player %d: hardware setting %s left alone", g_scope + 2, name);
        return;
    }
    oSetHardwareValue(name, value);
}
static const uint8_t kProSetHardwareValue[] = {0x83, 0xec, 0x18, 0x80, 0x3d, 0xa0, 0x95, 0xa8, 0x02, 0x00};

// The Lua functions Engine.LockInput(controller, on) 0x00938460, Engine.SetUIActive(controller,
// on) 0x00938510 and Engine.BlurWorld(controller, ...) 0x00938580: the menu code calls them when
// the first menu of a controller opens and the last one closes (input of that controller to the
// menus, HUD away, world blurred). From a shadow player's menu they would do that to player 1,
// so they do nothing then.
typedef int(__cdecl* LuaFunction_t)(uint8_t*);
static LuaFunction_t oLockInput, oSetUIActive, oBlurWorld;
static int __cdecl HookLockInput(uint8_t* L) {
    return g_scope >= 0 ? 0 : oLockInput(L);
}
static int __cdecl HookSetUIActive(uint8_t* L) {
    return g_scope >= 0 ? 0 : oSetUIActive(L);
}
static int __cdecl HookBlurWorld(uint8_t* L) {
    return g_scope >= 0 ? 0 : oBlurWorld(L);
}
static const uint8_t kProLuaFunction[] = {0x56, 0x8b, 0x74, 0x24, 0x08};

// The hardware profile is saved (0x0077ccd0) and applied (0x0077cd00) when the options close:
// CoD.Options.CloseMenu does both and then closes the menu. From a shadow player's menu neither
// happens, and the save tells this file that the menu is gone.
typedef void(__cdecl* Plain_t)();
static Plain_t oSaveHardware, oApplyHardware;
static void __cdecl HookSaveHardware() {
    if (g_scope >= 0) {
        g_slot[g_scope].closeSeen = true;
        return;
    }
    oSaveHardware();
}
static void __cdecl HookApplyHardware() {
    if (g_scope < 0) oApplyHardware();
}
static const uint8_t kProSaveHardware[] = {0x80, 0x3d, 0xa1, 0x95, 0xa8, 0x02, 0x00};
static const uint8_t kProApplyHardware[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xc0};

extern const Detour kPlayerMenuHooks[8] = {
    {"UI_SetActiveMenu (a shadow player's menu does not touch player 1's)", 0x0070d370, kProSetActiveMenu,
     sizeof(kProSetActiveMenu), (void*)HookSetActiveMenu, (void**)&oSetActiveMenu},
    {"profile value applied (no layout change for player 1 from a shadow player's menu)", 0x00672b80, kProProfileApply,
     sizeof(kProProfileApply), (void*)HookProfileApply, &oProfileApply},
    {"hardware profile value (not set from a shadow player's menu)", 0x0077cb90, kProSetHardwareValue,
     sizeof(kProSetHardwareValue), (void*)HookSetHardwareValue, (void**)&oSetHardwareValue},
    {"Engine.LockInput (not from a shadow player's menu)", 0x00938460, kProLuaFunction, sizeof(kProLuaFunction),
     (void*)HookLockInput, (void**)&oLockInput},
    {"Engine.SetUIActive (not from a shadow player's menu)", 0x00938510, kProLuaFunction, sizeof(kProLuaFunction),
     (void*)HookSetUIActive, (void**)&oSetUIActive},
    {"Engine.BlurWorld (not from a shadow player's menu)", 0x00938580, kProLuaFunction, sizeof(kProLuaFunction),
     (void*)HookBlurWorld, (void**)&oBlurWorld},
    {"hardware profile save (a shadow player's menu closes)", 0x0077ccd0, kProSaveHardware, sizeof(kProSaveHardware),
     (void*)HookSaveHardware, (void**)&oSaveHardware},
    {"hardware profile apply (not from a shadow player's menu)", 0x0077cd00, kProApplyHardware,
     sizeof(kProApplyHardware), (void*)HookApplyHardware, (void**)&oApplyHardware},
};
