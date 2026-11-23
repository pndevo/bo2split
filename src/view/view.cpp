#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/ui.h"
#include "engine/render.h"
#include "view/view_private.h"

// p2_view=1: split the screen with the engine's own multi-viewport loop.
// UpdateScreenRenderLocalViews (0x0055c100) counts active local clients, sets
// r_num_viewports and draws each view via DrawActiveFrameLocalClient0 (EAX = lc) with
// the viewport switch (0x0041c800) in between. Its three loops are bounded by
// MAX_LOCAL_CLIENTS = 1 ("cmp ebp,1" at 0x0055c3f2 / 0x0055c480 / 0x0055c58e); they are
// patched to 4 and IsLocalClientActive(lc) reports true for lc < g_viewCount while that
// function runs (two views by default, up to four with p_views / extra_players).
volatile bool g_inRenderViews;
volatile unsigned g_renderFrames;
PerfTimes g_perf;
// p2_frame_log=N: log N frames with every pass (cgs rect at pass begin, refdef at submit, pass
// count at the end of the view loop) to find frames that render full-screen.
int g_frameLogLeft = -1;
volatile bool g_viewOverride;
volatile bool g_vpHook;
static volatile bool g_vpLog;
volatile int g_vpLogLeft;
typedef char(__cdecl* IsLocalClientActive_t)(int);
static IsLocalClientActive_t oIsLocalClientActive;
static char __cdecl HookIsLocalClientActive(int lc) {
    if (lc >= 1 && g_inRenderViews && ReadIniInt(L"p2_view", 1)) return lc < g_viewCount;
    if (lc >= 2) return 0; // the engine's table has one slot; only the patched view loops ask
    return oIsLocalClientActive(lc);
}
static const uint8_t kProIsLocalClientActive[] = {0x8b, 0x44, 0x24, 0x04, 0x69, 0xc0, 0x58, 0x04, 0x00, 0x00};

typedef void(__cdecl* RenderViews_t)();
static RenderViews_t oRenderViews;
volatile int g_viewPass;
// View passes of this frame. p_views: 0 = automatic (2, plus one per player 3 and 4 that is to
// join the match: extra_players or the lobby), 2..4 = fixed (a pass without its own client shows
// player 2's state).
// 1 = player 1 alone (lobby_join with nobody joined).
volatile int g_viewCount = 2;
void ShadowViewFractions(int pass, float fr[4]) {
    fr[0] = 0.0f;
    fr[1] = 0.0f;
    fr[2] = 1.0f;
    fr[3] = 1.0f;
    if (pass < 0 || g_viewCount < 2) return; // one view: the whole screen
    // layout_three_players=1: three views as one wide view over two quarters (player 1 across the top).
    if (g_viewCount == 3 && ReadIniInt(L"layout_three_players", 1) == 1) {
        fr[3] = 0.5f;
        if (pass > 0) {
            fr[0] = pass == 2 ? 0.5f : 0.0f;
            fr[1] = 0.5f;
            fr[2] = 0.5f;
        }
    } else if (g_viewCount > 2) {
        fr[0] = (pass & 1) ? 0.5f : 0.0f;
        fr[1] = (pass & 2) ? 0.5f : 0.0f;
        fr[2] = 0.5f;
        fr[3] = 0.5f;
    } else if (ReadIniInt(L"split_vertical", 1)) {
        fr[0] = pass ? 0.5f : 0.0f;
        fr[2] = 0.5f;
    } else {
        fr[1] = pass ? 0.5f : 0.0f;
        fr[3] = 0.5f;
    }
}
void ShadowViewRect(int pass, int W, int H, int r[4]) {
    float fr[4];
    ShadowViewFractions(pass, fr);
    r[0] = (int)((float)W * fr[0]);
    r[1] = (int)((float)H * fr[1]);
    r[2] = (int)((float)W * fr[2]);
    r[3] = (int)((float)H * fr[3]);
}
uint8_t* ShadowViewCl() {
    if (g_viewPass >= 2) {
        uint8_t* c = ExtraCl(g_viewPass - 2);
        if (c) return c;
    }
    return g_p2.cl;
}
static void LogCmdBuffers(const char* where);
static void __cdecl HookRenderViews() {
    g_viewPass = 0;
    if (ReadIniInt(L"p2_view", 1)) {
        // The buffer used for the first view is issued at the mid-frame viewport switch but
        // never reset by R_EndFrame (which resets the second view's buffer); it has been
        // consumed by the time the next frame starts.
        uint32_t* cb = *(uint32_t**)engine::kCmdBuffer;
        cb[1] = 0;
        cb[2] = 0;
        cb[3] = 0;
    }
    g_viewOverride = ReadIniInt(L"p2_view", 1) != 0;
    g_vpHook = ReadIniInt(L"p2_vp_hook", 1) != 0;
    g_padReportedInactive = P1EnginePad() < 0 ? 0 : -1; // player 1 without a pad: the engine's pad 0 reads inactive
    g_vpLog = ReadIniInt(L"p2_vp_log", 0) != 0;
    static bool vpLogArmed;
    if (g_vpLog && !vpLogArmed) {
        vpLogArmed = true;
        g_vpLogLeft = 400;
    }
    if (g_frameLogLeft < 0 && g_p2.started && ReadIniInt(L"p2_frame_log", 0)) {
        static DWORD since;
        if (!since) since = GetTickCount();
        if ((LONG)(GetTickCount() - since) > 20000) g_frameLogLeft = ReadIniInt(L"p2_frame_log", 0);
    }
    {
        int n = ReadIniInt(L"p_views", 0);
        if (n < 2) {
            // The views of players 3 and 4 are there from the moment player 2 is, before those
            // players have connected (their passes show player 2's state until then). A layout
            // that changed with every join would resize player 2's HUD root from the upright
            // half to a quarter, and its HUD keeps the placement of the shape it had first:
            // points and ammo 245 pixels off the edge at 1920 x 1080.
            n = 2;
            if (g_p2.started) {
                for (int i = 0; i < 2 && ExtraCl(i); ++i)
                    n = 3 + i;
                if (n < 2 + ExtraWanted()) n = 2 + ExtraWanted();
            }
        }
        if (n > 4) n = 4;
        // Players joined in the lobby and nobody did: player 1 alone, one view over the whole screen.
        if (LobbyJoinOn() && LobbyJoined() == 0 && !g_p2.started) n = 1;
        if (n != g_viewCount) Log("[view] %d view passes per frame (was %d)", n, (int)g_viewCount);
        g_viewCount = n;
    }
    g_inRenderViews = true;
    oRenderViews();
    LogCmdBuffers("end");
    if (g_frameLogLeft > 0) {
        --g_frameLogLeft;
        Log("[frame] end: %d passes", g_viewPass);
    }
    g_inRenderViews = false;
}
static const uint8_t kProRenderViews[] = {0xa1, 0x14, 0x55, 0x4c, 0x03, 0x83, 0xec, 0x1c};

