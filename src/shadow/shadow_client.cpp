#include "core/common.h"
#include "shadow/shadow.h"
#include "core/dev.h"
#include "engine/common.h"
#include "engine/server.h"
#include "engine/client.h"
#include "engine/offsets.h"

ShadowClient g_p2;

// extra_players=N (0..2; 0 = off): N further shadow clients (players 3 and 4). This file holds
// their network side: own cl / clc / clientUIActive block / controller-map slot as for player 2,
// loopback channel 2 + i (client 0 uses 0, player 2 uses 1, the server 4), qport + 2 + i,
// XUID + 2 + i. Each sends one usercmd per frame from its device (p3_pad / p4_pad), so the server
// spawns and keeps it; its cgame block and view pass are in cgame_swap.cpp and view.cpp. The
// listen server carries more than one extra client this way. Log prefix [p3] / [p4].
struct ExtraClient {
    bool started, inContext;
    int index; // 0 = player 3, 1 = player 4
    uint8_t* cl;
    uint8_t* clc;
    uint32_t bufs[6];
    uint8_t uiState[shadowcl::kUiStateSize], savedUiState[shadowcl::kUiStateSize];
    uint8_t ctrlMap[shadowcl::kCtrlMapSize], savedCtrlMap[shadowcl::kCtrlMapSize];
    char xuid[32];
    uint32_t savedCl, savedClc, lastLoggedState;
    int lastCmdTime;
    DWORD nextDebug;
    // input (p3_pad / p4_pad)
    float pitch, yaw;
    LARGE_INTEGER lastPad;
    bool crouch, stanceWas, weapNextWas, weapPrevWas, cycled, jumpWantWas, jumpSentWas, jumpPending, weaponSettled;
    int loggedWeapon;
};
static ExtraClient g_px[2];
static ExtraClient* g_pxCtx; // the extra client whose context is switched in
uint8_t* ExtraCl(int index) {
    if (index < 0 || index > 1) return nullptr;
    const ExtraClient& x = g_px[index];
    if (!x.started || !x.cl || *(const uint32_t*)(x.uiState + 8) < 9 || !*(const uint32_t*)(x.cl + engine::kClSnapshot))
        return nullptr;
    return x.cl;
}
uint8_t* ShadowSlotClc(int slot) {
    if (slot == 0) return g_p2.started ? g_p2.clc : nullptr;
    if (slot < 1 || slot > 2 || !ExtraCl(slot - 1)) return nullptr;
    return g_px[slot - 1].clc;
}
uint8_t* ShadowSlotCl(int slot) {
    if (slot == 0) return g_p2.started ? g_p2.cl : nullptr;
    if (slot < 1 || slot > 2) return nullptr;
    return ExtraCl(slot - 1);
}
// Usercmd angle offsets of a shadow player (slot 0 = player 2, 1 / 2 = players 3 / 4): the kick
// angles the cgame worked out in that player's pass (HookCgSubmitInputAndView). They are
// absolute values for the frame, added to the facing in the usercmd and in the camera.
float g_viewKick[3][2];
void ShadowSetViewKick(int slot, float pitch, float yaw) {
    if (slot >= 0 && slot <= 2) {
        g_viewKick[slot][0] = pitch;
        g_viewKick[slot][1] = yaw;
    }
}
bool ExtraAngles(int index, float* pitch, float* yaw) {
    if (index < 0 || index > 1 || !g_px[index].started) return false;
    *pitch = g_px[index].pitch;
    *yaw = g_px[index].yaw;
    return true;
}
void ExtraSetAngles(int index, float pitch, float yaw) {
    if (index < 0 || index > 1) return;
    g_px[index].pitch = pitch > 85.0f ? 85.0f : pitch < -85.0f ? -85.0f : pitch;
    g_px[index].yaw = yaw;
}
int ExtraWanted() {
    if (LobbyJoinOn()) return LobbyJoined() > 1 ? LobbyJoined() - 1 : 0; // players 3 and 4 from the lobby
    int n = ReadIniInt(L"extra_players", 0);
    return n < 0 ? 0 : n > 2 ? 2 : n;
}

typedef char*(__cdecl* XuidForController_t)(int);
static XuidForController_t oXuidForController;
static char* __cdecl HookXuidForController(int controller) {
    if (g_p2.started && g_p2.inContext) return g_p2.xuid;
    if (g_pxCtx) return g_pxCtx->xuid;
    return oXuidForController(controller);
}
static const uint8_t kProXuidForController[] = {0x8b, 0x44, 0x24, 0x04, 0x69, 0xc0, 0x70, 0x10, 0x00, 0x00};

static void P2Enter() {
    g_p2.inContext = true;
    g_p2.savedCl = *(uint32_t*)engine::kClPtr;
    g_p2.savedClc = *(uint32_t*)engine::kClcPtr;
    memcpy(g_p2.savedUiState, (void*)engine::kUiState, shadowcl::kUiStateSize);
    memcpy(g_p2.savedCtrlMap, (void*)engine::kCtrlMap, shadowcl::kCtrlMapSize);
    *(uint32_t*)engine::kClPtr = (uint32_t)g_p2.cl;
    *(uint32_t*)engine::kClcPtr = (uint32_t)g_p2.clc;
    memcpy((void*)engine::kUiState, g_p2.uiState, shadowcl::kUiStateSize);
    memcpy((void*)engine::kCtrlMap, g_p2.ctrlMap, shadowcl::kCtrlMapSize);
    *(int*)engine::kQportBase += 1;
}

static void P2Leave() {
    memcpy(g_p2.uiState, (void*)engine::kUiState, shadowcl::kUiStateSize);
    memcpy(g_p2.ctrlMap, (void*)engine::kCtrlMap, shadowcl::kCtrlMapSize);
    *(uint32_t*)engine::kClPtr = g_p2.savedCl;
    *(uint32_t*)engine::kClcPtr = g_p2.savedClc;
    memcpy((void*)engine::kUiState, g_p2.savedUiState, shadowcl::kUiStateSize);
    memcpy((void*)engine::kCtrlMap, g_p2.savedCtrlMap, shadowcl::kCtrlMapSize);
    *(int*)engine::kQportBase -= 1;
    g_p2.inContext = false;
}

static void* ShadowAlloc(size_t n) {
    void* p = VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) Log("[p2] VirtualAlloc(%u) failed", (unsigned)n);
    return p;
}

// Mirrors AllocateClientConnections (0x005516b0) for one extra client: same header as
// client 0, own parse/snapshot/outPacket arrays.
bool P2AllocateClient() {
    const uint32_t* cl0 = (const uint32_t*)*(uint32_t*)engine::kClPtr;
    static uint32_t bufs[6]; // allocated once, reused across map restarts
    static const int slot[6] = {0x0, 0x3, 0x6, 0x9, 0xc, 0xd};
    uint32_t n = cl0[0xe];      // snapshot / outPacket count (8 or 16)
    uint32_t nClients = cl0[7]; // parseClients count
    if (!g_p2.cl) {
        g_p2.cl = (uint8_t*)ShadowAlloc(shadowcl::kClSize);
        g_p2.clc = (uint8_t*)ShadowAlloc(shadowcl::kClcSize);
        if (!g_p2.cl || !g_p2.clc) return false;
        bufs[0] = (uint32_t)ShadowAlloc(n * 0x80);         // parseMatchStates
        bufs[1] = (uint32_t)ShadowAlloc(n * 0xc1c0);       // parseEntities
        bufs[2] = (uint32_t)ShadowAlloc(nClients * 0x120); // parseClients
        bufs[3] = (uint32_t)ShadowAlloc(n * 0xc80);        // parseActors
        bufs[4] = (uint32_t)ShadowAlloc(n * 0xc);          // outPackets
        bufs[5] = (uint32_t)ShadowAlloc(n * 0x2a48);       // snapshots
        for (uint32_t b : bufs)
            if (!b) return false;
    }
    memset(g_p2.cl, 0, shadowcl::kClSize);
    memcpy(g_p2.cl, cl0, 0x40);
    uint32_t* cl = (uint32_t*)g_p2.cl;
    for (int i = 0; i < 6; ++i)
        cl[slot[i]] = bufs[i];
    g_p2.lastCmdTime = 0;
    Log("[p2] client structs: cl=%p clc=%p, %u snapshots, %u parseClients, mask %08x", g_p2.cl, g_p2.clc, n, nClients,
        cl0[0xf]);
    return true;
}

