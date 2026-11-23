#include "core/common.h"
#include "shadow/shadow.h"
#include "core/dev.h"
#include "engine/client.h"

// Input device per player (ini keys p1_pad .. p4_pad, read every frame):
//   -3 = automatic (player 1 only), -2 = keyboard and mouse, -1 = nothing, 0..3 = that XInput pad.
// Defaults: player 1 automatic, players 2 / 3 / 4 the pads 1 / 2 / 3.
// Automatic: player 1 plays on the first connected pad that no other player has, and keeps it
// while it stays connected; with no such pad player 1 plays on keyboard and mouse.
// A device goes to the lowest player that asks for it; a higher player with the same value
// gets nothing. With no player on keyboard and mouse they stay with player 1.
// p1_pad_only=1: while player 1 plays on a connected pad, keyboard and mouse do nothing in the
// match and still work in every menu (the same three hooks as below).
//
// Player 1 is the engine's own local client, so its device is switched on the engine's side:
//   pad:  the engine reads XInputGetState(0) only (UpdateGamepadStatePad0 0x0077c820); the
//         proxy export answers that call with the state of player 1's pad, or "not connected"
//         when player 1 has none (and GPad_IsActive(0) reports inactive then).
//   keyboard and mouse given to a shadow player: while the match runs with no menu open
//         (key catchers 0x010e7e14 == 0) the three hooks below keep them away from player 1:
//         CL_KeyEvent 0x0054e9d0(lc, key, down, time) drops the event (keyboard keys and the
//         mouse buttons 200..; pad keys take another path, 0x0054b740), CL_MouseEvent
//         0x00542ba0(lc, x, y, dx, dy) gets dx = dy = 0 and the movement is kept here for the
//         shadow player (the engine still recentres the cursor), and the input source setter
//         0x006be700(lc, source) ignores "mouse" / "keyboard", so player 1's hints stay on
//         the pad glyphs. Escape and the console keys always reach the engine.
//   The shadow player's usercmd is then built from GetAsyncKeyState and the kept mouse
//   movement (shadow_client.cpp: ReadPlayerInput) with the fixed key layout in controls.cpp.

namespace {

volatile LONG g_mouseDx, g_mouseDy;
bool g_swallowed[256];

bool PlayerPresent(int player) {
    if (player == 0) return true;
    if (player == 1) return g_p2.started;
    return ExtraCl(player - 2) != nullptr;
}

bool SplitInput() {
    return g_p2.enabled && ReadIniInt(L"p2_input", 1) != 0;
}

// Player 1 is in the match with no menu, console or chat open. Taken once per frame in
// player 1's context (PInputFrame): the usercmd of a shadow player is built while that
// player's client state is switched in, where the connection state reads 9, not 10.
volatile bool g_p1Playing;

// Player 1's pad, looked up twice a second (a poll of a pad that is not connected is slow):
// the pad the automatic choice holds, and whether the pad player 1 plays on is connected.
int g_p1AutoPad = -1;
bool g_p1PadThere;

bool PadConnected(int pad) {
    uint8_t state[16];
    return pad >= 0 && pad <= 3 && pGetState && pGetState((DWORD)pad, state) == ERROR_SUCCESS;
}

bool PadOfAnotherPlayer(int pad) {
    for (int p = 1; p < 4; ++p)
        if ((LobbyJoinOn() || PlayerPresent(p)) && PlayerDevice(p) == pad)
            return true; // a pad named for a player
                         // that is not there is free
    return false;
}

void UpdatePlayer1Pad() {
    int want = ReadIniInt(L"p1_pad", kDevAuto);
    if (want != kDevAuto) {
        g_p1AutoPad = -1;
        g_p1PadThere = PadConnected(want);
        return;
    }
    if (!(PadConnected(g_p1AutoPad) && !PadOfAnotherPlayer(g_p1AutoPad))) {
        int found = -1;
        for (int pad = 0; pad < 4 && found < 0; ++pad)
            if (!PadOfAnotherPlayer(pad) && PadConnected(pad)) found = pad;
        g_p1AutoPad = found;
    }
    g_p1PadThere = g_p1AutoPad >= 0;
}

// Player 1's keyboard and mouse do nothing for player 1 while the match runs without a menu:
// a shadow player has them, or player 1 plays on a pad.
bool KbmTaken() {
    if (!g_p1Playing || !SplitInput()) return false;
    if (KbmOwner() >= 1) return true;
    return g_p1PadThere && PlayerDevice(0) >= 0 && ReadIniInt(L"p1_pad_only", 1);
}

} // namespace