// Per view pass inside UpdateScreenRenderLocalViews. The viewport switch (0x0041c800)
// between the views clears the skin-cache "available" word (0x03942d50) that R_BeginFrame
// (0x0041eae0) set; re-arm it for the second view exactly as R_BeginFrame does.
static void* oDrawActiveFrame;
static void LogCmdBuffers(const char* where) {
    static DWORD next[5];
    int slot = strcmp(where, "end") == 0 ? 4 : (g_viewPass & 3);
    DWORD t = GetTickCount();
    if ((LONG)(t - next[slot]) < 0) return;
    next[slot] = t + 1000;
    const uint32_t* cb = *(const uint32_t**)engine::kCmdBuffer;
    Log("[view] %-6s pass %d cmdbuf %p: data %08x used %06x [2] %06x [3] %08x size %06x soft %06x [6] %u | skin %08x",
        where, g_viewPass, cb, cb[0], cb[1], cb[2], cb[3], cb[4], cb[5], cb[6],
        *(uint32_t*)engine::kSkinCacheAvailable);
}
// p2_scrplace=1: each pass gets the engine's per-client viewport setup for its half. The
// engine function CG_SetupViewport(lc, viewportIndex, viewportCount, fractions) (0x00522de0)
// stores the viewport rect in cgs (+0..+0xc, aspect +0x10, fractions +0x14..+0x20) and
// rebuilds the ScrPlace view placement (0x011e2950, 0x7c bytes) through
// ScrPlace_SetupViewport(place, x, y, w, h, aspect, flag) (0x0057e7d0), so refdef (from cgs),
// the HUD and the crosshair land inside the half. CG_SetupViewport also resizes the LUI root
// (0x00931990) on every call; called twice per frame that would overflow a buffer after ~30 s
// (memcpy crash), so only the cgs fields and the ScrPlace call are done here. The PC build's
// fraction table only has the single-viewport entry. After the second pass the full-screen
// setup is restored for the UI pass that follows the view loop (UpdateScreenRenderLocalViews
// calls CG_SetupViewports (0x00546290) once per frame before the loop).
static bool g_scrPlaceSplit;
static void ShadowSetupViewport(int pass) {
    static const uint8_t expect[] = {0x8b, 0x44, 0x24, 0x1c, 0xf3, 0x0f, 0x10, 0x44, 0x24, 0x18};
    static int checked;
    if (!checked) {
        checked = BytesMatch(engine::kScrPlace_SetupViewport, expect, sizeof(expect)) ? 1 : -1;
        if (checked < 0) Log("[view] ScrPlace_SetupViewport prologue mismatch, p2_scrplace disabled");
    }
    if (checked < 0) return;
    int* cgs = *(int**)engine::kCgs;
    if (!cgs) return;
    float fr[4];
    ShadowViewFractions(pass, fr);
    int W = *(int*)engine::kVidWidth, H = *(int*)engine::kVidHeight;
    float aspect = *(float*)engine::kVidAspect;
    cgs[0] = (int)((float)W * fr[0]);
    cgs[1] = (int)((float)H * fr[1]);
    cgs[2] = (int)((float)W * fr[2]);
    cgs[3] = (int)((float)H * fr[3]);
    ((float*)cgs)[4] = fr[2] * aspect / fr[3];
    // +0x14..+0x20: the fractions as floats (the LUI draw, 0x009315c0, restores the 2D viewport from them).
    for (int i = 0; i < 4; ++i)
        ((float*)cgs)[5 + i] = fr[i];
    engine::ScrPlace_SetupViewport((void*)engine::kScrPlaceView0, cgs[0], cgs[1], cgs[2], cgs[3], aspect, 0);
    // A half that is narrower than 4:3 (left/right split) gets scaleVirtualToReal.y twice
    // scaleVirtualToReal.x from the engine (480 virtual rows stretched over the full height);
    // p2_scrplace_uniform=1 uses the x scale for y as well, except for the full-screen scale
    // ([3]): elements aligned to the full screen (the low-health overlay) keep covering the
    // whole height. ScrPlace_ApplyY (0x0057eb60)
    // anchors: [0x15]/[0x17] viewable min/max, [0x19]/[0x1b] adjustable min/max (real
    // pixels, stay spread over the full height), [0xf] viewport y + [0x11] height for
    // centred elements, [0x1d] subscreen offset for the default alignment (centred here).
    float* pl = (float*)engine::kScrPlaceView0;
    if (pass >= 0 && ReadIniInt(L"p2_scrplace_uniform", 0) && pl[0] < pl[1]) {
        pl[1] = pl[0];
        pl[5] = pl[4];
        pl[0x1d] = (pl[0x11] - 480.0f * pl[1]) * 0.5f;
    }
    // The LUI HUD (points, ammo, round) is drawn from the per-controller LUI root, which is
    // laid out for its own size (full screen) and projected onto the current viewport: in a
    // half it is squeezed. LUI_SetRootSize(controller) (0x00931990) re-lays it out to the
    // viewport from the cgs fractions; it costs Lua stack space per call (twice per frame it
    // would overflow after ~30 s), so it runs once when the split starts, for controller 0 with
    // the first half's size, and the root stays that size for the match (p2_lui_root=1).
    // A second call within one match doubles the HUD elements (every ammo/points element is
    // drawn twice), so the resize repeats only when player 2
    // connected anew (new match; the roots are rebuilt with the map).
    // p2_lui_resize=1: the resize also repeats when the half's size changed (another
    // resolution or split direction during the match; the root would stay laid out for the old
    // half and show small and squeezed, for example at 1920 x 1080 after a start at 800 x 600).
    static DWORD rootMatch;
    static bool rootHalf;
    static int rootW, rootH;
    if (pass == 0 && ReadIniInt(L"p2_lui_root", 1)) {
        if (rootHalf && rootMatch != g_p2.startedTick) rootHalf = false;
        if (rootHalf && (rootW != cgs[2] || rootH != cgs[3]) && ReadIniInt(L"p2_lui_resize", 1)) rootHalf = false;
        if (!rootHalf && *(const uint8_t*)engine::kLuiRoot0Active) {
            {
                engine::LuiLock lock; // the resize runs Lua without taking the state's critical section
                engine::LUI_SetRootSize(0);
            }
            rootHalf = true;
            rootMatch = g_p2.startedTick;
            rootW = cgs[2];
            rootH = cgs[3];
            Log("[view] LUI root 0 resized to %d x %d", cgs[2], cgs[3]);
        }
    }
    g_scrPlaceSplit = pass >= 0;
    static int logged[2];
    if (pass >= 0 && logged[pass & 1] < 1) {
        ++logged[pass & 1];
        Log("[view] setup viewport pass %d: cgs rect %d %d %d %d aspect %.3f | place scaleV2R %.3f %.3f V2F %.3f %.3f "
            "R2V %.3f %.3f realViewable %.1f %.1f %.1f %.1f virtViewable %.1f %.1f %.1f %.1f vpPos %.1f %.1f vpSize "
            "%.1f %.1f subLeft %.1f %.1f [1e] %.3f",
            pass, cgs[0], cgs[1], cgs[2], cgs[3], ((const float*)cgs)[4], pl[0], pl[1], pl[2], pl[3], pl[4], pl[5],
            pl[6], pl[7], pl[8], pl[9], pl[10], pl[11], pl[12], pl[13], pl[14], pl[15], pl[16], pl[17], pl[28], pl[29],
            pl[30]);
    }
}

