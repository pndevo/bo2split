#include "core/common.h"
#include "shadow/shadow.h"
#include "core/dev.h"
#include "engine/common.h"
#include "engine/client.h"
#include "engine/ui.h"

// Players join in the lobby (lobby_join=1).
//
// While the local lobby is open the DLL reads the pads itself: A on a pad that no player
// has joins the next player (2, 3, 4), B on a joined pad leaves. The list of joined pads
// replaces extra_players and p2_pad .. p4_pad: PlayerDevice and the shadow client counts ask here.
// Player 1 keeps the device of p1_pad (by default the first connected pad); its pad cannot join.
//
// Each joined player is shown by the game's own lobby: a party member is written into the
// party (0x011c7550, members from +0x208, 0x148 bytes each: status +0 with 3 = there, XUID
// +0x38, name +0x40), a copy of the host's member with the name and the XUID the shadow
// client connects with (host name + " n", host XUID + n). The lobby then lists the rows and
// counts the players (0x005600c0), and getnumexpectedplayers gets the right number from the
// engine. The lobby builds its list again on the LUI event party_joined (gamelobby_update,
// partylobby_update and party_statechanged do not).

namespace {

constexpr uintptr_t kMembers = engine::kParty + 0x208;
constexpr size_t kMemberSize = 0x148;
constexpr WORD kPadA = 0x1000, kPadB = 0x2000;

int g_joinPad[3] = {-1, -1, -1}; // pad of players 2, 3, 4
int g_joined;                    // how many of them
int g_written;                   // member slots 1..n this code filled
WORD g_prev[4];

struct PadState {
    DWORD packet;
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
};

bool PartyActive() {
    return engine::Party_IsActive(engine::kParty);
}

// The lobby is open and player 1 is not in a match or loading into one.
bool InLobby() {
    if (!g_lobbyOpened || g_p2.started) return false;
    if (*(const uint32_t*)(engine::kUiState + 8) >= 5) return false;
    return PartyActive();
}

void WriteMembers() {
    uint8_t* host = (uint8_t*)kMembers;
    if (host[0] < 3) return; // no host member yet
    bool changed = false;
    for (int i = 1; i <= 3; ++i) {
        uint8_t* m = (uint8_t*)(kMembers + i * kMemberSize);
        if (i <= g_joined) {
            uint64_t xuid = *(const uint64_t*)(host + 0x38) + (uint64_t)i;
            if (m[0] >= 3 && *(const uint64_t*)(m + 0x38) == xuid) continue;
            memcpy(m, host, kMemberSize);
            *(uint64_t*)(m + 0x38) = xuid;
            char name[32];
            _snprintf_s(name, sizeof(name), _TRUNCATE, "%.28s %d", (const char*)(host + 0x40), i);
            memset(m + 0x40, 0, 32);
            memcpy(m + 0x40, name, strlen(name));
            changed = true;
        } else if (i <= g_written && m[0]) {
            memset(m, 0, kMemberSize);
            changed = true;
        }
    }
    g_written = g_joined;
    if (changed) {
        bool sent = LuiEvent(0, "party_joined");
        Log("[lobby] party members written for %d joined players, party_joined %s", g_joined,
            sent ? "sent" : "not sent");
    }
}

// The hint line. 0x004ad550(localClient) is the second of two debug text draws at the end
// of UpdateScreenRenderLocalViews (0x0055c4f2); it runs every frame, also in the menus, after
// the menu is drawn. The hook draws one line before it with the engine's text draw
// 0x004b8020(localClient, screen placement, x, y, x scale, y scale, text, colour, style, font):
// virtual 640x480 coordinates of the placement 0x011e29d0, style & 3 == 3 centres on x,
// style & 0xc == 4 moves y down by the font height. Fonts: the UI's small font 0x02879010,
// else the dev font the debug draws use (0x00553910 registers it).
void* oDebugDraw;
const uint8_t kProDebugDraw[8] = {0x83, 0xec, 0x2c, 0xa1, 0xd8, 0x0f, 0x06, 0x01};

void DrawLine(int lc, float x, float y, float scale, const char* text, const float* colour, void* font) {
    engine::CG_DrawTextAligned(lc, engine::kScrPlaceText, x, y, scale, scale, text, colour, 4, font);
}

void __cdecl HookDebugDraw(int lc) {
    if (LobbyJoinOn() && ReadIniInt(L"lobby_hint", 1) && InLobby()) {
        void* font = *(void**)engine::kDebugFont;
        if (!font) font = ((void*(__cdecl*)(const char*, int))0x00553910)("fonts/smallDevFont", 1);
        if (font) {
            static const float kBlack[4] = {0, 0, 0, 0.8f}, kWhite[4] = {0.75f, 0.75f, 0.75f, 1};
            char text[96];
            if (g_joined >= 3)
                _snprintf_s(text, sizeof(text), _TRUNCATE, "4 players. Gamepad: B = leave");
            else
                _snprintf_s(text, sizeof(text), _TRUNCATE, "Player %d: press A on a gamepad to join%s", g_joined + 2,
                            g_joined ? " (B = leave)" : "");
            // Keys in pixels of a 1920x1080 screen; virtual = (pixel - 144) / 2.25 on x, pixel / 2.25 on y.
            // The line sits under the last player row of the list and moves down with each joined player.
            float x = (ReadIniInt(L"lobby_hint_x", 1068) - 144) / 2.25f;
            float y = (ReadIniInt(L"lobby_hint_y", 136) + g_joined * ReadIniInt(L"lobby_hint_step", 45)) / 2.25f;
            float scale = ReadIniInt(L"lobby_hint_scale", 55) / 100.0f;
            DrawLine(lc, x + 1, y + 1, scale, text, kBlack, font);
            DrawLine(lc, x, y, scale, text, kWhite, font);
        }
    }
    ((void(__cdecl*)(int))oDebugDraw)(lc);
}

} // namespace

