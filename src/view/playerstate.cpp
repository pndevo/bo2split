// The playerState of a shadow player in its view pass: the swap into the cgame's predicted
// playerState and back, the interpolation between snapshots, and the re-apply after the
// engine's prediction.

#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "view/view_private.h"

// p2_ps=1: for the second pass the cgame's predicted playerState (cg+0x480a8, 0x2a08 bytes:
// weapon, ammo, view angles, view height, hud elements) is player 2's latest snapshot
// playerState (cl+0x70 in the shadow client), restored after the pass. Weapon model, HUD and
// (with the engine's own view calc) the camera then belong to player 2.
uint8_t g_savedPs[engine::kPlayerStateSize];
bool g_psSwapped;
int g_savedCgClientNum = -1;
// Copies player 2's snapshot playerState over the cgame's predicted playerState (the current
// content is kept for the restore at pass end). Also used from the CG_PredictPlayerState
// hook: the snapshot transition / prediction inside CG_DrawActiveFrame overwrites the
// predicted playerState in some frames (about every new snapshot), which shows as a
// flickering camera in the second view.
// p2_interp=1: player 2's playerState for this frame. Without a usercmd replay (p2_no_replay)
// the second pass works from the snapshot playerState as the server sent it, so player 2's
// origin would only move with every snapshot (20 Hz) while the frames in between show the same
// position (stuttering when walking). The engine interpolates player 1
// between snap and nextSnap for the same reason (frameInterpolation). Here the origin (+0x28)
// is interpolated between the last two snapshot origins of the shadow client at cg.time
// (cg+0x4808c, the same server clock in both passes); everything else stays the latest
// snapshot's. p2_interp_log=N logs N frames of the fraction.
struct InterpSnap {
    int time;
    float org[3];
    float adsFrac;
    float viewHeight;
    int bobCycle;
    float vel[3];
};
// One state per view slot (0 = player 2, 1 / 2 = players 3 / 4): the last snapshots of the
// shadow client (newest at head) and the playerState of this frame.
struct InterpState {
    InterpSnap ring[4];
    int head, count;
    uint8_t ps[engine::kPlayerStateSize];
};
static InterpState g_interp[3] = {{{}, -1, 0, {}}, {{}, -1, 0, {}}, {{}, -1, 0, {}}};
const uint8_t* ShadowFramePs(const uint8_t* cg) {
    const uint8_t* vcl = ShadowViewCl();
    int slot = ViewSlot();
    InterpState& st = g_interp[slot];
    const uint8_t* ps = vcl + engine::kClPlayerState;
    if (!cg || !ReadIniInt(L"p2_interp", 1)) return ps;
    int t = *(const int*)(vcl + shadowcl::kClSnapServerTime);
    if (st.head >= 0 && t < st.ring[st.head].time) {
        st.head = -1;
        st.count = 0;
    } // server time went back: new match
    if (st.head < 0 || t != st.ring[st.head].time) {
        st.head = (st.head + 1) & 3;
        if (st.count < 4) ++st.count;
        st.ring[st.head].time = t;
        memcpy(st.ring[st.head].org, ps + engine::kPsOrigin, 12);
        st.ring[st.head].adsFrac = *(const float*)(ps + engine::kPsWeaponPosFrac);
        st.ring[st.head].viewHeight = *(const float*)(ps + engine::kPsViewHeightCurrent);
        st.ring[st.head].bobCycle = *(const int*)(ps + 8) & 0xff;
        memcpy(st.ring[st.head].vel, ps + 0x34, 12);
    }
    memcpy(st.ps, ps, engine::kPlayerStateSize);
    int now = *(const int*)(cg + engine::kCgTime);
    // The pair that brackets cg.time (cg.time usually sits between the two newest snapshots,
    // but a snapshot can arrive before cg.time has reached the previous one's time).
    const InterpSnap* a = nullptr;
    const InterpSnap* b = nullptr;
    for (int i = 0; i + 1 < st.count; ++i) {
        const InterpSnap* n = &st.ring[(st.head - i) & 3];
        const InterpSnap* o = &st.ring[(st.head - i - 1) & 3];
        if (o->time <= 0 || n->time <= o->time) break;
        a = o;
        b = n;
        if (now >= o->time) break;
    }
    if (!a) return st.ps;
    float f = (float)(now - a->time) / (float)(b->time - a->time);
    float fc = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    float* o = (float*)(st.ps + engine::kPsOrigin);
    for (int i = 0; i < 3; ++i)
        o[i] = a->org[i] + (b->org[i] - a->org[i]) * fc;
    // p2_interp_viewheight=1: the eye height (viewHeightCurrent, ps+0x208) the same way. The
    // server moves it over a stance change, and from the snapshots alone the camera of a shadow
    // player would go down and up in steps of 50 ms (crouching would look as if frames were
    // missing).
    if (ReadIniInt(L"p2_interp_viewheight", 1))
        *(float*)(st.ps + engine::kPsViewHeightCurrent) = a->viewHeight + (b->viewHeight - a->viewHeight) * fc;
    // p2_interp_bob=1: the bob cycle (ps+8, one byte that wraps at 256; the weapon and view bob
    // of walking are a sine of it) and the velocity (ps+0x34, the bob's strength) the same way.
    // From the snapshots alone the weapon of a shadow player would sway in steps of 50 ms (the
    // weapon sway when walking would look as if frames were missing). The cycle only runs forward,
    // so the step from a to b is taken modulo 256.
    if (ReadIniInt(L"p2_interp_bob", 1)) {
        int step = (b->bobCycle - a->bobCycle) & 0xff;
        *(int*)(st.ps + 8) = (*(const int*)(st.ps + 8) & ~0xff) | ((a->bobCycle + (int)((float)step * fc)) & 0xff);
        float* v = (float*)(st.ps + 0x34);
        for (int i = 0; i < 3; ++i)
            v[i] = a->vel[i] + (b->vel[i] - a->vel[i]) * fc;
    }
    // p2_interp_ads=1: the aim-down-sights fraction (fWeaponPosFrac, ps+0x1e0: 0 = hip, 1 =
    // sights) the same way. It drives the zoom (fov) and the weapon position; from the
    // snapshots alone it moves in steps of 50 ms, three or four per transition (zooming
    // in takes about three steps).
    float* ads = (float*)(st.ps + engine::kPsWeaponPosFrac);
    int adsMode = ReadIniInt(L"p2_interp_ads", 2);
    if (adsMode) *ads = a->adsFrac + (b->adsFrac - a->adsFrac) * fc;
    // p2_interp_ads=2: local prediction on top. The interpolated value starts one server
    // round trip plus one snapshot interval after the trigger (the zoom comes about half a second
    // late); player 1 has no such delay because the engine predicts its playerState. Here the
    // fraction moves from the moment LT changes (g_p2AdsHeld, the usercmd button) at the rate
    // the snapshots of this weapon showed (largest step per millisecond seen, 1/200 per ms
    // until one was seen, reset on a weapon change); the result is the value further along of
    // the local and the interpolated one. When the server still disagrees 300 ms after the
    // change (sprint, reload: it refuses to aim), the local value follows the server.
    if (adsMode >= 2 &&
        slot == 0) { // the local prediction follows player 2's LT; the extra players get the interpolated value
        static float local, rateUp = 1.0f / 200.0f, rateDown = 1.0f / 200.0f;
        static bool wasHeld;
        static int weapon;
        static double lastMs, changeMs;
        static int lastSnapTime;
        static float lastSnapFrac;
        static LARGE_INTEGER freq;
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        double nowMs = (double)q.QuadPart * 1000.0 / (double)freq.QuadPart;
        float dt = lastMs ? (float)(nowMs - lastMs) : 0.0f;
        lastMs = nowMs;
        if (dt > 100.0f) dt = 100.0f;
        int w = *(const int*)(ps + engine::kPsWeapon);
        if (w != weapon) {
            weapon = w;
            rateUp = rateDown = 1.0f / 200.0f;
            lastSnapTime = 0;
        }
        const InterpSnap* newest = &st.ring[st.head];
        if (newest->time != lastSnapTime) {
            if (lastSnapTime && newest->time > lastSnapTime) {
                float r = (newest->adsFrac - lastSnapFrac) / (float)(newest->time - lastSnapTime);
                if (r > rateUp && lastSnapFrac > 0.0f && newest->adsFrac < 1.0f)
                    rateUp = r; // full interval inside the transition
                if (-r > rateDown && lastSnapFrac < 1.0f && newest->adsFrac > 0.0f) rateDown = -r;
            }
            lastSnapTime = newest->time;
            lastSnapFrac = newest->adsFrac;
        }
        bool held = g_p2AdsHeld;
        if (held != wasHeld) {
            wasHeld = held;
            changeMs = nowMs;
        }
        local += (held ? rateUp : -rateDown) * dt;
        if (local < 0.0f) local = 0.0f;
        if (local > 1.0f) local = 1.0f;
        bool late = nowMs - changeMs > 300.0;
        float server = newest->adsFrac;
        if (late && held && server <= 0.0f) local = 0.0f;  // the server refuses to aim
        if (late && !held && server >= 1.0f) local = 1.0f; // the server keeps aiming
        // A dual wield weapon cannot aim: its left weapon fires with LT. A local value would
        // still rise with every press (the 300 ms above start again with each one), so fast
        // left fire would hold the fraction near 1.0 in player 2's view: CG_KickAngles then
        // uses the weapon's aiming values to bring the camera back, a dual wield weapon has
        // none, and the kick of every left shot would stay and pile up to the 10 degree limit
        // (a fat recoil of the left weapon only). No local value for such a weapon; the
        // interpolated server value stays.
        bool dual = w && engine::BG_IsDualWield(w);
        if (dual)
            local = 0.0f;
        else
            *ads = held ? (local > *ads ? local : *ads) : (local < *ads ? local : *ads);
    }
    static int logLeft = -1;
    {
        static int armed = -1;
        int v = ReadIniInt(L"p2_interp_log", 0);
        if (v != armed) {
            armed = v;
            logLeft = v;
        }
    }
    if (logLeft > 0) {
        --logLeft;
        Log("[p2] interp: time %d snaps %d..%d f %.2f org %.1f %.1f %.1f (raw %.1f %.1f %.1f) ads %.3f (raw %.3f)", now,
            a->time, b->time, f, o[0], o[1], o[2], st.ring[st.head].org[0], st.ring[st.head].org[1],
            st.ring[st.head].org[2], *ads, st.ring[st.head].adsFrac);
    }
    return st.ps;
}
// p_spectate_view=1: a dead shadow player watches another player. The server then sends it a
// copy of the watched player's playerState (otherFlags bit kPofFollow, clientNum the watched
// player's), and the facing of its view is the watched player's, not its own pad's. The value
// returned is what the callers add the playerState's delta_angles (+0x98) to: for player 1 its
// predicted viewangles less that delta, for a shadow player (client numbers 1 to 3) its pad
// angles and kick.
bool ShadowFollowAngles(float* pitch, float* yaw) {
    const uint8_t* vcl = ShadowViewCl();
    if (!vcl || !g_inRenderViews || g_viewPass < 1 || !ReadIniInt(L"p_spectate_view", 1)) return false;
    const uint8_t* ps = vcl + engine::kClPlayerState;
    if (!(*(const int*)(ps + engine::kPsOtherFlags) & engine::kPofFollow)) return false;
    int watched = *(const int*)(ps + engine::kPsClientNum);
    const uint8_t* p1 = ShadowPlayer1Cg();
    if (p1 && watched == *(const int*)p1) {
        const float* angles = (const float*)(p1 + engine::kCgPredictedPs + 0x1f8);
        const float* delta = (const float*)(ps + 0x98);
        *pitch = angles[0] - delta[0];
        *yaw = angles[1] - delta[1];
        return true;
    }
    int slot = watched - 1;
    if (slot == 0) {
        *pitch = g_p2Pitch + g_viewKick[0][0];
        *yaw = g_p2Yaw + g_viewKick[0][1];
        return true;
    }
    if ((slot == 1 || slot == 2) && ExtraAngles(slot - 1, pitch, yaw)) {
        *pitch += g_viewKick[slot][0];
        *yaw += g_viewKick[slot][1];
        return true;
    }
    return false;
}
static uint8_t g_snapPsSaved[engine::kPlayerStateSize];
static uint8_t* g_snapPsAt;
void ShadowSwapPs(uint8_t* cg) {
    // Player 1's playerState is saved by the first swap of the pass only: at a repeated swap
    // the predicted playerState may already be a copy of the swapped snapshot.
    if (!g_psSwapped) memcpy(g_savedPs, cg + engine::kCgPredictedPs, engine::kPlayerStateSize);
    memcpy(cg + engine::kCgPredictedPs, ShadowFramePs(cg), engine::kPlayerStateSize);
    // Fields kept from player 1: +0x15c..+0x178
    // vision-set indices (swapping them restarts the vision lerp every pass: black
    // screen), +0x1a0 clientNum.
    if (ReadIniInt(L"p2_ps_keep_vision", 1)) memcpy(cg + engine::kCgPredictedPs + 0x15c, g_savedPs + 0x15c, 0x1c);
    // The number kept is player 1's own (cg->clientNum of its block), not the one in its
    // playerState: a dead player 1 watches another player, and its playerState then carries
    // that player's number. With that number in a shadow pass the watched player would be the
    // "local" one there: the view of a living player 3 or 4 showed its own body and no weapon.
    if (ReadIniInt(L"p2_ps_keep_clientnum", 1)) {
        const uint8_t* p1 = ShadowPlayer1Cg();
        int own = p1 ? *(const int*)p1 : *(const int*)(g_savedPs + engine::kPsClientNum);
        *(int*)(cg + engine::kCgPredictedPs + engine::kPsClientNum) = own;
    }
    // p2_cam_native=1: let the cgame's own view calculation (bob, eye height, fov) work
    // from player 2's playerState; viewangles (+0x1f8 pitch, +0x1fc yaw, +0x200 roll) are
    // not sent by the server, so write pad angles + delta_angles there.
    if (ReadIniInt(L"p2_cam_native", 1)) {
        float* nps = (float*)(cg + engine::kCgPredictedPs);
        nps[0x1f8 / 4] = ViewPitch() + nps[0x98 / 4];
        nps[0x1fc / 4] = ViewYaw() + nps[0x9c / 4];
        nps[0x200 / 4] = 0.0f;
    }
    g_psSwapped = true;
}
void ShadowRestoreSnapPs() {
    if (!g_snapPsAt) return;
    memcpy(g_snapPsAt, g_snapPsSaved, engine::kPlayerStateSize);
    g_snapPsAt = nullptr;
}
// p_snap_ps=1: while the 2D part of a shadow pass is drawn (0x004b23f0, lc in EAX: the HUD
// menus are opened, closed and painted, the damage direction indicators are drawn), the
// playerState inside the cgame's snapshot (cg->snap + 0x10) is the pass's player's. The
// menus read health and state from the snapshot, not from the predicted playerState; with
// player 1's snapshot the low-health overlay of every view would follow player 1 and a hurt
// shadow player would get none. The snapshot gets its own playerState back right after the
// call: with the swapped playerState in place for the whole pass the game ends in an access
// violation (0x009bc01c, write to 0x170) when player 1 goes down.
static void __cdecl Draw2DBegin() {
    if (!g_inRenderViews || g_viewPass < 1 || !g_psSwapped || !ReadIniInt(L"p_snap_ps", 1)) return;
    uint8_t* cg = *(uint8_t**)engine::kCg;
    uint8_t* snap = cg ? *(uint8_t**)(cg + engine::kCgSnap) : nullptr;
    if (!snap) return;
    ShadowRestoreSnapPs();
    g_snapPsAt = snap + 0x10;
    memcpy(g_snapPsSaved, g_snapPsAt, engine::kPlayerStateSize);
    memcpy(g_snapPsAt, cg + engine::kCgPredictedPs, engine::kPlayerStateSize);
}
static void __cdecl Draw2DEnd() {
    ShadowRestoreSnapPs();
}
static void* oDraw2D;
static __declspec(naked) void HookDraw2D() {
    __asm {
        push eax
        call Draw2DBegin
        pop eax
        call dword ptr [oDraw2D]
        push eax
        call Draw2DEnd
        pop eax
        ret
    }
}
static const uint8_t kProDraw2D[] = {0x83, 0xec, 0x08, 0x56, 0x8b, 0x35, 0x5c, 0xf7, 0x05, 0x01};
// CG_DrawActiveFrame -> 0x00523080 (view origin from the snapshot / predicted playerState,
// runs after the snapshot processing and the prediction): in the second pass the swap is
// re-applied here when the predicted playerState no longer holds player 2's origin.
typedef void(__cdecl* CgViewOrigin_t)(int);
static CgViewOrigin_t oCgViewOrigin;
static void __cdecl HookCgViewOrigin(int lc) {
    if (g_inRenderViews && g_viewPass >= 1 && g_psSwapped && ReadIniInt(L"p2_ps_reswap", 1)) {
        uint8_t* cg = *(uint8_t**)engine::kCg;
        if (cg &&
            memcmp(cg + engine::kCgPredictedPs + engine::kPsOrigin, ShadowFramePs(cg) + engine::kPsOrigin, 12) != 0) {
            static DWORD nextLog;
            static int count;
            ++count;
            if ((LONG)(GetTickCount() - nextLog) >= 0) {
                nextLog = GetTickCount() + 1000;
                const float* a = (const float*)(cg + engine::kCgPredictedPs + engine::kPsOrigin);
                const float* b = (const float*)(ShadowFramePs(cg) + engine::kPsOrigin);
                const float* c = (const float*)(g_savedPs + engine::kPsOrigin);
                if (TraceOn())
                    Log("[p2] ps clobbered before view calc (%d so far): now %.0f %.0f %.0f, p2 %.0f %.0f %.0f, saved "
                        "p1 %.0f %.0f %.0f",
                        count, a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
            }
            ShadowSwapPs(cg);
        }
        // The prediction (0x00527900, before this call) runs PM_UpdateViewAngles with player
        // 1's usercmd on the swapped playerState in some frames: viewangles then become player 1's
        // command angles + player 2's delta_angles and the second view would alternate between the
        // two facings every other frame. Write the pad facing right before the view calc.
        if (cg && ReadIniInt(L"p2_cam_native", 1)) {
            float* nps = (float*)(cg + engine::kCgPredictedPs);
            nps[0x1f8 / 4] = ViewPitch() + nps[0x98 / 4];
            nps[0x1fc / 4] = ViewYaw() + nps[0x9c / 4];
            nps[0x200 / 4] = 0.0f;
        }
    }
    oCgViewOrigin(lc);
}
static const uint8_t kProCgViewOrigin[] = {0x56, 0x8b, 0x35, 0x5c, 0xf7, 0x05, 0x01};

const Detour kViewPsHooks[] = {
    {"CG view origin (re-apply the playerState swap after prediction)", 0x00523080, kProCgViewOrigin,
     sizeof(kProCgViewOrigin), (void*)HookCgViewOrigin, (void**)&oCgViewOrigin},
    {"2D draw of a view (the pass's playerState in the snapshot)", 0x004b23f0, kProDraw2D, sizeof(kProDraw2D),
     (void*)HookDraw2D, &oDraw2D},
};