// p2_snap_diff=N (diagnostic): N reports, at most two a second, that compare the entities of
// player 1's current snapshot with those of player 2's (both taken from the client structs:
// cl+0x50 = current snapshot, +8 serverTime, +0x2a28 entity count, +0x2a38 first index into
// the parse-entity ring cl+0xc with cl+0x10 entries of 0xf8 bytes, as CL_GetSnapshot
// 0x005450d0 reads them). Only snapshots of the same server time are compared. The report
// lists the entities only one player got and, for the shared ones, which dwords differ.
static void P2SnapDiff() {
    static int left = -1, armed = -1;
    {
        int v = ReadIniInt(L"p2_snap_diff", 0);
        if (v != armed) {
            armed = v;
            left = v;
        }
    }
    if (left <= 0) return;
    static DWORD next;
    if ((LONG)(GetTickCount() - next) < 0) return;
    const uint8_t* cl[2] = {*(const uint8_t**)engine::kClPtr, g_p2.cl};
    if (!cl[0] || !cl[1] || !*(const uint32_t*)(cl[0] + engine::kClSnapshot) ||
        !*(const uint32_t*)(cl[1] + engine::kClSnapshot))
        return;
    if (*(const int*)(cl[0] + shadowcl::kClSnapServerTime) != *(const int*)(cl[1] + shadowcl::kClSnapServerTime))
        return;
    next = GetTickCount() + 500;
    --left;
    static const uint32_t* ent[2][0x800];
    memset(ent, 0, sizeof(ent));
    int count[2];
    for (int p = 0; p < 2; ++p) {
        const uint8_t* snap = cl[p] + engine::kClSnapshot;
        int n = *(const int*)(snap + 0x2a28), first = *(const int*)(snap + 0x2a38);
        const uint8_t* ring = *(const uint8_t* const*)(cl[p] + 0xc);
        int ringCount = *(const int*)(cl[p] + 0x10);
        count[p] = n;
        if (!ring || ringCount <= 0 || n < 0 || n > 0x400) {
            Log("[snap] player %d: bad ring %p / %d / count %d", p + 1, ring, ringCount, n);
            return;
        }
        for (int i = 0; i < n; ++i) {
            const uint32_t* es = (const uint32_t*)(ring + (size_t)((first + i) % ringCount) * 0xf8);
            if (es[0] < 0x800) ent[p][es[0]] = es;
        }
    }
    const float* o1 = (const float*)(cl[0] + engine::kClPlayerState + engine::kPsOrigin);
    const float* o2 = (const float*)(cl[1] + engine::kClPlayerState + engine::kPsOrigin);
    Log("[snap] time %d: player 1 %d entities at %.0f %.0f %.0f, player 2 %d entities at %.0f %.0f %.0f",
        *(const int*)(cl[0] + shadowcl::kClSnapServerTime), count[0], o1[0], o1[1], o1[2], count[1], o2[0], o2[1],
        o2[2]);
    char line[420];
    size_t len;
    for (int p = 0; p < 2; ++p) {
        len = 0;
        line[0] = 0;
        int only = 0;
        for (int i = 0; i < 0x800; ++i) {
            const uint32_t* es = ent[p][i];
            if (!es || ent[1 - p][i]) continue;
            ++only;
            len += (size_t)_snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %d(t%u m%u)", i,
                                       es[0xd8 / 4] & 0xffff, es[0x88 / 4] & 0xffffff);
            if (len > 320) {
                Log("[snap] only player %d:%s", p + 1, line);
                len = 0;
                line[0] = 0;
            }
        }
        if (len) Log("[snap] only player %d:%s", p + 1, line);
        Log("[snap] only player %d: %d entities", p + 1, only);
    }
    int hist[0x3e] = {};
    int differing = 0, shared = 0, listed = 0;
    for (int i = 0; i < 0x800; ++i) {
        const uint32_t* a = ent[0][i];
        const uint32_t* b = ent[1][i];
        if (!a || !b) continue;
        ++shared;
        len = 0;
        line[0] = 0;
        int d = 0;
        for (int k = 0; k < 0x3e; ++k) {
            if (a[k] == b[k]) continue;
            ++d;
            ++hist[k];
            if (d <= 8)
                len += (size_t)_snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %02x:%08x/%08x", k * 4, a[k],
                                           b[k]);
        }
        if (!d) continue;
        ++differing;
        if (listed < 30) {
            ++listed;
            Log("[snap] entity %d (t%u m%u) differs in %d dwords:%s", i, a[0xd8 / 4] & 0xffff, a[0x88 / 4] & 0xffffff,
                d, line);
        }
    }
    len = 0;
    line[0] = 0;
    for (int k = 0; k < 0x3e; ++k)
        if (hist[k]) len += (size_t)_snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %02x:%d", k * 4, hist[k]);
    Log("[snap] shared %d, differing %d; offsets:%s", shared, differing, line);
}

