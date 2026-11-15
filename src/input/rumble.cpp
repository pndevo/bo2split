#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/offsets.h"

// player_rumble=1: gamepad vibration for players 2 to 4.
//
// The cgame keeps the rumbles that play right now in a list per local client (32 entries of
// 0x1c bytes at engine::kRumbleState + 0x4ec0, then the listener's position). Its update
// (0x004ef2a0, once per CG_DrawActiveFrame, so once per view pass) adds the entries up for the
// player of the pass and hands the two motor strengths (0 to 1) to the pad of the local
// client: 0x0077c640 (low frequency motor) and 0x0077c660 (high frequency motor) store them in
// the pad state, and the pad code sends them to XInput once per frame.
//
// All passes run as local client 0. Without this file they share one list, and every pass
// writes its result to player 1's pad. Here every shadow player has a list of its own, swapped
// in and out with its cgame block, and the two strengths of a shadow pass are kept for that
// player instead of reaching player 1's pad. ShadowRumbleFrame sends them to the player's own
// pad, unless the player turned vibration off in its controller options.
//
// Starting a rumble (0x004ef5f0) and the update both give up unless the player of the snapshot
// (snapshot + 0x1b0) is the client of the cgame block (cg + 0): a spectator feels nothing, and
// the update then empties the list and stops the motors. A shadow pass runs on player 1's
// snapshot with its own client number in the block, so for both calls the snapshot carries
// the pass's client number.
//
// p_rumble_log=N: N lines with the strengths a pass asked for.

constexpr size_t kListOffset = 0x4ec0, kListSize = 0x390, kEntrySize = 0x1c, kEntries = 32;

static uint8_t g_listSlot[3][kListSize];
static uint8_t g_listSave[kListSize];
static bool g_listSwapped;
static int g_listCur;
static volatile float g_strength[3][2]; // low, high motor per shadow player
static volatile DWORD g_strengthTick[3];

static bool g_inRumbleUpdate; // the cgame's rumble update runs right now
static bool RumbleOn() {
    return ReadIniInt(L"player_rumble", 1) != 0;
}

void ShadowRumbleSwapIn(int slot, bool fresh) {
    if (slot < 0 || slot > 2 || !RumbleOn()) return;
    uint8_t* list = (uint8_t*)(engine::kRumbleState + kListOffset);
    if (fresh) {
        // A new block starts with no rumble playing: the entries of player 1's list, each
        // cleared the way the update clears them.
        memcpy(g_listSlot[slot], list, kListSize);
        for (size_t i = 0; i < kEntries; i++) {
            uint8_t* e = g_listSlot[slot] + i * kEntrySize;
            *(int*)(e + 0) = 0;
            *(int*)(e + 4) = -1;
            *(int*)(e + 0xc) = 0;
        }
    }
    memcpy(g_listSave, list, kListSize);
    memcpy(list, g_listSlot[slot], kListSize);
    g_listCur = slot;
    g_listSwapped = true;
}

void ShadowRumbleSwapOut() {
    if (!g_listSwapped) return;
    g_listSwapped = false;
    uint8_t* list = (uint8_t*)(engine::kRumbleState + kListOffset);
    memcpy(g_listSlot[g_listCur], list, kListSize);
    memcpy(list, g_listSave, kListSize);
}

typedef void(__cdecl* GpadSetRumble_t)(int, float);
static GpadSetRumble_t oGpadSetLowRumble, oGpadSetHighRumble;
static bool TakeForShadow(int motor, float value) {
    if (!(g_cgSwapped && g_inRenderViews && g_viewPass >= 1 && g_viewPass <= 3 && RumbleOn())) return false;
    int slot = g_viewPass - 1;
    // Only the rumble update speaks for the player. Other code that sets the motors during
    // the pass (it stops them for local client 0) is for player 1 and must not reach either pad.
    if (!g_inRumbleUpdate) {
        static int leftOther = 8;
        if (leftOther > 0 && ReadIniInt(L"p_rumble_log", 0)) {
            --leftOther;
            Log("[rumble] pass %d: %s motor %.3f set outside the rumble update, dropped", g_viewPass,
                motor ? "high" : "low", value);
        }
        return true;
    }
    g_strength[slot][motor] = value;
    g_strengthTick[slot] = GetTickCount();
    static int left = -1, armed = -1;
    int v = ReadIniInt(L"p_rumble_log", 0);
    if (v != armed) {
        armed = v;
        left = v;
    }
    if (left > 0 && value != 0.0f) {
        --left;
        Log("[rumble] pass %d: %s motor %.3f", g_viewPass, motor ? "high" : "low", value);
    }
    return true;
}
static void __cdecl HookGpadSetLowRumble(int pad, float value) {
    if (!TakeForShadow(0, value)) oGpadSetLowRumble(pad, value);
}
static void __cdecl HookGpadSetHighRumble(int pad, float value) {
    if (!TakeForShadow(1, value)) oGpadSetHighRumble(pad, value);
}