// Replays ConnectLocalClientToServer (0x00557c10) for the shadow client: the server
// address is the one client 0 is connected to.
static void ShadowDrainLoopback(int channel);
void P2StartConnect() {
    if (ReadIniInt(L"p2_drain_loopback", 1)) ShadowDrainLoopback(1);
    const uint8_t* clc0 = (const uint8_t*)*(uint32_t*)engine::kClcPtr;
    const char* xuidStr0 = (const char*)engine::kXuid0;
    uint64_t xuid0 = _strtoui64(xuidStr0, nullptr, 16);
    _snprintf_s(g_p2.xuid, sizeof(g_p2.xuid), _TRUNCATE, "%I64x", xuid0 ? xuid0 + 1 : 0x0110000100000002ULL);
    Log("[p2] controller 0 xuid string '%s' -> player 2 xuid '%s'", xuidStr0, g_p2.xuid);
    memset(g_p2.uiState, 0, sizeof(g_p2.uiState));
    *(uint32_t*)(g_p2.uiState + 0) = 3;    // active + in use
    *(uint32_t*)(g_p2.uiState + 8) = 4;    // CA_CONNECTING
    *(uint32_t*)(g_p2.uiState + 0x10) = 3; // migration state as the engine's own connect sets it
    memcpy(g_p2.ctrlMap, (const void*)engine::kCtrlMap, sizeof(g_p2.ctrlMap));
    *(uint32_t*)(g_p2.ctrlMap + 0) = 1; // controller used
    *(uint32_t*)(g_p2.ctrlMap + 4) = 0; // lcn stays 0: all engine code runs as "client 0"
    *(uint32_t*)(g_p2.ctrlMap + 8) =
        0; // controller 0: every per-controller table has one slot (Live, stats, messaging)
    *(uint32_t*)(g_p2.ctrlMap + 0x10) = 1; // loopback channel 1
    memset(g_p2.clc, 0, shadowcl::kClcSize);
    uint32_t* clc = (uint32_t*)g_p2.clc;
    clc[0] = (uint32_t)(*(int*)engine::kQportBase + 1); // qport
    memcpy(g_p2.clc + 0x10, clc0 + 0x10, 0x14);         // server address (loopback, server channel)
    clc[9] = (uint32_t)-99999;                          // connectTime
    clc[10] = 0;                                        // connectPacketCount
    *(uint32_t*)(g_p2.cl + shadowcl::kClServerId) = 0;
    *(uint32_t*)(g_p2.cl + shadowcl::kClTimeoutCount) = 0;
    g_p2.started = true;
    g_p2.startedTick = GetTickCount();
    g_p2.lastLoggedState = 4;
    Log("[p2] connecting: qport %u, netsrc 1, server addr words %08x %08x %08x %08x %08x", clc[0], clc[4], clc[5],
        clc[6], clc[7], clc[8]);
    wchar_t wcmd[256];
    char cmd[256];
    wchar_t ini[MAX_PATH];
    lstrcpynW(ini, g_gameDir, MAX_PATH);
    lstrcatW(ini, L"bo2ss.ini");
    GetPrivateProfileStringW(L"bo2ss", L"p2_exec", L"r_dof_enable 0", wcmd, 256, ini);
    if (wcmd[0]) {
        WideCharToMultiByte(CP_ACP, 0, wcmd, -1, cmd, 256, nullptr, nullptr);
        Log("[p2] exec: %s", cmd);
        ExecCmd(cmd);
    }
}

static const char* ConnStateName(uint32_t s) {
    static const char* names[] = {"DISCONNECTED", "CINEMATIC",    "LOGO",    "UI",     "CONNECTING", "CHALLENGING",
                                  "CONNECTED",    "SENDINGSTATS", "LOADING", "PRIMED", "ACTIVE"};
    return s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

// Appends one usercmd per frame once a snapshot has arrived, so the server moves the
// client from CS_PRIMED to CS_ACTIVE (ClientBegin -> player spawns).
// usercmd_s (0x3c bytes), layout from MSG_WriteDeltaUsercmdKey (0x006d25f0):
//   +0 serverTime, +4 buttons, +8 buttons2, +0xc angles[3] (ANGLE2SHORT as int),
//   +0x18 weapon, +0x1c offhand, +0x20 (weapon-related, 26 bits), +0x24 forwardmove (char),
//   +0x25 rightmove (char), +0x27/+0x28 (chars), +0x2a/+0x30/+0x32/+0x38/+0x3a shorts, +0x34..0x36 bytes.
struct UsercmdView {
    int serverTime, buttons, buttons2, angles[3], weapon, offhand, w3;
    int8_t forwardmove, rightmove;
};

static inline int Angle2Short(float deg) {
    return (int)(deg * (65536.0f / 360.0f)) & 0xffff;
}

float g_p2Pitch, g_p2Yaw;  // player 2 view angles (degrees)
volatile bool g_p2AdsHeld; // LT / ads button in the latest usercmd
// Per shadow player, from its last view pass: the field of view factor of the turn rate and
// the aim-down-sights fraction (0 = hip, 1 = sights).
float g_viewLook[3][2] = {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}};
void ShadowSetViewLook(int slot, float fovScale, float adsFrac) {
    if (slot < 0 || slot > 2) return;
    if (!(fovScale > 0.05f && fovScale < 4.0f)) fovScale = 1.0f;
    if (!(adsFrac >= 0.0f && adsFrac <= 1.0f)) adsFrac = 0.0f;
    g_viewLook[slot][0] = fovScale;
    g_viewLook[slot][1] = adsFrac;
}