// A dead player 1 watches another player: the playerState in its snapshot is then a copy of
// the watched player's (clientNum that player's, otherFlags with kPofFollow). The cgame block
// of a shadow player uses player 1's snapshot, so in its pass the watched player would count
// as the one this view belongs to: a living shadow player that is not the watched one saw
// its own body and no weapon. For the time of a shadow pass the two fields say what they say
// while player 1 lives: player 1's own number, no following.
static struct {
    uint8_t* at;
    int num, flags;
} g_snapFollow[2];
static void SnapFollowHide(uint8_t* cg) {
    const uint8_t* p1 = ShadowPlayer1Cg();
    if (!p1 || g_snapFollow[0].at || g_snapFollow[1].at) return;
    // The cgame keeps two snapshots: cg+0x24 and the current one.
    uint8_t* snaps[2] = {*(uint8_t**)(cg + engine::kCgSnap), *(uint8_t**)(cg + 0x24)};
    if (snaps[1] == snaps[0]) snaps[1] = nullptr;
    for (int i = 0; i < 2; ++i) {
        uint8_t* snap = snaps[i];
        if (!snap) continue;
        int* flags = (int*)(snap + 0x10 + engine::kPsOtherFlags);
        if (!(*flags & engine::kPofFollow)) continue;
        int* num = (int*)(snap + 0x10 + engine::kPsClientNum);
        g_snapFollow[i].at = snap;
        g_snapFollow[i].num = *num;
        g_snapFollow[i].flags = *flags;
        *num = *(const int*)p1;
        *flags = 4;
    }
}
static void SnapFollowRestore() {
    for (auto& s : g_snapFollow) {
        if (!s.at) continue;
        *(int*)(s.at + 0x10 + engine::kPsClientNum) = s.num;
        *(int*)(s.at + 0x10 + engine::kPsOtherFlags) = s.flags;
        s.at = nullptr;
    }
}
static void __cdecl OnDrawPassBegin() {
    if (!g_inRenderViews) return;
    if (g_viewPass == 0) ++g_renderFrames;
    // perf_log: where a frame's time goes on this thread. The time from one pass begin to the
    // next is that pass; from the last pass begin to the next frame's first is the last pass
    // plus everything else in the frame (server, sound, waiting for the backend).
    {
        static LARGE_INTEGER prev;
        static int prevPass = -1;
        static LONGLONG frameStart;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        int pass = g_viewPass;
        if (prevPass >= 0 && prevPass < 4) {
            LONGLONG d = now.QuadPart - prev.QuadPart;
            if (pass == 0)
                g_perf.tail += d;
            else
                g_perf.pass[prevPass] += d;
        }
        if (pass == 0) {
            if (frameStart) {
                LONGLONG f = now.QuadPart - frameStart;
                if (f > g_perf.maxFrame) g_perf.maxFrame = f;
                if (f > g_perf.slowLimit) ++g_perf.slowFrames;
            }
            frameStart = now.QuadPart;
        }
        prev = now;
        prevPass = pass;
    }
    if (g_viewPass == 0 && g_p2.started) P2SnapDiff();
    LogCmdBuffers(g_viewPass == 0 ? "pass0" : "pass1");
    if (ReadIniInt(L"p2_view", 1) && ReadIniInt(L"p2_scrplace", 1)) ShadowSetupViewport(g_viewPass);
    if (g_frameLogLeft > 0) {
        const int* cgs = *(const int**)engine::kCgs;
        Log("[frame] pass %d begin: cgs %d %d %d %d p2started %d snapvalid %u", g_viewPass, cgs ? cgs[0] : -1,
            cgs ? cgs[1] : -1, cgs ? cgs[2] : -1, cgs ? cgs[3] : -1, (int)g_p2.started,
            g_p2.cl ? *(uint32_t*)(g_p2.cl + engine::kClSnapshot) : 0u);
    }
    if (g_viewPass >= 1) {
        uint8_t* fe = *(uint8_t**)engine::kFrontendData;
        // More than two views: the two command buffers alternate per pass, so from the third
        // pass on a pass gets a buffer an earlier pass of this frame filled (issued mid-frame
        // and drawn by now: the issue in between waited for it). Its write position is reset
        // as HookRenderViews does for the frame's first buffer; otherwise the old commands
        // would run again.
        if (g_viewCount > 2 && g_viewPass >= 2) {
            uint32_t* cb = *(uint32_t**)engine::kCmdBuffer;
            cb[1] = 0;
            cb[2] = 0;
            cb[3] = 0;
        }
        *(uint32_t*)engine::kSkinCacheRearm = 1;
        *(uint32_t*)engine::kSkinCacheAvailable = *(uint32_t*)(*(uint32_t*)(fe + 0x474198) + 0xc);
        uint8_t* cg = *(uint8_t**)engine::kCg;
        // The swap starts as soon as player 2 has a snapshot, so its view shows its own state
        // from the first frame of the match. p2_ps_after_ms delays it (until then the view
        // shows player 1's state and hands).
        static DWORD p2Since;
        static bool p2WasStarted;
        if (g_p2.started != p2WasStarted) {
            p2WasStarted = g_p2.started;
            p2Since = GetTickCount();
            g_cgResetPending = true;
        }
        bool swapDue = g_p2.started && (LONG)(GetTickCount() - p2Since) >= ReadIniInt(L"p2_ps_after_ms", 0);
        // p2_ps_diff=1: once (after the delay) log every dword that differs between player 1's
        // predicted playerState and player 2's snapshot playerState (the fields that matter
        // for weapon/HUD and the ones that break rendering when swapped).
        static bool diffed;
        if (cg && swapDue && !diffed && *(uint32_t*)(g_p2.cl + engine::kClSnapshot) && ReadIniInt(L"p2_ps_diff", 0)) {
            diffed = true;
            const uint32_t* a = (const uint32_t*)(cg + engine::kCgPredictedPs);
            const uint32_t* b = (const uint32_t*)(g_p2.cl + engine::kClPlayerState);
            char line[400];
            size_t n = 0;
            int count = 0;
            for (size_t i = 0; i < engine::kPlayerStateSize / 4; ++i) {
                if (a[i] == b[i]) continue;
                ++count;
                n += (size_t)_snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %03x:%08x/%08x", (unsigned)(i * 4),
                                         a[i], b[i]);
                if (n > 300) {
                    Log("[p2] ps diff%s", line);
                    n = 0;
                    line[0] = 0;
                }
            }
            if (n) Log("[p2] ps diff%s", line);
            Log("[p2] ps diff: %d dwords differ of %u", count, (unsigned)(engine::kPlayerStateSize / 4));
        }
        if (cg && swapDue && *(uint32_t*)(ShadowViewCl() + engine::kClSnapshot) && ReadIniInt(L"p2_ps", 1)) {
            ShadowCgSwapIn(cg, g_viewPass - 1);
            ShadowSwapPs(cg);
            if (g_viewPass >= 1 && g_viewPass <= 3) ShadowHudSecondPass(cg);
            {
                static int beArmed = -1;
                int v = ReadIniInt(L"p2_be_log", 0);
                if (v != beArmed) {
                    beArmed = v;
                    g_beLogLeft = v;
                }
            } // re-armed when the ini value changes
            // cg->clientNum (cg+0) decides which player entity is the local one (not drawn
            // in first person); player 2's own body must not show in its view.
            {
                static int extraLogged[4];
                if (g_viewPass >= 2 && extraLogged[g_viewPass & 3] < 2) {
                    ++extraLogged[g_viewPass & 3];
                    const float* ps = (const float*)(cg + engine::kCgPredictedPs);
                    Log("[view] pass %d shows player %d (%s): clientNum %d origin %.1f %.1f %.1f", g_viewPass,
                        g_viewPass + 1,
                        ShadowViewCl() == g_p2.cl ? "player 2's state, no client of its own" : "its own shadow client",
                        *(const int*)(ShadowViewCl() + engine::kClPlayerState + engine::kPsClientNum), ps[10], ps[11],
                        ps[12]);
                }
            }
            if (ReadIniInt(L"p2_cg_clientnum", 1)) {
                g_savedCgClientNum = *(int*)cg;
                *(int*)cg = *(const int*)(ShadowViewCl() + engine::kClPlayerState + engine::kPsClientNum);
            }
            SnapFollowHide(cg);
            static int logged;
            if (g_viewPass == 1 && logged < 3) {
                ++logged;
                const int* ps = (const int*)(cg + engine::kCgPredictedPs);
                Log("[p2] ps swap: commandTime %d pm_type %d origin %.1f %.1f %.1f weapon@+0x%x", ps[0], ps[1],
                    ((const float*)ps)[10], ((const float*)ps)[11], ((const float*)ps)[12], 0);
            }
        }
    }
}
static void __cdecl OnDrawPassEnd() {
    if (!g_inRenderViews) return;
    // p2_vm_trace: a pass that added no viewmodel logs what CG_AddViewWeapon's conditions
    // (0x005393e0) see: the weapon of the predicted playerState, the predicted entity's
    // eFlags (bits 0x300 suppress it), ps+0x47c (3 suppresses it), the viewmodel DObj.
    {
        static int armed = -1;
        int v = ReadIniInt(L"p2_vm_trace", 0);
        if (v != armed) {
            armed = v;
            g_vmTraceLeft = v;
        }
    }
    if (g_vmTraceLeft > 0 && g_vmAdds == 0 && g_p2.started) {
        --g_vmTraceLeft;
        const uint8_t* cg = *(const uint8_t**)engine::kCg;
        const uint8_t* ps = cg + engine::kCgPredictedPs;
        const uint8_t* vm = *(const uint8_t**)engine::kViewModel;
        Log("[vm] no add in pass %d swapped %d: ps weapon %d, pm_type %d, predicted entity eFlags %08x, ps+0x47c %d, "
            "ps+0x10 %08x, viewmodel dobj %p, built for weapon %d, origin %.0f %.0f %.0f",
            g_viewPass, (int)g_cgSwapped, *(const int*)(ps + engine::kPsWeapon), *(const int*)(ps + 4),
            *(const uint32_t*)(cg + engine::kCgPredictedEnt + 0x1e0), *(const int*)(ps + 0x47c),
            *(const uint32_t*)(ps + 0x10), vm ? *(void* const*)vm : nullptr, *(const int*)(cg + 0x696c4),
            ((const float*)ps)[10], ((const float*)ps)[11], ((const float*)ps)[12]);
    }
    g_vmAdds = 0;
    if (g_viewPass >= 1 && g_psSwapped) {
        uint8_t* cg = *(uint8_t**)engine::kCg;
        ShadowRestoreSnapPs();
        SnapFollowRestore();
        if (cg) memcpy(cg + engine::kCgPredictedPs, g_savedPs, engine::kPlayerStateSize);
        if (cg && g_savedCgClientNum >= 0) {
            *(int*)cg = g_savedCgClientNum;
            g_savedCgClientNum = -1;
        }
        if (cg) ShadowCgSwapOut(cg);
        g_psSwapped = false;
    }
    if (g_viewPass >= 1 && g_scrPlaceSplit) ShadowSetupViewport(-1);
    // Player 1's predicted player entity (cg+0x4aab4, filled by the prediction of pass 0)
    // for the body in the second view (AddPlayer1Body).
    if (g_viewPass == 0 && g_p2.started) {
        const uint8_t* cg = *(const uint8_t**)engine::kCg;
        if (cg) {
            memcpy(g_p1PredEnt, cg + engine::kCgPredictedEnt, engine::kCentSize);
            g_p1PredEntValid = true;
        }
    }
    WritePlayer1Pose();
    ++g_viewPass;
}
static __declspec(naked) void HookDrawActiveFrame() {
    __asm {
        pushad
        call OnDrawPassBegin
        popad
        call dword ptr [oDrawActiveFrame]
        push eax
        pushad
        call OnDrawPassEnd
        popad
        pop eax
        ret
    }
}
static const uint8_t kProDrawActiveFrame[] = {0x83, 0xec, 0x08, 0x83, 0x3d, 0x18, 0x7e, 0x0e, 0x01, 0x0a};