// The snapshot's player is the pass's player while fn runs.
//
// A rumble of the player itself (a shot, a hit) is started on the player of the snapshot, and
// the update lets a rumble without a range through only when its entity is that player. In
// a shadow player's list such an entry is the shadow player's own, so before the update the
// entries on the snapshot's player are moved to the pass's client number.
template <typename F> static void AsPassPlayer(F fn, bool update) {
    uint8_t* cg = *(uint8_t**)engine::kCg;
    int* snapClient = nullptr;
    if (g_cgSwapped && g_inRenderViews && g_viewPass >= 1 && g_viewPass <= 3 && RumbleOn() && cg) {
        uint8_t* snap = *(uint8_t**)(cg + 0x28);
        if (snap) snapClient = (int*)(snap + 0x1b0);
    }
    int saved = snapClient ? *snapClient : 0;
    if (snapClient) {
        *snapClient = *(const int*)cg;
        if (update && saved != *(const int*)cg) {
            for (size_t i = 0; i < kEntries; i++) {
                int* e = (int*)(engine::kRumbleState + kListOffset + i * kEntrySize);
                if (e[0] && e[3] == 1 && e[4] == saved) e[4] = *(const int*)cg; // source 1 = an entity, its number
            }
        }
    }
    fn();
    if (snapClient) *snapClient = saved;
}
// Six stack arguments cover every caller; the function reads none past them.
typedef void(__cdecl* CgPlayRumble_t)(int, int, int, int, int, int);
static CgPlayRumble_t oCgPlayRumble;
static void __cdecl HookCgPlayRumble(int a, int b, int c, int d, int e, int f) {
    // p_rumble_log: which rumble starts in which pass (the second argument is its name).
    static int left = -1, armed = -1;
    int v = ReadIniInt(L"p_rumble_log", 0);
    if (v != armed) {
        armed = v;
        left = v;
    }
    if (left > 0 && b) {
        --left;
        Log("[rumble] pass %d: start \"%.40s\" (arguments %d %d %d %d)", g_inRenderViews ? g_viewPass : -1,
            (const char*)b, c, d, e, f);
    }
    AsPassPlayer([&] { oCgPlayRumble(a, b, c, d, e, f); }, false);
}
typedef void(__cdecl* CgUpdateRumble_t)(int);
static CgUpdateRumble_t oCgUpdateRumble;
static void __cdecl HookCgUpdateRumble(int lc) {
    // p_rumble_log: the playing entries of a shadow pass before the update adds them up.
    static int left = -1, armed = -1;
    int v = ReadIniInt(L"p_rumble_log", 0);
    if (v != armed) {
        armed = v;
        left = v;
    }
    const uint8_t* cg = *(const uint8_t* const*)engine::kCg;
    if (left > 0 && cg && g_cgSwapped && g_inRenderViews && g_viewPass >= 1) {
        const uint8_t* list = (const uint8_t*)(engine::kRumbleState + kListOffset);
        const uint8_t* snap = *(const uint8_t* const*)(cg + 0x28);
        for (size_t i = 0; i < kEntries && left > 0; i++) {
            const int* e = (const int*)(list + i * kEntrySize);
            if (!e[0] || !e[3]) continue;
            const uint8_t* info = (const uint8_t*)e[0];
            --left;
            Log("[rumble] pass %d entry %u: start %d, block time %d, duration %.0f, range %.0f (ranged %d), source %d "
                "of %d, block client %d, snapshot client %d",
                g_viewPass, (unsigned)i, e[1], *(const int*)(cg + 0x4808c), *(const float*)(info + 4),
                *(const float*)(info + 8), *(const int*)(info + 0x18), e[3], e[4], *(const int*)cg,
                snap ? *(const int*)(snap + 0x1b0) : -1);
        }
    }
    g_inRumbleUpdate = true;
    AsPassPlayer([&] { oCgUpdateRumble(lc); }, true);
    g_inRumbleUpdate = false;
}