extern const Detour kLobbyHintHook = {"lobby hint line (debug text draw)",
                                      0x004ad550,
                                      kProDebugDraw,
                                      sizeof(kProDebugDraw),
                                      (void*)HookDebugDraw,
                                      &oDebugDraw};

bool LobbyJoinOn() {
    return ReadIniInt(L"lobby_join", 1) != 0;
}
int LobbyJoined() {
    return g_joined;
}
int LobbyPad(int player) {
    return (player >= 1 && player <= g_joined) ? g_joinPad[player - 1] : -1;
}

void LobbyFrame() {
    if (!LobbyJoinOn()) return;
    if (!InLobby()) {
        memset(g_prev, 0xff, sizeof(g_prev));
        return;
    } // no edge from a button held through the match
    int p1 = PlayerDevice(0);
    for (int pad = 0; pad < 4; ++pad) {
        PadState s = {};
        bool there = pGetState && pGetState((DWORD)pad, &s) == ERROR_SUCCESS;
        WORD pressed = there ? (WORD)(s.buttons & ~g_prev[pad]) : 0;
        g_prev[pad] = there ? s.buttons : 0;
        DevLobbyPad(pad, &there, &pressed);
        int slot = -1;
        for (int i = 0; i < g_joined; ++i)
            if (g_joinPad[i] == pad) slot = i;
        if (!there && slot < 0) continue;
        if (slot < 0 && (pressed & kPadA) && pad != p1 && g_joined < 3) {
            g_joinPad[g_joined++] = pad;
            Log("[lobby] pad %d joins as player %d (connection state %u)", pad, g_joined + 1,
                *(const uint32_t*)(engine::kUiState + 8));
        } else if (slot >= 0 && ((pressed & kPadB) || !there)) {
            Log("[lobby] pad %d leaves (player %d)%s", pad, slot + 2, there ? "" : ", pad gone");
            for (int i = slot; i < g_joined - 1; ++i)
                g_joinPad[i] = g_joinPad[i + 1];
            g_joinPad[--g_joined] = -1;
        }
    }
    WriteMembers();
}