// R_GenerateSortedDrawSurfs(viewParms, scene, refdef, dualDesc) (0x004189a0): viewParms carries
// the scaled scene viewport at [0x395..0x398] (x, y, w, h) and the unscaled one at [0x399..0x39c];
// [0x394] = "is fullscreen". Log and force them per view pass.
typedef void(__cdecl* GenSortedSurfs_t)(int*, void*, int*, void*);
static GenSortedSurfs_t oGenSortedSurfs;
static void __cdecl HookGenSortedSurfs(int* vp, void* scene, int* refdef, void* dual) {
    if (g_inRenderViews && ReadIniInt(L"p2_view", 1)) {
        static DWORD nextLog;
        bool logNow = (LONG)(GetTickCount() - nextLog) >= 0;
        if (logNow && g_viewPass == 1) nextLog = GetTickCount() + 2000;
        if (logNow)
            Log("[view] drawsurfs pass %d: full=%d scaled %d %d %d %d unscaled %d %d %d %d (rt %d x %d screen %d x %d) "
                "viewIdx %d",
                g_viewPass, vp[0x394], vp[0x395], vp[0x396], vp[0x397], vp[0x398], vp[0x399], vp[0x39a], vp[0x39b],
                vp[0x39c], *(int*)engine::kRtWidth, *(int*)engine::kRtHeight, *(int*)engine::kScreenWidth,
                *(int*)engine::kScreenHeight, *(int*)engine::kTraceViewIdx);
        if (ReadIniInt(L"p2_force_viewport", 1)) {
            int rtw = *(int*)engine::kRtWidth, rth = *(int*)engine::kRtHeight;
            int vr[4];
            ShadowViewRect(g_viewPass, rtw, rth, vr);
            int x = vr[0], y = vr[1], w = vr[2], h = vr[3];
            *(uint8_t*)(vp + 0x394) = 0;
            vp[0x395] = x;
            vp[0x396] = y;
            vp[0x397] = w;
            vp[0x398] = h;
            vp[0x39d] = x;
            vp[0x39e] = y;
            vp[0x39f] = w;
            vp[0x3a0] = h;
        }
    }
    oGenSortedSurfs(vp, scene, refdef, dual);
    // p_view_stale=1: a shadow pass is view 1 (or higher) of its frontend buffer, and the view
    // slots below it still hold what last used them: the viewport of an earlier layout (one
    // view, two halves, the wide view of three players) while the players joined one by one.
    // The backend draws the buffer's 2D once per view slot, so it would show up a second time in
    // that old rect (player 2's score and ammo twice, the copy twice as wide: slot 0 holds
    // 0 0 1920 540 from the three-player layout). 2: the lower slots get an empty viewport at
    // the place of the pass's own view. 1: they get the viewport of the pass's own view; the 2D
    // of view n is then drawn n + 1 times on top of itself, which nobody sees on an opaque
    // element, but a translucent full-screen one (a fade, the flash of a nuke, a menu's backdrop)
    // is that many times as strong.
    if (g_inRenderViews && g_viewPass >= 1 && ReadIniInt(L"p2_view", 1)) {
        int mode = ReadIniInt(L"p_view_stale", 2);
        uint8_t* fe = *(uint8_t**)engine::kFrontendData;
        int idx = *(int*)(fe + engine::kViewIndex), cnt = *(int*)(fe + engine::kViewCount);
        static int logLeft = 8;
        if (mode && idx >= 1 && idx <= 3) {
            int* mine = (int*)(*(uint8_t**)(fe + engine::kViewData) + idx * engine::kViewDataSize);
            for (int v = 0; v < idx; ++v) {
                int* vd = (int*)(*(uint8_t**)(fe + engine::kViewData) + v * engine::kViewDataSize);
                if (logLeft > 0 && memcmp(vd + engine::kViewRectX, mine + engine::kViewRectX, 32) != 0) {
                    --logLeft;
                    Log("[view] pass %d (view %d of %d): stale view %d viewport %d %d %d %d replaced by %d %d %d %d",
                        g_viewPass, idx, cnt, v, vd[engine::kViewRectX], vd[engine::kViewRectY], vd[engine::kViewRectW],
                        vd[engine::kViewRectH], mine[engine::kViewRectX], mine[engine::kViewRectY],
                        mine[engine::kViewRectW], mine[engine::kViewRectH]);
                }
                if (mode == 2) {
                    vd[engine::kViewRectX] = mine[engine::kViewRectX];
                    vd[engine::kViewRectY] = mine[engine::kViewRectY];
                    vd[engine::kViewRectW] = 0;
                    vd[engine::kViewRectH] = 0;
                    vd[0x58] = mine[0x58];
                    vd[0x59] = mine[0x59];
                    vd[0x5a] = 0;
                    vd[0x5b] = 0;
                } else
                    memcpy(vd + engine::kViewRectX, mine + engine::kViewRectX, 32);
            }
        }
    }
    if (g_inRenderViews && ReadIniInt(L"p2_view", 1)) {
        static DWORD nextLog2;
        if ((LONG)(GetTickCount() - nextLog2) >= 0 && g_viewPass == 1) {
            nextLog2 = GetTickCount() + 2000;
            uint8_t* fe = *(uint8_t**)engine::kFrontendData;
            int idx = *(int*)(fe + engine::kViewIndex), cnt = *(int*)(fe + engine::kViewCount);
            for (int v = 0; v < 2; ++v) {
                const int* vd = (const int*)(*(uint8_t**)(fe + engine::kViewData) + v * engine::kViewDataSize);
                Log("[view] after drawsurfs (pass %d, fe idx %d cnt %d): view %d viewport %d %d %d %d | %d %d %d %d "
                    "flag968 %d client %d",
                    g_viewPass, idx, cnt, v, vd[engine::kViewRectX], vd[engine::kViewRectY], vd[engine::kViewRectW],
                    vd[engine::kViewRectH], vd[0x58], vd[0x59], vd[0x5a], vd[0x5b],
                    *(const uint8_t*)((const uint8_t*)vd + 0x968), vd[0x66]);
            }
        }
    }
}
static const uint8_t kProGenSortedSurfs[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};