// The fire rumble of a weapon (weapon def + 0x900, a rumble name) starts in the fire event
// (0x00534210) when the shot is the view's own player's. The fire events of the shadow
// players run in player 1's cgame, where they are other characters, so nothing starts. This
// runs in the shadow player's own pass for a shot the fire sound hook counted and starts the
// rumble of the weapon in hand the way the game does for player 1 (0x004ef8a0: on the client).
void ShadowRumbleShot(const uint8_t* cg) {
    if (!RumbleOn() || !cg) return;
    int weapon = (int)engine::BG_GetWeapon(cg + engine::kCgPredictedPs);
    const uint8_t* def = weapon ? engine::BG_GetWeaponDef(weapon) : nullptr;
    const char* name = def ? *(const char* const*)(def + 0x900) : nullptr;
    if (name && *name) ((void(__cdecl*)(int, const char*))engine::kCG_PlayRumbleOnClient)(0, name);
}

// Once per frame: the strengths of every shadow player go to its pad. A player whose pass
// has not run for a quarter of a second (pause, end of the match, the player left) gets zero.
void ShadowRumbleFrame() {
    static WORD sent[4][2];
    static DWORD sentTick[4];
    WORD want[4][2] = {};
    DWORD now = GetTickCount();
    for (int slot = 0; slot < 3; slot++) {
        int pad = PlayerInputDevice(slot + 1);
        if (pad < 0 || pad > 3) continue;
        if (!RumbleOn() || !ShadowPlayerSettings(slot).rumble || ShadowMenuOpen(slot)) continue;
        if (now - g_strengthTick[slot] > 250) continue;
        for (int m = 0; m < 2; m++) {
            float f = g_strength[slot][m];
            if (!(f > 0.0f)) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            want[pad][m] = (WORD)(f * 65535.0f);
        }
    }
    int p1 = P1EnginePad(); // player 1's pad belongs to the game
    for (int pad = 0; pad < 4; pad++) {
        if (pad == p1) continue;
        bool change = want[pad][0] != sent[pad][0] || want[pad][1] != sent[pad][1];
        // A strength that stays is sent again once a second, so a pad plugged in late follows.
        bool refresh = (want[pad][0] || want[pad][1]) && now - sentTick[pad] > 1000;
        if (!change && !refresh) continue;
        DWORD rc = XInputSetVibration((DWORD)pad, want[pad][0], want[pad][1]);
        static int leftSent = -1, armedSent = -1;
        int v = ReadIniInt(L"p_rumble_log", 0);
        if (v != armedSent) {
            armedSent = v;
            leftSent = v;
        }
        if (leftSent > 0) {
            --leftSent;
            Log("[rumble] pad %d: low %u high %u sent, result %lu", pad, (unsigned)want[pad][0], (unsigned)want[pad][1],
                rc);
        }
        sent[pad][0] = want[pad][0];
        sent[pad][1] = want[pad][1];
        sentTick[pad] = now;
    }
}

static const uint8_t kProGpadSetRumble[] = {0x8b, 0x44, 0x24, 0x04, 0xf3, 0x0f, 0x10, 0x44, 0x24, 0x08};
static const uint8_t kProCgPlayRumble[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xc0};
static const uint8_t kProCgUpdateRumble[] = {0x8b, 0x0d, 0x5c, 0xf7, 0x05, 0x01};
const Detour kRumbleHooks[4] = {
    {"cgame rumble start (a shadow player's rumbles start in its own pass)", 0x004ef5f0, kProCgPlayRumble,
     sizeof(kProCgPlayRumble), (void*)HookCgPlayRumble, (void**)&oCgPlayRumble},
    {"cgame rumble update (a shadow player's rumbles are added up in its own pass)", 0x004ef2a0, kProCgUpdateRumble,
     sizeof(kProCgUpdateRumble), (void*)HookCgUpdateRumble, (void**)&oCgUpdateRumble},
    {"GPad_SetLowRumble (a shadow player's vibration goes to its own pad)", 0x0077c640, kProGpadSetRumble,
     sizeof(kProGpadSetRumble), (void*)HookGpadSetLowRumble, (void**)&oGpadSetLowRumble},
    {"GPad_SetHighRumble (a shadow player's vibration goes to its own pad)", 0x0077c660, kProGpadSetRumble,
     sizeof(kProGpadSetRumble), (void*)HookGpadSetHighRumble, (void**)&oGpadSetHighRumble},
};