int PlayerDevice(int player) {
    static const wchar_t* keys[4] = {L"p1_pad", L"p2_pad", L"p3_pad", L"p4_pad"};
    static const int defaults[4] = {kDevAuto, 1, 2, 3};
    if (player < 0 || player > 3) return kDevNone;
    if (player >= 1 && LobbyJoinOn()) return LobbyPad(player); // joined in the lobby, or nothing
    int d = ReadIniInt(keys[player], defaults[player]);
    if (player == 0 && d == kDevAuto) return g_p1AutoPad >= 0 ? g_p1AutoPad : kDevKbm;
    return (d < kDevKbm || d > 3) ? kDevNone : d;
}

int KbmOwner() {
    for (int p = 0; p < 4; ++p)
        if (PlayerPresent(p) && PlayerDevice(p) == kDevKbm) return p;
    return 0;
}

int PlayerInputDevice(int player) {
    int d = PlayerDevice(player);
    if (d == kDevKbm) return KbmOwner() == player ? kDevKbm : kDevNone;
    if (d < 0) return kDevNone;
    for (int p = 0; p < player; ++p)
        if (PlayerPresent(p) && PlayerDevice(p) == d) return kDevNone;
    return d;
}

int P1EnginePad() {
    if (!SplitInput()) return 0; // no split input: the engine sees pad 0 as it is
    int d = PlayerDevice(0);
    return d >= 0 ? d : -1;
}

bool KbmRead(int player, KbmState* k) {
    *k = {};
    if (player < 1 || KbmOwner() != player) return false;
    LONG dx = InterlockedExchange(&g_mouseDx, 0), dy = InterlockedExchange(&g_mouseDy, 0);
    if (ReadIniInt(L"p_input_log", 0)) { // diagnostic: why keyboard and mouse do or do not reach the shadow player
        static DWORD next;
        DWORD now = GetTickCount();
        if ((LONG)(now - next) >= 0) {
            next = now + 1000;
            Log("[input] kbm player %d: player 1 playing %d, key catchers %08x, ui state %u, foreground %p / game "
                "window %p, W %d, mouse %ld %ld",
                player + 1, (int)g_p1Playing, *(const uint32_t*)engine::kKeyCatchers,
                *(const uint32_t*)(engine::kUiState + 8), (void*)GetForegroundWindow(),
                (void*)*(HWND*)engine::kWindowHandle, (GetAsyncKeyState('W') & 0x8000) != 0, dx, dy);
        }
    }
    if (!KbmTaken() || GetForegroundWindow() != *(HWND*)engine::kWindowHandle) return false;
    auto down = [](int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };
    k->fwd = (down('W') ? 1.0f : 0.0f) - (down('S') ? 1.0f : 0.0f);
    k->right = (down('D') ? 1.0f : 0.0f) - (down('A') ? 1.0f : 0.0f);
    k->dx = (int)dx;
    k->dy = (int)dy;
    k->ads = down(VK_RBUTTON);
    k->acts = ShadowControlsKbmActions();
    return true;
}

// Once a second: log the assignment when it changed.
void PInputFrame() {
    static DWORD next;
    static int last[4] = {-9, -9, -9, -9};
    static int lastOwner = -9;
    g_p1Playing = *(const uint32_t*)engine::kKeyCatchers == 0 && *(const uint32_t*)(engine::kUiState + 8) == 10;
    DWORD now = GetTickCount();
    static DWORD nextPad;
    if ((LONG)(now - nextPad) >= 0) {
        nextPad = now + 500;
        UpdatePlayer1Pad();
    }
    if ((LONG)(now - next) < 0) return;
    next = now + 1000;
    if (!SplitInput()) return;
    int dev[4];
    bool changed = false;
    for (int p = 0; p < 4; ++p) {
        dev[p] = PlayerPresent(p) ? PlayerInputDevice(p) : -8;
        if (dev[p] != last[p]) changed = true;
    }
    int owner = KbmOwner();
    if (!changed && owner == lastOwner) return;
    lastOwner = owner;
    char line[200];
    int n = 0;
    for (int p = 0; p < 4; ++p) {
        last[p] = dev[p];
        if (dev[p] == -8) continue;
        if (dev[p] == kDevKbm)
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " player %d keyboard+mouse,", p + 1);
        else if (dev[p] < 0)
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " player %d nothing,", p + 1);
        else {
            uint8_t st[16];
            bool on = pGetState && pGetState((DWORD)dev[p], st) == ERROR_SUCCESS;
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " player %d pad %d (%s),", p + 1, dev[p],
                             on ? "connected" : "not connected");
        }
    }
    if (n > 0) line[n - 1] = 0;
    Log("[input] devices:%s; keyboard+mouse owner: player %d", line, owner + 1);
}