// FX_SetNextUpdateCamera (0x005c9d60, cdecl(lc, camera params)): fills the FX system's "next
// update" camera (system 0x020d2864 + 0: origin, frustum planes of four floats each from
// +0x10, plane count at +0x94, 5 or 6) from the view of the pass. The hook hands each pass's
// camera to fx.cpp (culling against both cameras, p2_fx_cull). p2_fx_nocull=1 (diagnostic)
// sets the plane count to 0, so no effect is culled by a frustum; the spawn range stays.
typedef void(__cdecl* FxSetCamera_t)(int, void*);
static FxSetCamera_t oFxSetCamera;
static void __cdecl HookFxSetCamera(int lc, void* params) {
    oFxSetCamera(lc, params);
    if (g_p2.started && g_inRenderViews) {
        uint8_t* sys = *(uint8_t**)engine::kFxSystem;
        ShadowFxCaptureCamera(sys);
        if (sys && ReadIniInt(L"p2_fx_nocull", 0)) *(int*)(sys + 0x94) = 0;
    }
}
static const uint8_t kProFxSetCamera[] = {0x8b, 0x44, 0x24, 0x04, 0x56, 0x57, 0x50};

// SND_SetDuck(index, duckId, duckDef, amount) (0x0082b010): the cgame starts/updates sound
// ducks from the playerState every frame; with the swapped playerState the second pass
// alternates the duck ids each frame, every change allocates a new duck and the pool runs
// dry ("SOUND: no free sound ducks"). Sound belongs to player 1: the second pass does not
// touch the ducks (p2_skip_duck=1).
typedef void(__cdecl* SndSetDuck_t)(int, int, int, float);
static SndSetDuck_t oSndSetDuck;
static void __cdecl HookSndSetDuck(int index, int duckId, int duckDef, float amount) {
    if (g_inRenderViews && g_viewPass >= 1 && ReadIniInt(L"p2_skip_duck", 1)) return;
    oSndSetDuck(index, duckId, duckDef, amount);
}
static const uint8_t kProSndSetDuck[] = {0xf3, 0x0f, 0x10, 0x05, 0x6c, 0x3b, 0xb2, 0x00};