// Input of a shadow player (player index 1..3) from its device (input.cpp): an XInput pad
// or keyboard and mouse. Stick values after the dead zones, the look step of this command in
// degrees, the pad buttons and the actions held on keyboard and mouse.
struct PlayerInput {
    float lx, ly, rx, ry, yawStep, pitchStep;
    unsigned buttons, lt, rt, kbmActs;
};
static void ReadPlayerInput(int player, float dtMs, PlayerInput* in) {
    *in = {};
    int dev = PlayerInputDevice(player);
    if (dev == kDevKbm) {
        KbmState k;
        if (!KbmRead(player, &k)) return;
        in->lx = k.right;
        in->ly = k.fwd;
        in->kbmActs = k.acts;
        // mouse_sens: tenths of the engine's "sensitivity" (50 = 5.0), 0.022 degrees per count
        // and unit as the engine's m_yaw / m_pitch.
        float s = 0.022f * ReadIniInt(L"mouse_sens", 50) * 0.1f;
        if (k.ads) s *= ReadIniInt(L"look_ads_scale", 65) * 0.01f;
        in->yawStep = -k.dx * s;
        in->pitchStep = k.dy * s;
        return;
    }
    struct XINPUT_STATE_ {
        DWORD packet;
        WORD buttons;
        BYTE lt, rt;
        SHORT lx, ly, rx, ry;
    } st = {};
    if (dev < 0 || !pGetState || pGetState((DWORD)dev, &st) != ERROR_SUCCESS) st = {}; // no pad: neutral stick
    // The player's own controller options (player_menu.cpp). Stick layout: 1 "southpaw" swaps the
    // sticks, 2 "legacy" moves forward and turns on the left stick and strafes and looks up and
    // down on the right one, 3 is legacy with the sticks swapped.
    const ShadowSettings& own = ShadowPlayerSettings(player - 1);
    {
        SHORT lx = st.lx, ly = st.ly, rx = st.rx, ry = st.ry;
        if (own.sticks == 1) {
            st.lx = rx, st.ly = ry, st.rx = lx, st.ry = ly;
        } else if (own.sticks == 2) {
            st.lx = rx, st.rx = lx;
        } else if (own.sticks == 3) {
            st.ly = ry, st.ry = ly;
        }
    }
    if (ShadowMenuOpen(player - 1)) st.lx = st.ly = st.rx = st.ry = 0; // the pad drives the player's menu
    DevRightStick(player, &st.rx, &st.ry, nullptr);
    auto axis = [](SHORT v) {
        float f = v / 32767.0f;
        return (f > -0.2f && f < 0.2f) ? 0.0f : f;
    };
    in->lx = axis(st.lx);
    in->ly = axis(st.ly);
    in->buttons = st.buttons;
    in->lt = st.lt;
    in->rt = st.rt;
    // Right stick (look): radial dead zone, rescaled so the turn rate starts at zero at its
    // edge (a per-axis cut would jump from 0 to 20 % of the full rate and allow no fine aim), then a
    // response curve (look_curve, percent exponent: 100 = linear, 150 = finer around the
    // centre, same full rate). look_ads_scale (percent) slows the turn while LT is held.
    // With look_native the curve is the game's own (0x0048d0e0): the stick travel times the
    // value of the graph aim_input_graph_index names at that travel (the graphs lie at
    // engine::kAimInputGraphs), and with aim_scale_view_axis the smaller axis shrinks by the
    // difference of the two, so a turn that is nearly level stays level.
    bool native = ReadIniInt(L"look_native", 1) != 0;
    float rx = st.rx / 32767.0f, ry = st.ry / 32767.0f;
    {
        float mag = sqrtf(rx * rx + ry * ry);
        float dz = ReadIniInt(L"look_deadzone", 20) * 0.01f;
        if (dz < 0.0f) dz = 0.0f;
        if (dz > 0.9f) dz = 0.9f;
        if (mag <= dz) {
            rx = 0.0f;
            ry = 0.0f;
        } else {
            float m = mag > 1.0f ? 1.0f : mag;
            float travel = (m - dz) / (1.0f - dz), out;
            if (native) {
                out = travel;
                void* on = *(void**)engine::kAimInputGraphEnabledDvar;
                void* index = *(void**)engine::kAimInputGraphIndexDvar;
                int g = index ? engine::DvarIntValue(index) : -1;
                if (on && engine::Dvar_GetBool(on) && g >= 0 && g < engine::kAimInputGraphCount) {
                    const uint8_t* graph = (const uint8_t*)(engine::kAimInputGraphs + g * engine::kAimInputGraphSize);
                    int knots = *(const int*)(graph + 0x140);
                    if (knots >= 2 && knots <= 32) out *= engine::GraphFloat_GetValue(graph, travel);
                }
            } else {
                out = powf(travel, ReadIniInt(L"look_curve", 150) * 0.01f);
            }
            rx *= out / mag;
            ry *= out / mag;
            void* scaleAxis = *(void**)engine::kAimScaleViewAxisDvar;
            if (native && scaleAxis && engine::Dvar_GetBool(scaleAxis)) {
                float ax = fabsf(rx), ay = fabsf(ry);
                if (ay > ax)
                    rx *= 1.0f - (ay - ax);
                else
                    ry *= 1.0f - (ax - ay);
            }
        }
    }
    in->rx = rx;
    in->ry = ry;
    if (native) {
        // The turn rates the game gives player 1's pad (0x0048d4e0): per axis the rate of the
        // dvars aim_turnrate_yaw / _pitch (260 and 90 degrees per second), blended to
        // aim_turnrate_yaw_ads / _pitch_ads (90 and 55) by the aim-down-sights fraction, times
        // the field of view factor of the player's view (1.0 at a field of view of 65, smaller
        // with a zoomed weapon) and the player's look sensitivity. A rate that grows rises by
        // aim_accel_turnrate_lerp (1200 degrees per second squared, times the sensitivity)
        // while aim_accel_turnrate_enabled is set; a rate that falls is taken at once.
        auto rate = [](uintptr_t dvar, float fallback) {
            const uint8_t* d = *(const uint8_t* const*)dvar;
            return d ? *(const float*)(d + 0x18) : fallback;
        };
        int slot = player - 1;
        float fov = g_viewLook[slot][0], ads = g_viewLook[slot][1], dt = dtMs * 0.001f;
        float yawHip = rate(engine::kAimTurnrateYawDvar, 260.0f), pitchHip = rate(engine::kAimTurnratePitchDvar, 90.0f);
        float want[2] = {fabsf(rx) * (yawHip + (rate(engine::kAimTurnrateYawAdsDvar, 90.0f) - yawHip) * ads) * fov *
                             own.sensitivity,
                         fabsf(ry) * (pitchHip + (rate(engine::kAimTurnratePitchAdsDvar, 55.0f) - pitchHip) * ads) *
                             fov * own.sensitivity};
        //
        // The game runs this for player 1 at the start of every view pass, each time with the
        // whole frame time. With pad_turn_once that is cut to one turn per frame (predict.cpp)
        // and a shadow player takes one step per command. Without it the shadow players take
        // as many steps as there are views, so every pad on the screen still turns alike.
        static float cur[3][2]; // the turn rate each player has reached, yaw and pitch
        void* accelOn = *(void**)engine::kAimAccelTurnrateEnabledDvar;
        bool accel = accelOn && engine::Dvar_GetBool(accelOn) && PadTurnAccel(); // look_accel=0: no run-up
        float step = rate(engine::kAimAccelTurnrateLerpDvar, 1200.0f) * own.sensitivity * dt;
        int passes = PadTurnOncePerFrame() || g_viewCount < 1 ? 1 : g_viewCount > 4 ? 4 : g_viewCount;
        float turned[2] = {};
        for (int n = 0; n < passes; n++) {
            for (int i = 0; i < 2; i++) {
                float& c = cur[slot][i];
                if (accel && want[i] > c + step)
                    c += step;
                else
                    c = want[i];
                turned[i] += c * dt;
            }
        }
        in->yawStep = (rx > 0.0f ? -1.0f : 1.0f) * turned[0];
        in->pitchStep = ((ry > 0.0f) == (own.invert != 0) ? 1.0f : -1.0f) * turned[1];
        return;
    }
    float sens = ReadIniInt(L"look_speed", 180) * 0.001f * dtMs * own.sensitivity; // degrees per second * dt
    if (st.lt > 60) sens *= ReadIniInt(L"look_ads_scale", 65) * 0.01f;
    in->yawStep = -rx * sens;
    in->pitchStep = (own.invert ? ry : -ry) * sens * 0.6f;
}