typedef void(__cdecl* KeyEvent_t)(int, int, int, int);
static KeyEvent_t oKeyEvent;
static void __cdecl HookKeyEvent(int lc, int key, int down, int time) {
    if (lc == 0 && key >= 0 && key < 256) {
        if (down) {
            if (key != 0x1b && key != 0x60 && key != 0x7e && KbmTaken()) {
                g_swallowed[key] = true;
                return;
            }
        } else if (g_swallowed[key]) {
            g_swallowed[key] = false;
            if (!*(const int*)(engine::kKeyStates + key * 0x10)) return; // the engine never saw it go down
        }
    }
    oKeyEvent(lc, key, down, time);
}
static const uint8_t kProKeyEvent[] = {0x53, 0x8b, 0x5c, 0x24, 0x10};

typedef int(__cdecl* MouseEvent_t)(int, int, int, int, int);
static MouseEvent_t oMouseEvent;
static int __cdecl HookMouseEvent(int lc, int x, int y, int dx, int dy) {
    if (lc == 0 && (dx || dy) && KbmTaken()) {
        if (KbmOwner() >= 1) {
            InterlockedExchangeAdd(&g_mouseDx, dx);
            InterlockedExchangeAdd(&g_mouseDy, dy);
        }
        dx = 0;
        dy = 0;
    }
    return oMouseEvent(lc, x, y, dx, dy);
}
static const uint8_t kProMouseEvent[] = {0x83, 0x3d, 0xcc, 0x9c, 0x50, 0x03, 0x00};

typedef void(__cdecl* LastInputSet_t)(int, int);
static LastInputSet_t oLastInputSet;
static void __cdecl HookLastInputSet(int lc, int source) {
    if (lc == 0 && source != 0 && KbmTaken()) return;
    oLastInputSet(lc, source);
}
static const uint8_t kProLastInputSet[] = {0x53, 0x8b, 0x5c, 0x24, 0x08};

const Detour kInputHooks[3] = {
    {"CL_KeyEvent (keyboard and mouse buttons of a shadow player stay away from player 1)", 0x0054e9d0, kProKeyEvent,
     sizeof(kProKeyEvent), (void*)HookKeyEvent, (void**)&oKeyEvent},
    {"CL_MouseEvent (mouse movement for the shadow player that owns the mouse)", 0x00542ba0, kProMouseEvent,
     sizeof(kProMouseEvent), (void*)HookMouseEvent, (void**)&oMouseEvent},
    {"Com_LocalClient_LastInput_Set (player 1 stays on pad glyphs while a shadow player has the keyboard)", 0x006be700,
     kProLastInputSet, sizeof(kProLastInputSet), (void*)HookLastInputSet, (void**)&oLastInputSet},
};

// GPad_IsActive(pad) (0x0077c430) runs right after UpdateGamepadStatePad0 in the gamepad
// update. While player 1 has no pad (p1_pad < 0) the engine's pad 0 is reported inactive,
// so a pad of another player never moves player 1 as well.
volatile int g_padReportedInactive = -1;
constexpr uintptr_t kPadFunctionAfterIsActive = 0x0077d217; // return address of the pad function's GPad_IsActive call
typedef char(__cdecl* GpadIsActive_t)(int);
static GpadIsActive_t oGpadIsActive;
static char __cdecl HookGpadIsActive(int pad) {
    // The call from the pad function (0x0077d212) decides whether the sticks and the button
    // events of player 1's pad are worked off. In the pass of a shadow player they are not:
    // the commands of those events would run on that player's cgame block (see
    // HookGamepadUpdate, steam_offline.cpp).
    if (g_cgSwapped && _ReturnAddress() == (void*)kPadFunctionAfterIsActive) return 0;
    bool injected = DevPadInject(pad);
    char r = oGpadIsActive(pad);
    if (injected) return r;
    if (pad == g_padReportedInactive) return 0;
    return r;
}
static const uint8_t kProGpadIsActive[] = {0x8b, 0x44, 0x24, 0x04, 0x6b, 0xc0, 0x7c};
const Detour kGpadInjectHook = {"GPad_IsActive (pad exclusive to another player)",
                                0x0077c430,
                                kProGpadIsActive,
                                sizeof(kProGpadIsActive),
                                (void*)HookGpadIsActive,
                                (void**)&oGpadIsActive};