const Detour kViewHooks[] = {
    {"SND_SetDuck (no duck changes in the second pass)", 0x0082b010, kProSndSetDuck, sizeof(kProSndSetDuck),
     (void*)HookSndSetDuck, (void**)&oSndSetDuck},
    {"R_SetRenderTargetViewport (per-view D3D viewport)", 0x0046ad90, kProSetTargetViewport,
     sizeof(kProSetTargetViewport), (void*)HookSetTargetViewport, &oSetTargetViewport},
    {"R_IssueRenderCommands (view rect per command buffer)", 0x0041c800, kProIssueRenderCommands,
     sizeof(kProIssueRenderCommands), (void*)HookIssueRenderCommands, (void**)&oIssueRenderCommands},
    {"render thread dequeue (view rect for the backend)", 0x006dc7f0, kProDequeueRenderBuffer,
     sizeof(kProDequeueRenderBuffer), (void*)HookDequeueRenderBuffer, (void**)&oDequeueRenderBuffer},
    {"backend single-view backdrop (skipped for the first pass's buffer)", 0x00423fc0, kProSingleViewBackdrop,
     sizeof(kProSingleViewBackdrop), (void*)HookSingleViewBackdrop, (void**)&oSingleViewBackdrop},
    {"FX_SetNextUpdateCamera (no frustum culling of effects while the split runs)", 0x005c9d60, kProFxSetCamera,
     sizeof(kProFxSetCamera), (void*)HookFxSetCamera, (void**)&oFxSetCamera},
    {"RB_ResolveViews (half scissor ends before the frame's full-screen 2D)", 0x00425940, kProResolveViews,
     sizeof(kProResolveViews), (void*)HookResolveViews, (void**)&oResolveViews},
    {"RB_Draw3D (model lighting patches written before the buffer is drawn)", 0x0043c630, kProRbDraw3D,
     sizeof(kProRbDraw3D), (void*)HookRbDraw3D, (void**)&oRbDraw3D},
    {"R_GenerateSortedDrawSurfs (scene viewport per view)", 0x004189a0, kProGenSortedSurfs, sizeof(kProGenSortedSurfs),
     (void*)HookGenSortedSurfs, (void**)&oGenSortedSurfs},
    {"IsLocalClientActive (lc 1 while rendering views)", 0x00966210, kProIsLocalClientActive,
     sizeof(kProIsLocalClientActive), (void*)HookIsLocalClientActive, (void**)&oIsLocalClientActive},
    {"UpdateScreenRenderLocalViews (scope flag)", 0x0055c100, kProRenderViews, sizeof(kProRenderViews),
     (void*)HookRenderViews, (void**)&oRenderViews},
    {"DrawActiveFrameLocalClient0 (view pass counter)", 0x0055af10, kProDrawActiveFrame, sizeof(kProDrawActiveFrame),
     (void*)HookDrawActiveFrame, &oDrawActiveFrame},
};