// Fills player 2's usercmd from its device. The weapon fields start as a copy of player 1's
// latest usercmd (both start with the same pistol); the block below replaces them with player 2's own.
static void P2FillUsercmdFromPad(uint8_t* cmd, float dtMs) {
    PlayerInput st;
    ReadPlayerInput(1, dtMs, &st);
    float lx = st.lx, ly = st.ly, rx = st.rx, ry = st.ry;
    g_p2Yaw += st.yawStep;
    g_p2Pitch += st.pitchStep;
    if (g_p2Pitch > 85.0f) g_p2Pitch = 85.0f;
    if (g_p2Pitch < -85.0f) g_p2Pitch = -85.0f;
    UsercmdView* u = (UsercmdView*)cmd;
    cmd[0x27] = (uint8_t)(int8_t)(ry * 127.0f);               // right stick y (RStickUp -> +0x27 = 0x81)
    cmd[0x28] = (uint8_t)(int8_t)(rx * 127.0f);               // right stick x (RStickRight -> +0x28 = 0x81)
    u->angles[0] = Angle2Short(g_p2Pitch + g_viewKick[0][0]); // facing plus the kick angles of the player's pass
    u->angles[1] = Angle2Short(g_p2Yaw + g_viewKick[0][1]);
    u->angles[2] = 0;
    // Player 2's pad layout: controls.cpp (bits per action, stance state, action slots).
    int buttons = 0, buttons2 = 0;
    ShadowControlsButtons(0, st.buttons, st.lt, st.rt, st.kbmActs, &buttons, &buttons2);
    if (DevApplyInput(0, &ly, &lx)) { /* stick overridden */
    }
    g_p2AdsHeld = (buttons & 0x00100280) !=
                  0; // the ADS action's usercmd bits (kBitsAds); for the local zoom prediction (p2_interp_ads=2)
    // Weapon cycle: rising edge of the pseudo bit -> CG_WeapNext_f on player 2's block in the
    // next second pass (HookCgPredict); the bit never reaches the usercmd.
    {
        static bool wasDown;
        bool down = (buttons2 & kWeapNextPseudoBit) != 0;
        buttons2 &= ~kWeapNextPseudoBit;
        if (down && !wasDown) g_p2WeapNextPending = true;
        wasDown = down;
        static bool prevWasDown;
        bool prevDown = (buttons2 & kWeapPrevPseudoBit) != 0;
        buttons2 &= ~kWeapPrevPseudoBit;
        if (prevDown && !prevWasDown) g_p2WeapPrevPending = true;
        prevWasDown = prevDown;
    }
    u->forwardmove = (int8_t)(ly * 127.0f);
    u->rightmove = (int8_t)(lx * 127.0f);
    u->buttons = buttons;
    u->buttons2 = buttons2;
    // Weapon ids: the usercmd tells the server which weapon the player wants. Player 2 asks
    // for the weapon its own snapshot playerState holds (BG_GetWeapon 0x008db250 on cl+0x70:
    // ps+0x1b8, or +0x1cc/+0x1a4 in the alt/offhand states), so a weapon switch of player 1
    // never carries over (p2_own_weapon=1). Offhand ids still come from player 1's latest
    // usercmd. p2_own_weapon=2: the ids player 2's cgame block selected in the last second
    // pass (g_p2CmdWeapon, from CG_SubmitInputAndView: weaponSelect after a weapon cycle, the
    // alt id of the slot, the offhand), when that weapon is in player 2's list (ps+0x248..,
    // 15 x 0x1c); a weapon the server put in hand on its own (wall buy, mystery box: ps+0x1b8
    // changes without a cycle) becomes the selection, as CG_TransitionPlayerState does.
    const uint8_t* cl0 = (const uint8_t*)g_p2.savedCl;
    if (cl0) {
        uint32_t n0 = *(const uint32_t*)(cl0 + shadowcl::kClCmdNumber);
        const UsercmdView* c0 =
            (const UsercmdView*)(cl0 + shadowcl::kClCmds + (n0 & shadowcl::kClCmdMask) * shadowcl::kClCmdSize);
        u->weapon = c0->weapon;
        u->offhand = c0->offhand;
        u->w3 = c0->w3;
    }
    int ownWeapon = ReadIniInt(L"p2_own_weapon", 2);
    if (g_p2.cl && ownWeapon) {
        const uint8_t* ps = g_p2.cl + engine::kClPlayerState;
        int w = engine::BG_GetWeapon(ps);
        if (w) u->weapon = w;
        if (ownWeapon >= 2) {
            static int lastPsWeapon;
            int psWeapon = *(const int*)(ps + engine::kPsWeapon);
            if (psWeapon != lastPsWeapon) {
                lastPsWeapon = psWeapon;
                // p_srvcmd_hold_ms (1500): right after the server's own switch ("h 68" when the
                // perk bottle or the build tool is taken away) the playerState shows another
                // weapon for a moment (47, for 30 to 60 ms after the switch); following it would
                // drop the server's choice and leave the player on the pistol. For that long
                // the selection stays the server's weapon and the usercmd keeps asking for it.
                bool held = g_srvWeapon[0] && g_p2CmdWeapon[0] == g_srvWeapon[0] && psWeapon != g_srvWeapon[0] &&
                            GetTickCount() - g_srvWeaponTick[0] < (DWORD)ReadIniInt(L"p_srvcmd_hold_ms", 1500);
                if (held)
                    Log("[p2] weapon: ps weapon %d right after the server's switch to %d, selection kept", psWeapon,
                        g_srvWeapon[0]);
                else if (psWeapon && g_p2CmdWeapon[0] != psWeapon) {
                    Log("[p2] weapon: ps weapon %d (selected %d) -> selection follows", psWeapon, g_p2CmdWeapon[0]);
                    P2CgSetWeaponSelect(psWeapon);
                    g_p2CmdWeapon[0] = psWeapon;
                }
            }
            bool inList = false;
            for (int i = 0; i < 15 && !inList; ++i)
                inList = *(const int*)(ps + 0x248 + i * 0x1c) == g_p2CmdWeapon[0];
            if (g_p2CmdWeapon[0] && inList) {
                static int logged;
                if (u->weapon != g_p2CmdWeapon[0] && logged < 40) {
                    ++logged;
                    Log("[p2] weapon: usercmd asks %d (ps %d, offhand %d alt %d)", g_p2CmdWeapon[0], w,
                        g_p2CmdWeapon[1], g_p2CmdWeapon[2]);
                }
                u->weapon = g_p2CmdWeapon[0];
                u->offhand = g_p2CmdWeapon[1];
                u->w3 = g_p2CmdWeapon[2];
            }
        }
    }
}
volatile bool g_p2WeapNextPending;
volatile bool g_p2WeapPrevPending, g_viewWeapPrev[3];