// Render command buffers (0x034b40d0, two of 0x60000 bytes) hold one view's commands;
// with two views per frame they are replaced by buffers twice the size.
void EnlargeCommandBuffers() {
    for (int i = 0; i < 2; ++i) {
        uint32_t* e = (uint32_t*)(engine::kCmdBuffers + i * 0x1c);
        if (e[4] != 0x60000) {
            Log("[view] command buffer %d: unexpected size %08x, left alone", i, e[4]);
            continue;
        }
        void* buf = VirtualAlloc(nullptr, 0xc0000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf) return;
        e[0] = (uint32_t)buf;
        e[4] = 0xc0000;
        e[5] = 0x90000;
    }
    Log("[view] command buffers enlarged to 0xc0000");
}

const BytePatch kViewLoopPatches[] = {
    {"render views loop bound 1 -> 4 (a)",
     L"p2_view_hook",
     nullptr,
     0x0055c3f2,
     {0x83, 0xfd, 0x01},
     {0x83, 0xfd, 0x04},
     3},
    {"render views loop bound 1 -> 4 (b)",
     L"p2_view_hook",
     nullptr,
     0x0055c480,
     {0x83, 0xfd, 0x01},
     {0x83, 0xfd, 0x04},
     3},
    {"render views loop bound 1 -> 4 (c)",
     L"p2_view_hook",
     nullptr,
     0x0055c58e,
     {0x83, 0xfd, 0x01},
     {0x83, 0xfd, 0x04},
     3},
    // DrawActiveFrameLocalClient0 takes lc in EAX and derives cl from it (NULL for lc 1):
    // "mov eax, ebp" -> "xor eax, eax", so the second view also runs as client 0.
    {"second view draws as local client 0", L"p2_view_hook", nullptr, 0x0055c434, {0x8b, 0xc5}, {0x33, 0xc0}, 2},
    // After the loop the UI calls take the last loop index (EBX) as the local client; keep it 0,
    // otherwise they draw the load screen for a "client 1" over the whole frame.
    {"post-loop UI client index stays 0", L"p2_view_hook", nullptr, 0x0055c436, {0x8b, 0xdd}, {0x33, 0xdb}, 2},
    // R_InitCommandBuffers (0x0041c070) allocates two buffers of 0x60000 (soft limit 0x48000);
    // two views per frame need twice that.
    {"command buffer alloc 0x60000 -> 0xc0000",
     L"p2_view_hook",
     nullptr,
     0x0041c080,
     {0x68, 0x00, 0x00, 0x06, 0x00},
     {0x68, 0x00, 0x00, 0x0c, 0x00},
     5},
    {"command buffer size 0x60000 -> 0xc0000",
     L"p2_view_hook",
     nullptr,
     0x0041c08b,
     {0x00, 0x00, 0x06, 0x00},
     {0x00, 0x00, 0x0c, 0x00},
     4},
    {"command buffer soft limit 0x48000 -> 0x90000",
     L"p2_view_hook",
     nullptr,
     0x0041c095,
     {0x00, 0x80, 0x04, 0x00},
     {0x00, 0x00, 0x09, 0x00},
     4},
    // p2_keep_light_handle=1: the scene entity jobs (r_sceneents: 0x00409780 DObjs, 0x00409910
    // models) take each entity's lighting from the entity light cache (0x00457970; the handle is
    // a ushort at the start of the pose, reused while the position matches). For an entity the
    // pass's camera does not see they reset the handle to 0 ("mov word [reg], 0"). With two
    // passes per frame, whatever one player does not look at loses its handle every frame and
    // the other pass allocates and samples a new cache slot each frame: the brightness of
    // doors, barricade boards and player 2's hands jumps. The reset is removed; a stale handle
    // is still refused by the cache when the position or the slot no longer matches.
    {"scene DObj job keeps the light cache handle of unseen entities",
     L"p2_keep_light_handle",
     nullptr,
     0x004097e6,
     {0x66, 0x89, 0x10},
     {0x90, 0x90, 0x90},
     3},
    {"scene model job keeps the light cache handle of unseen entities",
     L"p2_keep_light_handle",
     nullptr,
     0x0040996e,
     {0x66, 0x89, 0x02},
     {0x90, 0x90, 0x90},
     3},
};