static void P2PushUsercmd() {
    // Before the first snapshot the server only needs a monotonic time to run
    // SV_ClientEnterWorld; afterwards follow the snapshot server time.
    int snapTime = *(int*)(g_p2.cl + shadowcl::kClSnapServerTime);
    int t;
    if (snapTime > 0) {
        t = g_p2.lastCmdTime + 1;
        if (t < snapTime) t = snapTime;
        if (t > snapTime + 200) t = snapTime + 200;
    } else {
        t = *(int*)engine::kSvTime; // server rejects command times ahead of svs.time
    }
    if (t <= g_p2.lastCmdTime) return;
    int prevCmdTime = g_p2.lastCmdTime;
    g_p2.lastCmdTime = t;
    uint32_t* cmdNumber = (uint32_t*)(g_p2.cl + shadowcl::kClCmdNumber);
    ++*cmdNumber;
    uint8_t* cmd = g_p2.cl + shadowcl::kClCmds + (*cmdNumber & shadowcl::kClCmdMask) * shadowcl::kClCmdSize;
    memset(cmd, 0, shadowcl::kClCmdSize);
    *(int*)cmd = t; // serverTime
    // Frame time for the look integration from the performance counter: GetTickCount steps
    // in 15.6 ms, so at 60 fps the per-frame turn would alternate between 0, 15/16 and 31 ms
    // worth of rotation (uneven turning with a steady stick).
    static LARGE_INTEGER lastPad, freq;
    LARGE_INTEGER nowQ;
    QueryPerformanceCounter(&nowQ);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    float dt =
        lastPad.QuadPart ? (float)((double)(nowQ.QuadPart - lastPad.QuadPart) * 1000.0 / (double)freq.QuadPart) : 0.0f;
    lastPad = nowQ;
    if (dt > 100.0f) dt = 100.0f;
    if (ReadIniInt(L"p2_input", 1)) P2FillUsercmdFromPad(cmd, dt);
    // p2_jump_step_ms: player 2's usercmds advance 1 ms per frame between snapshots and by a
    // server frame (about 50 ms) when a new snapshot arrived. A jump that starts in a 1 ms
    // command does not happen: presses that start in a step of 48 / 49 ms jump, those that
    // start in a step of 1 ms do not (about half of all presses). So the jump bit starts only
    // in a command whose step is at least this many ms; a press that came earlier waits for
    // that command (at most one server frame), also when the button is up again by then.
    // 0 = off.
    {
        static bool wantWas, sentWas, pending;
        int minStep = ReadIniInt(L"p2_jump_step_ms", 8);
        int* b = (int*)(cmd + 4);
        bool want = (*b & 0x00200000) != 0;
        if (minStep > 0) {
            if (want && !wantWas) pending = true;
            bool send = sentWas ? want : (pending && t - prevCmdTime >= minStep);
            if (send) pending = false;
            if (send)
                *b |= 0x00200000;
            else
                *b &= ~0x00200000;
            sentWas = send;
        } else {
            pending = false;
            sentWas = want;
        }
        wantWas = want;
    }
    // p2_jump_log=N (diagnostic): the next N usercmds that newly carry the jump bit, with the
    // command time step, the snapshot time and the server's playerState command time.
    {
        static int left = -1, armed = -1;
        static bool was;
        {
            int v = ReadIniInt(L"p2_jump_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        bool now = (*(const int*)(cmd + 4) & 0x00200000) != 0;
        if (now && !was && left > 0) {
            --left;
            Log("[p2] jump cmd: time %d (step %d), snapshot time %d, svs.time %d, snapshot ps commandTime %d", t,
                t - prevCmdTime, snapTime, *(const int*)engine::kSvTime,
                *(const int*)(g_p2.cl + engine::kClPlayerState));
        }
        was = now;
    }
}

// ProcessClientLoopbackPackets has register arguments (compiler-chosen): EDI = localClientNum,
// ESI = msg_t* initialised with MSG_Init(msg, buffer, 0x10000), exactly as its only caller
// (0x006c44c0) sets them up.
static uint8_t g_shadowMsgBuf[0x10000];
static uint8_t g_shadowMsg[0x30];
static __declspec(naked) void CallProcessLoopbackLc0() {
    __asm {
        push esi
        push edi
        xor edi, edi
        lea esi, g_shadowMsg
        mov eax, 0x006c4430
        call eax
        pop edi
        pop esi
        ret
    }
}
static void ShadowProcessLoopback() {
    engine::MSG_Init(g_shadowMsg, g_shadowMsgBuf, 0x10000);
    CallProcessLoopbackLc0();
}

// Drops everything queued in a loopback channel (0x026fc990, stride 0x54): channel 1 is
// player 2's inbox, channel 4 the server's. Stale packets from an ended match would otherwise
// be delivered into the next one.
static void ShadowDrainLoopback(int channel) {
    uint8_t from[32];
    int n = 0;
    for (; n < 4000; ++n) {
        engine::MSG_Init(g_shadowMsg, g_shadowMsgBuf, 0x10000);
        if (!engine::GetLoopbackPacket(channel, from, g_shadowMsg)) break;
    }
    if (n) Log("[p2] loopback channel %d: dropped %d stale packets", channel, n);
}

static void ExtraEnter(ExtraClient& x) {
    x.inContext = true;
    g_pxCtx = &x;
    x.savedCl = *(uint32_t*)engine::kClPtr;
    x.savedClc = *(uint32_t*)engine::kClcPtr;
    memcpy(x.savedUiState, (void*)engine::kUiState, shadowcl::kUiStateSize);
    memcpy(x.savedCtrlMap, (void*)engine::kCtrlMap, shadowcl::kCtrlMapSize);
    *(uint32_t*)engine::kClPtr = (uint32_t)x.cl;
    *(uint32_t*)engine::kClcPtr = (uint32_t)x.clc;
    memcpy((void*)engine::kUiState, x.uiState, shadowcl::kUiStateSize);
    memcpy((void*)engine::kCtrlMap, x.ctrlMap, shadowcl::kCtrlMapSize);
    *(int*)engine::kQportBase += 2 + x.index;
}
static void ExtraLeave(ExtraClient& x) {
    memcpy(x.uiState, (void*)engine::kUiState, shadowcl::kUiStateSize);
    memcpy(x.ctrlMap, (void*)engine::kCtrlMap, shadowcl::kCtrlMapSize);
    *(uint32_t*)engine::kClPtr = x.savedCl;
    *(uint32_t*)engine::kClcPtr = x.savedClc;
    memcpy((void*)engine::kUiState, x.savedUiState, shadowcl::kUiStateSize);
    memcpy((void*)engine::kCtrlMap, x.savedCtrlMap, shadowcl::kCtrlMapSize);
    *(int*)engine::kQportBase -= 2 + x.index;
    x.inContext = false;
    g_pxCtx = nullptr;
}
static void ExtraStopAll(const char* why) {
    for (ExtraClient& x : g_px) {
        if (x.inContext) ExtraLeave(x);
        if (!x.started) continue;
        x.started = false;
        Log("[p%d] shadow client stopped: %s", 3 + x.index, why);
        if (ReadIniInt(L"p2_drain_loopback", 1)) ShadowDrainLoopback(2 + x.index);
    }
}
// Called outside any context (player 1's pointers in place). Mirrors P2AllocateClient and
// P2StartConnect without the cgame part.
static bool ExtraStart(ExtraClient& x, int index) {
    const uint32_t* cl0 = (const uint32_t*)*(uint32_t*)engine::kClPtr;
    static const int slot[6] = {0x0, 0x3, 0x6, 0x9, 0xc, 0xd};
    uint32_t n = cl0[0xe], nClients = cl0[7];
    x.index = index;
    if (!x.cl) {
        x.cl = (uint8_t*)ShadowAlloc(shadowcl::kClSize);
        x.clc = (uint8_t*)ShadowAlloc(shadowcl::kClcSize);
        if (!x.cl || !x.clc) return false;
        x.bufs[0] = (uint32_t)ShadowAlloc(n * 0x80);
        x.bufs[1] = (uint32_t)ShadowAlloc(n * 0xc1c0);
        x.bufs[2] = (uint32_t)ShadowAlloc(nClients * 0x120);
        x.bufs[3] = (uint32_t)ShadowAlloc(n * 0xc80);
        x.bufs[4] = (uint32_t)ShadowAlloc(n * 0xc);
        x.bufs[5] = (uint32_t)ShadowAlloc(n * 0x2a48);
        for (uint32_t b : x.bufs)
            if (!b) return false;
    }
    memset(x.cl, 0, shadowcl::kClSize);
    memcpy(x.cl, cl0, 0x40);
    for (int i = 0; i < 6; ++i)
        ((uint32_t*)x.cl)[slot[i]] = x.bufs[i];
    if (ReadIniInt(L"p2_drain_loopback", 1)) ShadowDrainLoopback(2 + index);
    const uint8_t* clc0 = (const uint8_t*)*(uint32_t*)engine::kClcPtr;
    uint64_t xuid0 = _strtoui64((const char*)engine::kXuid0, nullptr, 16);
    _snprintf_s(x.xuid, sizeof(x.xuid), _TRUNCATE, "%I64x", (xuid0 ? xuid0 : 0x0110000100000001ULL) + 2 + index);
    memset(x.uiState, 0, sizeof(x.uiState));
    *(uint32_t*)(x.uiState + 0) = 3;
    *(uint32_t*)(x.uiState + 8) = 4; // CA_CONNECTING
    *(uint32_t*)(x.uiState + 0x10) = 3;
    memcpy(x.ctrlMap, (const void*)engine::kCtrlMap, sizeof(x.ctrlMap));
    *(uint32_t*)(x.ctrlMap + 0) = 1;
    *(uint32_t*)(x.ctrlMap + 4) = 0;
    *(uint32_t*)(x.ctrlMap + 8) = 0;
    *(uint32_t*)(x.ctrlMap + 0x10) = 2 + index; // loopback channel
    memset(x.clc, 0, shadowcl::kClcSize);
    uint32_t* clc = (uint32_t*)x.clc;
    clc[0] = (uint32_t)(*(int*)engine::kQportBase + 2 + index);
    memcpy(x.clc + 0x10, clc0 + 0x10, 0x14);
    clc[9] = (uint32_t)-99999;
    x.lastCmdTime = 0;
    x.lastLoggedState = 4;
    x.nextDebug = 0;
    x.pitch = x.yaw = 0.0f;
    x.lastPad.QuadPart = 0;
    x.crouch = x.stanceWas = x.weapNextWas = x.weapPrevWas = x.cycled = x.jumpWantWas = x.jumpSentWas = x.jumpPending =
        x.weaponSettled = false;
    x.loggedWeapon = -1;
    x.started = true;
    Log("[p%d] connecting: xuid '%s', qport %u, netsrc %d", 3 + index, x.xuid, clc[0], 2 + index);
    return true;
}
// Input of an extra player from its device (p3_pad / p4_pad: a pad or keyboard and mouse),
// look and move as player 2 (ReadPlayerInput), buttons through the layout of controls.cpp
// with a state of its own (stance, action slots). No scoreboard or menu.
static void ExtraFillUsercmd(ExtraClient& x, uint8_t* cmd, float dtMs, int step) {
    PlayerInput in;
    ReadPlayerInput(2 + x.index, dtMs, &in);
    float lx = in.lx, ly = in.ly;
    x.yaw += in.yawStep;
    x.pitch += in.pitchStep;
    if (x.pitch > 85.0f) x.pitch = 85.0f;
    if (x.pitch < -85.0f) x.pitch = -85.0f;
    int buttons = 0, buttons2 = 0;
    ShadowControlsButtons(1 + x.index, in.buttons, in.lt, in.rt, in.kbmActs, &buttons, &buttons2);
    DevApplyInput(1 + x.index, &ly, &lx);
    {
        bool down = (buttons2 & kWeapNextPseudoBit) != 0;
        buttons2 &= ~kWeapNextPseudoBit;
        if (down && !x.weapNextWas) {
            g_viewWeapNext[1 + x.index] = true;
            x.cycled = true;
        }
        x.weapNextWas = down;
        bool prevDown = (buttons2 & kWeapPrevPseudoBit) != 0;
        buttons2 &= ~kWeapPrevPseudoBit;
        if (prevDown && !x.weapPrevWas) {
            g_viewWeapPrev[1 + x.index] = true;
            x.cycled = true;
        }
        x.weapPrevWas = prevDown;
    }
    if (ShadowControlsTakeWeaponChange(1 + x.index))
        x.cycled = true; // action slot: the usercmd follows the block's selection
    if (InterlockedExchange(&g_srvWeaponSwitched[1 + x.index], 0))
        x.cycled = true; // the server's switch (perk bottle): the same
    // The jump bit starts only in a command with a long enough time step (see p2_jump_step_ms).
    {
        int minStep = ReadIniInt(L"p2_jump_step_ms", 8);
        bool want = (buttons & 0x00200000) != 0;
        if (minStep > 0) {
            if (want && !x.jumpWantWas) x.jumpPending = true;
            bool send = x.jumpSentWas ? want : (x.jumpPending && step >= minStep);
            if (send) x.jumpPending = false;
            if (send)
                buttons |= 0x00200000;
            else
                buttons &= ~0x00200000;
            x.jumpSentWas = send;
        } else {
            x.jumpPending = false;
            x.jumpSentWas = want;
        }
        x.jumpWantWas = want;
    }
    UsercmdView* u = (UsercmdView*)cmd;
    cmd[0x27] = (uint8_t)(int8_t)(in.ry * 127.0f);
    cmd[0x28] = (uint8_t)(int8_t)(in.rx * 127.0f);
    {
        int ks = x.index == 1 ? 2 : 1; // view slot of this extra client (player 3 = slot 1, player 4 = slot 2)
        u->angles[0] = Angle2Short(x.pitch + g_viewKick[ks][0]);
        u->angles[1] = Angle2Short(x.yaw + g_viewKick[ks][1]);
    }
    u->angles[2] = 0;
    u->forwardmove = (int8_t)(ly * 127.0f);
    u->rightmove = (int8_t)(lx * 127.0f);
    u->buttons = buttons;
    u->buttons2 = buttons2;
}
static void ExtraPushUsercmd(ExtraClient& x) {
    int snapTime = *(int*)(x.cl + shadowcl::kClSnapServerTime);
    int t;
    if (snapTime > 0) {
        t = x.lastCmdTime + 1;
        if (t < snapTime) t = snapTime;
        if (t > snapTime + 200) t = snapTime + 200;
    } else
        t = *(int*)engine::kSvTime;
    if (t <= x.lastCmdTime) return;
    int prevCmdTime = x.lastCmdTime;
    x.lastCmdTime = t;
    uint32_t* cmdNumber = (uint32_t*)(x.cl + shadowcl::kClCmdNumber);
    ++*cmdNumber;
    uint8_t* cmd = x.cl + shadowcl::kClCmds + (*cmdNumber & shadowcl::kClCmdMask) * shadowcl::kClCmdSize;
    memset(cmd, 0, shadowcl::kClCmdSize);
    *(int*)cmd = t;
    {
        static LARGE_INTEGER freq;
        LARGE_INTEGER nowQ;
        QueryPerformanceCounter(&nowQ);
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        float dt = x.lastPad.QuadPart
                       ? (float)((double)(nowQ.QuadPart - x.lastPad.QuadPart) * 1000.0 / (double)freq.QuadPart)
                       : 0.0f;
        x.lastPad = nowQ;
        if (dt > 100.0f) dt = 100.0f;
        if (ReadIniInt(L"p_extra_input", 1)) ExtraFillUsercmd(x, cmd, dt, t - prevCmdTime);
    }
    if (*(const uint32_t*)(x.cl + engine::kClSnapshot)) {
        const uint8_t* ps = x.cl + engine::kClPlayerState;
        UsercmdView* u = (UsercmdView*)cmd;
        int w = engine::BG_GetWeapon(ps);
        // The list's first entry is not the start pistol (list 46 47 79, player 2 holds 47);
        // with 46 in hand the view shows no weapon model. Until the extra player has held the
        // weapon of player 1's latest usercmd once, the usercmd asks for that weapon when the
        // extra player owns it; after that for whatever its own playerState holds.
        if (!x.weaponSettled) {
            const uint8_t* cl0 = (const uint8_t*)x.savedCl; // player 1's cl while this context is switched in
            int want = 0;
            if (cl0) {
                uint32_t n0 = *(const uint32_t*)(cl0 + shadowcl::kClCmdNumber);
                want =
                    ((const UsercmdView*)(cl0 + shadowcl::kClCmds + (n0 & shadowcl::kClCmdMask) * shadowcl::kClCmdSize))
                        ->weapon;
            }
            bool owned = false;
            for (int i = 0; i < 15 && want && !owned; ++i)
                owned = *(const int*)(ps + 0x248 + i * 0x1c) == want;
            if (owned) {
                if (w == want)
                    x.weaponSettled = true;
                else
                    w = want;
            }
        }
        u->weapon = w;
        // After a weapon cycle of this player (pad Y): the ids its cgame block selected in its
        // last pass, when that weapon is in its list (as p2_own_weapon=2 does for player 2).
        if (x.cycled) {
            const int* sel = g_viewCmdWeapon[1 + x.index];
            bool inList = false;
            for (int i = 0; i < 15 && sel[0] && !inList; ++i)
                inList = *(const int*)(ps + 0x248 + i * 0x1c) == sel[0];
            if (inList) {
                u->weapon = sel[0];
                u->offhand = sel[1];
                u->w3 = sel[2];
            }
        }
        if (u->weapon != x.loggedWeapon) {
            x.loggedWeapon = u->weapon;
            char list[160] = "";
            int n = 0;
            for (int i = 0; i < 15; ++i)
                n += _snprintf_s(list + n, sizeof(list) - n, _TRUNCATE, " %d", *(const int*)(ps + 0x248 + i * 0x1c));
            Log("[p%d] usercmd asks for weapon %d (ps weapon %d, list%s)", 3 + x.index, u->weapon,
                *(const int*)(ps + engine::kPsWeapon), list);
        }
    }
}
// Called from P2Frame after player 2's own context switch is over.
static void ExtraFrames() {
    int want = ExtraWanted();
    uint8_t* svClients = *(uint8_t**)engine::kSvClients;
    DWORD now = GetTickCount();
    for (int i = 0; i < want; ++i) {
        ExtraClient& x = g_px[i];
        // One after the other, each a few seconds after the one before: player 2 first.
        if (!x.started) {
            if (*(const uint32_t*)(g_p2.uiState + 8) < 9 || (LONG)(now - g_p2.startedTick) < 4000 * (i + 1)) break;
            if (i > 0 && *(const uint32_t*)(g_px[i - 1].uiState + 8) < 9) break;
            static bool failed[2];
            if (failed[i]) break;
            if (!ExtraStart(x, i)) {
                failed[i] = true;
                Log("[p%d] allocation failed", 3 + i);
                break;
            }
        }
        ExtraEnter(x);
        ShadowProcessLoopback();
        engine::CL_CheckForResend(0);
        uint32_t state = *(uint32_t*)(engine::kUiState + 8);
        if (state >= 9) ExtraPushUsercmd(x);
        engine::CL_SendCmd(0);
        ExtraLeave(x);
        state = *(uint32_t*)(x.uiState + 8);
        if (state != x.lastLoggedState) {
            Log("[p%d] connstate %u (%s) -> %u (%s)", 3 + i, x.lastLoggedState, ConnStateName(x.lastLoggedState), state,
                ConnStateName(state));
            x.lastLoggedState = state;
        }
        if ((LONG)(now - x.nextDebug) >= 0) {
            x.nextDebug = now + 3000;
            const float* ps = (const float*)(x.cl + engine::kClPlayerState);
            char slots[64] = "";
            int sn = 0;
            for (int c = 0; c < 4 && svClients; ++c)
                sn += _snprintf_s(slots + sn, sizeof(slots) - sn, _TRUNCATE, " %d",
                                  *(const int*)(svClients + c * shadowcl::kSvClientSize));
            if (TraceOn())
                Log("[p%d] state %u (%s), snap valid %u time %d, clientNum %d, origin %.1f %.1f %.1f, cmdNumber %u, "
                    "server slot states%s",
                    3 + i, state, ConnStateName(state), *(const uint32_t*)(x.cl + engine::kClSnapshot),
                    *(const int*)(x.cl + shadowcl::kClSnapServerTime),
                    *(const int*)(x.cl + engine::kClPlayerState + engine::kPsClientNum), ps[10], ps[11], ps[12],
                    *(const uint32_t*)(x.cl + shadowcl::kClCmdNumber), slots);
        }
    }
    // No Steam auth result offline: mark the extra clients' server slots as authorized too.
    if (want > 0 && svClients)
        for (int c = 2; c < 4; ++c)
            if (*(const int*)(svClients + c * shadowcl::kSvClientSize) >= 3)
                svClients[c * shadowcl::kSvClientSize + shadowcl::kSvClientSteamAuthorized] = 1;
}

void ShadowStop(const char* why) {
    ExtraStopAll(why);
    if (g_p2.inContext) P2Leave();
    g_p2.started = false;
    Log("[p2] shadow client stopped: %s", why);
    if (ReadIniInt(L"p2_drain_loopback", 1)) {
        ShadowDrainLoopback(1);
        ShadowDrainLoopback(4);
    }
}

static void P2Frame() {
    if (!g_p2.started) return;
    // Only while player 1 is in game on a running listen server; otherwise the map is
    // changing or the game ended and the engine tears down client/server state.
    bool svRunning = engine::Dvar_GetBool(*(void**)engine::kSvRunningDvar) != 0;
    if (*(uint32_t*)(engine::kUiState + 8) != 10 || !svRunning) {
        ShadowStop(svRunning ? "player 1 left the game" : "server not running");
        return;
    }
    P2Enter();
    ShadowProcessLoopback();
    engine::CL_CheckForResend(0);
    uint32_t state = *(uint32_t*)(engine::kUiState + 8);
    if (state >= 9) P2PushUsercmd();
    engine::CL_SendCmd(0);
    P2Leave();
    // The listen server kicks clients that never get a Steam auth result (offline:
    // never). Mark server slot 1 as authorized; slot 0 is the host.
    uint8_t* svClients = *(uint8_t**)engine::kSvClients;
    if (svClients && *(int*)(svClients + shadowcl::kSvClientSize) >= 3)
        svClients[shadowcl::kSvClientSize + shadowcl::kSvClientSteamAuthorized] = 1;
    state = *(uint32_t*)(g_p2.uiState + 8);
    static DWORD nextDebug;
    DWORD now = GetTickCount();
    if ((LONG)(now - nextDebug) >= 0) {
        nextDebug = now + 3000;
        const uint32_t* clc = (const uint32_t*)g_p2.clc;
        if (*(uint32_t*)(g_p2.cl + engine::kClSnapshot)) {
            // Trace output: player 2's snapshot playerState has its origin at +0x28; the view
            // angles are searched by value to show where the server stores them.
            const float* ps = (const float*)(g_p2.cl + engine::kClPlayerState);
            float yaw = fmodf(g_p2Yaw, 360.0f);
            if (yaw < 0) yaw += 360.0f;
            float yawN = yaw - 360.0f;
            char hits[160];
            int hn = 0;
            for (int i = 0; i < 0x40 && hn < 150; ++i) {
                float f = ps[i];
                if (fabsf(f - yaw) < 0.05f || fabsf(f - yawN) < 0.05f)
                    hn += _snprintf_s(hits + hn, sizeof(hits) - hn, _TRUNCATE, " yaw@+%03x", i * 4);
                if (fabsf(g_p2Pitch) > 1.0f && fabsf(f - g_p2Pitch) < 0.05f)
                    hn += _snprintf_s(hits + hn, sizeof(hits) - hn, _TRUNCATE, " pitch@+%03x", i * 4);
            }
            if (TraceOn())
                Log("[p2] ps origin %.1f %.1f %.1f, my yaw %.1f pitch %.1f ->%s", ps[10], ps[11], ps[12], yaw,
                    g_p2Pitch, hits);
        }
        if (TraceOn())
            Log("[p2] state %u (%s), connectPackets %u, svtime %d, netchan in %u, srvMsgSeq %u, snap valid %u time %d "
                "msgNum %u, cmdNumber %u, svslot1 state %d",
                state, ConnStateName(state), clc[10], *(int*)engine::kSvTime, clc[0x24350 / 4 + 3], clc[0x4340 / 4],
                *(uint32_t*)(g_p2.cl + engine::kClSnapshot), *(int*)(g_p2.cl + shadowcl::kClSnapServerTime),
                *(uint32_t*)(g_p2.cl + 0x60), *(uint32_t*)(g_p2.cl + shadowcl::kClCmdNumber),
                svClients ? *(int*)(svClients + shadowcl::kSvClientSize) : -1);
    }
    if (state != g_p2.lastLoggedState) {
        const uint32_t* clc = (const uint32_t*)g_p2.clc;
        Log("[p2] connstate %u (%s) -> %u (%s): challenge %u, serverId %u, snap serverTime %d, cmdNumber %u",
            g_p2.lastLoggedState, ConnStateName(g_p2.lastLoggedState), state, ConnStateName(state), clc[75],
            *(uint32_t*)(g_p2.cl + shadowcl::kClServerId), *(int*)(g_p2.cl + shadowcl::kClSnapServerTime),
            *(uint32_t*)(g_p2.cl + shadowcl::kClCmdNumber));
        g_p2.lastLoggedState = state;
    }
    ExtraFrames();
}

typedef void(__cdecl* ClFrame_t)(int, int);
static ClFrame_t oClFrame;
static void __cdecl HookClFrame(int lc, int msec) {
    // Com_Error longjmps out of the frame; if that happened inside P2Frame the engine
    // would keep running with player 2's pointers. Restore before anything else.
    if (g_p2.inContext || g_pxCtx) ShadowStop("context still active at CL_Frame start");
    oClFrame(lc, msec);
    if (lc == 0) DevAfterClFrame();
    if (lc == 0 && g_p2.enabled) P2Frame();
}
static const uint8_t kProClFrame[] = {0x53, 0x8b, 0x1d, 0x18, 0x7e, 0x0e, 0x01};

// CL_InitCGame(lc): the gamestate for the shadow client must not start a second cgame
// (it would re-run map/cgame init on client 0's cgame). Jump straight to CA_PRIMED.
typedef void(__cdecl* InitCGame_t)(int);
static InitCGame_t oInitCGame;
static void __cdecl HookInitCGame(int lc) {
    if (g_p2.inContext || g_pxCtx) {
        *(uint32_t*)(engine::kUiState + 8) = 9;
        *(uint32_t*)(engine::kUiState + 0) |= 0x30;
        Log("[p2] CL_InitCGame skipped for the shadow client, state -> PRIMED (9)");
        return;
    }
    oInitCGame(lc);
}
static const uint8_t kProInitCGame[] = {0x83, 0xec, 0x44, 0xa1, 0x48, 0x2a, 0x48, 0x02};

// Com_Error(code, fmt, ...) longjmps out of the frame. If it fires inside P2Frame the
// player-1 pointers must be back in place before the error path runs.
static void* oComError;
// regs: [0] eflags, [1] edi, [2] esi, [3] ebp, [4] esp, [5] ebx, [6] edx, [7] ecx, [8] eax,
// [9] return address, [10] code, [11] format.
static void __cdecl OnComError(const uint32_t* regs) {
    char chain[200] = "";
    __try {
        const uint32_t* ebp = (const uint32_t*)regs[3];
        size_t n = 0;
        for (int i = 0; i < 8 && ebp && n < sizeof(chain) - 12; ++i) {
            n += (size_t)_snprintf_s(chain + n, sizeof(chain) - n, _TRUNCATE, " %08x", ebp[1]);
            ebp = (const uint32_t*)ebp[0];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    const char* fmt = (const char*)regs[11];
    __try {
        Log("[p2] Com_Error code %u from %08x: %s | chain%s", regs[10], regs[9], fmt ? fmt : "(null)", chain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (g_p2.inContext || g_pxCtx)
        ShadowStop("Com_Error inside the shadow client frame");
    else if (g_p2.started)
        ShadowStop("Com_Error");
}
static __declspec(naked) void HookComError() {
    __asm {
        pushad
        pushfd
        push esp
        call OnComError
        add esp, 4
        popfd
        popad
        jmp dword ptr [oComError]
    }
}
static const uint8_t kProComError[] = {0x53, 0x56, 0x33, 0xdb, 0x53};

// IsPrimaryLocalClient(lc) (0x00966230) folded to lc == 0: in the player-2 context the cgame
// must not run the primary-only paths (renderer/sound/UI resets in CG_Init).
typedef char(__cdecl* IsPrimary_t)(int);
static IsPrimary_t oIsPrimary;
static char __cdecl HookIsPrimary(int lc) {
    return (g_p2.inContext || g_pxCtx) ? 0 : oIsPrimary(lc);
}
static const uint8_t kProIsPrimary[] = {0x33, 0xc0, 0x39, 0x44, 0x24, 0x04};

// Script builtin getnumexpectedplayers (0x0061c300): in a session-mode game it counts the
// connected server clients, otherwise the party members (at least 1). The zombies scripts
// spawn everybody who is in by the time this many players are connected; later arrivals
// spectate until the round ends. With the shadow client enabled the answer is at least 2,
// so player 2 is spawned in round 1 with player 1. Replaced whole (no trampoline call).
static void* oGetNumExpectedPlayers;
static void __cdecl HookGetNumExpectedPlayers() {
    int n = 0;
    if (((char(__cdecl*)(int))0x006c31e0)(0)) {
        int maxClients = engine::DvarIntValue(*(void**)engine::kMaxClientsDvar);
        const uint8_t* cl = *(const uint8_t**)engine::kSvClients;
        for (int i = 0; i < maxClients; ++i)
            if (*(const int*)(cl + i * 0x4e180) > 1) ++n;
    } else {
        n = engine::Party_CountMembers(engine::kParty);
        if (n < 1) n = 1;
    }
    int want = (g_p2.enabled && ReadIniInt(L"p2_expected", 1) && !(LobbyJoinOn() && LobbyJoined() == 0))
                   ? 2 + ExtraWanted()
                   : 0;
    static int calls;
    ++calls;
    if (calls <= 20 || ((calls % 200) == 0 && TraceOn()))
        Log("[p2] getnumexpectedplayers: engine %d -> %d (call %d)", n, n < want ? want : n, calls);
    if (n < want) n = want;
    engine::Scr_AddInt(0, n);
}
static const uint8_t kProGetNumExpectedPlayers[] = {0x56, 0x33, 0xf6, 0x56, 0xe8};

const Detour kShadowClientHooks[] = {
    {"getnumexpectedplayers (>= 2 with shadow client)", 0x0061c300, kProGetNumExpectedPlayers,
     sizeof(kProGetNumExpectedPlayers), (void*)HookGetNumExpectedPlayers, &oGetNumExpectedPlayers},
    {"IsPrimaryLocalClient (0 in shadow context)", 0x00966230, kProIsPrimary, sizeof(kProIsPrimary),
     (void*)HookIsPrimary, (void**)&oIsPrimary},
    {"Com_Error (shadow client reset)", 0x006c3990, kProComError, sizeof(kProComError), (void*)HookComError,
     &oComError},
    {"CL_InitCGame (skip for shadow client)", 0x005465d0, kProInitCGame, sizeof(kProInitCGame), (void*)HookInitCGame,
     (void**)&oInitCGame},
    {"CL_Frame (shadow client driver)", shadowcl::kClFrame, kProClFrame, sizeof(kProClFrame), (void*)HookClFrame,
     (void**)&oClFrame},
    {"Live_GetXuidForController", shadowcl::kXuidForController, kProXuidForController, sizeof(kProXuidForController),
     (void*)HookXuidForController, (void**)&oXuidForController},
};
