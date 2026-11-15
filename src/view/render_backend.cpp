#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/render.h"
#include "engine/offsets.h"
#include <d3d11_1.h>

// Backend: R_SetRenderTargetViewport (0x0046ad90, ESI = state, [esp+4] = target id) sets the
// D3D11 viewport to the full target whenever the render target changes. With several views the
// current view's viewport (view data +0x150, from the frontend data the backend is executing,
// 0x0350c884) is applied after it via ID3D11DeviceContext::RSSetViewports (vtable +0xb0).
// ID3D11DeviceContext::RSSetViewports (vtable slot 44) is hooked in the context's vtable: while
// the views are drawn, every full-screen viewport (the render-target switch, the viewmodel
// depth-range switch in 0x0046b7b0 and its inline copies, ...) becomes the current view's
// rect from the backend view data (0x0350c884 + 0x474208 index, + 0x474210 views, stride 0xc50,
// rect at +0x150). Depth range is kept.
static void BeLog(const char* what, uintptr_t ret, const float* vps, const uint8_t* state);
volatile int g_beLogLeft = -1;
volatile LONG g_drawCount;
static int g_beRect[4];
static bool g_beRectValid;
static DWORD g_beThread; // render thread: the one doing the render-target switches
// The backend (render thread) cannot use the frontend view index (+0x474208): it executes
// view 1's command buffer while the frontend already builds view 2 with index 1, so hooks
// keyed on the index flicker with the timing. Instead the view rect travels with the
// buffer: R_IssueRenderCommands (0x0041c800, frontend; flags bit 1 = present) pushes the
// rect of the pass whose buffer it issues (mid-frame issue = pass 0, final = pass 1) keyed
// by the frontend data block it enqueues (the value of 0x033487d8 at the call; the two
// blocks alternate), and the render thread's dequeue (0x006dc7f0, InterlockedExchange of the
// single queue slot, returns that block) looks it up into g_beRect for the backend hooks
// (p2_be_rect=1). A plain FIFO of rects is not possible: while the render thread runs
// synchronously (loading, vid_restart) the issue executes the commands inline without an
// enqueue, so a FIFO would fill up and give every later buffer the rect of an issue eight calls
// back (all pass 1).
struct RectTag {
    volatile LONG fe;
    volatile LONG seq;
    int r[6];
}; // rect, pass, last pass of the frame
static RectTag g_rectTags[4];
static volatile int g_bePass = -1;       // pass whose buffer the render thread executes (-1 = none)
static volatile bool g_beLast;           // that pass is the frame's last one (its buffer carries the frame's own 2D)
static volatile bool g_beFirstDone;      // first screen-sized render target of this buffer handled (p2_rt_rect=5)
static volatile unsigned g_beLastTarget; // last render target id switched to
static volatile bool g_beFull2D;         // second pass's buffer is past the per-view resolve: full-screen 2D follows
static void* volatile g_beCtx;           // device context seen at the last render-target switch
IssueRenderCommands_t oIssueRenderCommands;
void __cdecl HookIssueRenderCommands(unsigned flags) {
    int r[6] = {0, 0, 0, 0, -1, 0};
    if (g_inRenderViews && g_viewOverride && ReadIniInt(L"p2_be_rect", 1)) {
        // The issue follows the pass it belongs to (the pass counter already moved on): the
        // mid-frame issues carry flags 1 / 0, the frame's last one bit 1 (present).
        bool last = (flags & 2) != 0;
        int pass = g_viewPass - 1;
        // A frame with a single view pass (menus before the split runs, the lobby, a paused
        // frame) gets no rect: its final issue would count as the second pass, and with
        // the half scissor on the backbuffer (p2_rt_rect=3) the whole screen would be cut down
        // to the second half (a black left half in the menu).
        bool split = pass >= 0 && pass <= 3 && (!last || g_viewPass >= 2);
        if (split) {
            r[4] = pass;
            r[5] = last ? 1 : 0;
            ShadowViewRect(pass, *(int*)engine::kScreenWidth, *(int*)engine::kScreenHeight, r);
        }
    }
    // p2_issue_2d=1: the backend runs the resolve to the backbuffer and the 2D command list
    // (RB_ExecuteRenderCommands tail 0x0043cb80) only when the issue flags have bit 1 (present)
    // or bit 4 (0x10) set. The mid-frame issue of the first pass carries 0/1, so its HUD, hint
    // texts and LUI draw would never run; with bit 4 they run in the first pass's own buffer. Needs
    // the half scissor on the backbuffer too (p2_rt_rect=3), or the second pass's resolve
    // covers them again.
    static int issueLog = 6;
    if (r[4] >= 0 && !r[5] && g_p2.started && ReadIniInt(L"p2_issue_2d", 1)) {
        flags |= 0x10;
        if (issueLog > 0) {
            --issueLog;
            Log("[be] first-pass issue with 2D: flags %x", flags);
        }
    }
    LONG fe = *(volatile LONG*)engine::kFrontendData;
    RectTag* t = nullptr;
    for (RectTag& c : g_rectTags)
        if (c.fe == fe) {
            t = &c;
            break;
        }
    if (!t)
        for (RectTag& c : g_rectTags)
            if (c.fe == 0) {
                t = &c;
                break;
            }
    if (!t) {
        t = &g_rectTags[0];
        for (RectTag& c : g_rectTags)
            if (c.seq < t->seq) t = &c;
    }
    InterlockedIncrement(&t->seq); // odd: being written
    t->fe = fe;
    memcpy(t->r, r, sizeof(r));
    InterlockedIncrement(&t->seq); // even: stable
    oIssueRenderCommands(flags);
}
const uint8_t kProIssueRenderCommands[] = {0xa1, 0xd8, 0x87, 0x34, 0x03, 0x53, 0x8b, 0x5c, 0x24, 0x08};
DequeueRenderBuffer_t oDequeueRenderBuffer;
LONG __cdecl HookDequeueRenderBuffer() {
    LONG v = oDequeueRenderBuffer();
    if (v) {
        int r[6] = {0, 0, 0, 0, -1, 0};
        bool found = false;
        for (RectTag& c : g_rectTags) {
            if (c.fe != v) continue;
            for (int tries = 0; tries < 8; ++tries) {
                LONG s0 = c.seq;
                if (s0 & 1) continue;
                memcpy(r, c.r, sizeof(r));
                if (c.seq == s0 && c.fe == v) {
                    found = true;
                    break;
                }
            }
            break;
        }
        if (found) {
            g_beRect[0] = r[0];
            g_beRect[1] = r[1];
            g_beRect[2] = r[2];
            g_beRect[3] = r[3];
            g_beRectValid = r[2] > 0 && r[3] > 0;
            g_bePass = r[4];
            g_beLast = r[5] != 0;
            g_beFirstDone = false;
        } else {
            g_beRectValid = false;
            g_bePass = -1;
            g_beLast = false;
        }
        g_beFull2D = false;
        // Diagnostics: the pass sequence on the render thread must alternate 0, 1, 0, 1 while
        // the split runs.
        static int lastPass = -1;
        static int oddLeft = 200;
        // The menu issues one buffer per frame (every tag pass 1), so the budget only counts
        // while the playerState swap runs (two passes per frame).
        bool odd = g_viewOverride && g_psSwapped && (!found || (g_bePass >= 0 && g_bePass == lastPass));
        if ((odd && oddLeft > 0) || g_beLogLeft > 0) {
            if (odd) --oddLeft;
            Log("[be] dequeue fe %08lx pass %d (last %d) found %d rect %d %d %d %d%s", v, g_bePass, lastPass,
                (int)found, g_beRect[0], g_beRect[1], g_beRect[2], g_beRect[3], odd ? " ODD" : "");
        }
        lastPass = g_bePass;
    }
    return v;
}
const uint8_t kProDequeueRenderBuffer[] = {0x6a, 0x00, 0x68, 0x30, 0xf7, 0x79, 0x02};
// 0x00423fc0 (render thread, first step of the resolve 0x00425940): when the frontend data
// holds exactly one view and its display rect (view data +0x160..+0x16c) is not the whole
// screen, it draws the frame image (0x03944aac) into that rect with UVs 0..1 on target 2: the
// backdrop for a single shrunken view. The first pass's buffer is such a case once its
// resolve runs (p2_issue_2d): the whole frame would show up squeezed into the first half. Skipped
// while the render thread executes the first pass's buffer (p2_skip_backdrop=1).
SingleViewBackdrop_t oSingleViewBackdrop;
void __cdecl HookSingleViewBackdrop() {
    if (g_viewOverride && g_bePass == 0 && ReadIniInt(L"p2_skip_backdrop", 1)) return;
    oSingleViewBackdrop();
}
const uint8_t kProSingleViewBackdrop[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
typedef void(__stdcall* RSSetViewports_t)(void*, unsigned, const float*);
static RSSetViewports_t oRSSetViewports;
static void __stdcall HookRSSetViewports(void* ctx, unsigned n, const float* vps) {
    if (g_vpLogLeft > 0 && vps) {
        --g_vpLogLeft;
        uint8_t* fe = *(uint8_t**)engine::kBackendData;
        int idx = fe ? *(int*)(fe + engine::kViewIndex) : -1, cnt = fe ? *(int*)(fe + engine::kViewCount) : -1;
        Log("[vp] pass %d view %d/%d: n=%u %g %g %g %g depth %g..%g from %08x", g_viewPass, idx, cnt, n, vps[0], vps[1],
            vps[2], vps[3], vps[4], vps[5], (unsigned)(uintptr_t)_ReturnAddress());
    }
    // Only the depth-range re-sends are rewritten (identified by their return address:
    // 0x0046b7b0 and the inline copies in 0x00472370, 0x00473d30, 0x004743f0, 0x00474870). They
    // carry the state rect, which the render-target switch resets to full screen; everything
    // drawn after such a switch (viewmodel, transparents, decals) would cover both views. The
    // copy in 0x00472370 (returns to 0x0047258d) runs only in frames that draw an effect of
    // the first-person weapon: with the full-screen rect the muzzle flash sat where the muzzle
    // would be on an undivided screen.
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    BeLog("rsvp", ret, vps, nullptr);
    // 0x0046b7b0 ends at 0x0046b852; its RSSetViewports call returns to 0x0046b84f (a bound
    // of 0x0046b830 would miss it and leave full-screen viewports in the second pass: a
    // flickering right half with player 1's scene elements and the weapon at the window centre).
    bool weaponFxSite = ret >= 0x00472370 && ret < 0x004727d6 && ReadIniInt(L"p_fx_weapon_rect", 1);
    bool depthRangeSite =
        (ret >= 0x0046b7b0 && ret < 0x0046b860) || (ret >= 0x00473d30 && ret < 0x00475000) || weaponFxSite;
    if (g_viewOverride && g_vpHook && n == 1 && vps && depthRangeSite) {
        int sw = *(int*)engine::kScreenWidth, sh = *(int*)engine::kScreenHeight;
        if (vps[0] == 0.0f && vps[1] == 0.0f && (int)vps[2] == sw && (int)vps[3] == sh) {
            // p_fx_weapon_own=1: the effects of a first-person weapon are drawn in every view
            // pass, in the pass's own weapon space, so player 1's muzzle flash showed at the
            // muzzle of every other player as well. Only player 1's weapon plays such effects
            // (a shadow player's shot plays the flash of a player seen from outside), so the
            // passes of the shadow players draw them into an empty viewport. The next
            // depth-range re-send restores the view's rect.
            if (weaponFxSite && g_bePass >= 1 && ReadIniInt(L"p_fx_weapon_own", 1)) {
                float none[6] = {(float)g_beRect[0], (float)g_beRect[1], 0.0f, 0.0f, vps[4], vps[5]};
                oRSSetViewports(ctx, 1, none);
                return;
            }
            if (g_beRectValid && ReadIniInt(L"p2_be_rect", 1)) {
                float vp[6] = {
                    (float)g_beRect[0], (float)g_beRect[1], (float)g_beRect[2], (float)g_beRect[3], vps[4], vps[5]};
                oRSSetViewports(ctx, 1, vp);
                return;
            }
            uint8_t* fe = *(uint8_t**)engine::kBackendData;
            if (fe) {
                int idx = *(int*)(fe + engine::kViewIndex), cnt = *(int*)(fe + engine::kViewCount);
                if (cnt >= 2 && idx >= 0 && idx <= 3) {
                    const int* vd = (const int*)(*(uint8_t**)(fe + engine::kViewData) + idx * engine::kViewDataSize);
                    if (vd[engine::kViewRectW] > 0 && vd[engine::kViewRectH] > 0 &&
                        (vd[engine::kViewRectW] != sw || vd[engine::kViewRectH] != sh)) {
                        float vp[6] = {(float)vd[engine::kViewRectX],
                                       (float)vd[engine::kViewRectY],
                                       (float)vd[engine::kViewRectW],
                                       (float)vd[engine::kViewRectH],
                                       vps[4],
                                       vps[5]};
                        oRSSetViewports(ctx, 1, vp);
                        return;
                    }
                }
            }
        }
    }
    oRSSetViewports(ctx, n, vps);
}
// ID3D11DeviceContext::ClearRenderTargetView (vtable slot 50): the second view's clear of
// the scene target wipes the first view's half; the post-process resolve then copies the
// whole target (full-screen quad) over the backbuffer, so only one half survives per frame.
// The first clears while the split runs are logged.
typedef void(__stdcall* ClearRTV_t)(void*, void*, const float*);
static ClearRTV_t oClearRTV;
static void __stdcall HookClearRTV(void* ctx, void* rtv, const float* color) {
    static int logged;
    if (g_viewOverride && logged < 60) {
        ++logged;
        Log("[be] clear rtv %p pass %d target %u color %.2f %.2f %.2f %.2f", rtv, g_bePass, g_beLastTarget,
            color ? color[0] : -1.0f, color ? color[1] : -1.0f, color ? color[2] : -1.0f, color ? color[3] : -1.0f);
    }
    oClearRTV(ctx, rtv, color);
}
// ID3D11DeviceContext::RSSetScissorRects (vtable slot 45): the engine drives the scissor test,
// so p2_rt_rect=3 limits every pass to the view half through the scissor while the viewport
// stays the full target (post-process quads keep their UVs).
typedef void(__stdcall* RSSetScissorRects_t)(void*, unsigned, const RECT*);
static RSSetScissorRects_t oRSSetScissorRects;
// The engine's own scissor (open 0 0 8192 8192 at 0x004679bf, the screen at 0x0042c2ac, ...)
// is remembered and, while the view-half scissor is active (p2_rt_rect>=3, screen-sized
// target), cut down to the half: the engine re-sending its scissor (ADS, UI clip rects, ...)
// would otherwise open the whole target again for the pass, and the present after pass 0 would
// show player 1 full-screen (flicker when aiming).
static RECT g_engScissor = {0, 0, 8192, 8192};
static volatile bool g_beScissorOn;
static void ApplyScissor(void* ctx) {
    RECT rc = g_engScissor;
    if (g_beScissorOn && g_beRectValid && ReadIniInt(L"p2_scissor_clip", 1)) {
        LONG l = g_beRect[0], t = g_beRect[1], r = g_beRect[0] + g_beRect[2], b = g_beRect[1] + g_beRect[3];
        if (rc.left < l) rc.left = l;
        if (rc.top < t) rc.top = t;
        if (rc.right > r) rc.right = r;
        if (rc.bottom > b) rc.bottom = b;
        if (rc.right < rc.left) rc.right = rc.left;
        if (rc.bottom < rc.top) rc.bottom = rc.top;
    }
    oRSSetScissorRects(ctx, 1, &rc);
}
static void __stdcall HookRSSetScissorRects(void* ctx, unsigned n, const RECT* rects) {
    if (g_viewOverride && g_beLogLeft > 0 && rects) {
        --g_beLogLeft;
        Log("[be] draws since last event: %ld", InterlockedExchange(&g_drawCount, 0));
        Log("[be] scissor n=%u %ld %ld %ld %ld pass %d on %d from %08x", n, rects[0].left, rects[0].top, rects[0].right,
            rects[0].bottom, g_bePass, (int)g_beScissorOn, (unsigned)(uintptr_t)_ReturnAddress());
    }
    if (rects && n >= 1) {
        g_engScissor = rects[0];
        if (g_viewOverride && g_beScissorOn && g_beRectValid) {
            ApplyScissor(ctx);
            return;
        }
    }
    oRSSetScissorRects(ctx, n, rects);
}
// Draw / DrawIndexed (vtable slots 13 / 12): counted between the logged backend events.
typedef void(__stdcall* Draw_t)(void*, unsigned, unsigned);
typedef void(__stdcall* DrawIndexed_t)(void*, unsigned, unsigned, int);
static Draw_t oDraw;
static DrawIndexed_t oDrawIndexed;
static void __stdcall HookDraw(void* ctx, unsigned a, unsigned b) {
    InterlockedIncrement(&g_drawCount);
    oDraw(ctx, a, b);
}
static void __stdcall HookDrawIndexed(void* ctx, unsigned a, unsigned b, int c) {
    InterlockedIncrement(&g_drawCount);
    oDrawIndexed(ctx, a, b, c);
}
// RB_ResolveViews (0x00425940, render thread): the per-view part of a buffer (resolve and the
// 2D list of each view: HUD, hints, name tags) ends here. What follows in RB_ResolveAndDraw2D
// is the frame's own 2D list (menus, pause screen, scoreboard, fps counter), drawn once for
// the whole screen from the second pass's buffer. With the half scissor still on, a menu
// would show only in the second half (the left half black). p2_full_2d=1: the half scissor ends
// when this function returns in the second pass's buffer, until the next buffer.
// Three views in quarters (layout_three_players=0): no pass draws the fourth quarter, it keeps what the
// backbuffer held (the last frame of a fourth view, the loading screen) plus every full-screen
// 2D drawn since. After the last pass's resolve the quarter is cleared to black with
// ID3D11DeviceContext1::ClearView (a clear of a rect; ClearRenderTargetView takes the whole
// target). The backbuffer's view is entry 1 of the render target table (0x03944a24, 0x14 bytes
// per id: colour view, depth view, width and height as words; R_SetRenderTargetViewport
// 0x0046ad90 binds from it). Entry 0 is empty; entry 1 is the only screen-sized one without a
// depth view next to the resolve targets. Note for the
// hooks above: state+0x534c still holds the previous target's id when OnAfterSetTargetViewport
// runs (the logged ids are one switch behind the logged sizes).
static void ClearUnusedQuarter() {
    ID3D11DeviceContext* ctx = (ID3D11DeviceContext*)g_beCtx;
    ID3D11RenderTargetView* rtv = *(ID3D11RenderTargetView**)(engine::kRenderTargetTable + 0x14);
    if (!ctx || !rtv) return;
    static ID3D11DeviceContext* known;
    static ID3D11DeviceContext1* ctx1;
    static bool logged;
    if (ctx != known) {
        if (ctx1) {
            ctx1->Release();
            ctx1 = nullptr;
        }
        known = ctx;
        HRESULT hr = ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&ctx1);
        if (FAILED(hr)) {
            ctx1 = nullptr;
            Log("[be] three views: no ID3D11DeviceContext1 (hr %08lx), the fourth quarter keeps its old content", hr);
        }
    }
    if (!ctx1) return;
    int r[4];
    ShadowViewRect(3, *(int*)engine::kScreenWidth, *(int*)engine::kScreenHeight, r);
    if (r[2] <= 0 || r[3] <= 0) return;
    static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D11_RECT rc = {r[0], r[1], r[0] + r[2], r[1] + r[3]};
    ctx1->ClearView(rtv, black, &rc, 1);
    if (!logged) {
        logged = true;
        Log("[be] three views: fourth quarter %d %d %d %d cleared to black after the last pass (view %p)", r[0], r[1],
            r[2], r[3], rtv);
    }
}
ResolveViews_t oResolveViews;
void __cdecl HookResolveViews() {
    oResolveViews();
    if (g_viewOverride && g_bePass >= 1 && g_beLast && g_viewCount == 3 && ReadIniInt(L"layout_three_players", 1) == 0)
        ClearUnusedQuarter();
    if (g_viewOverride && g_bePass >= 1 && g_beLast && ReadIniInt(L"p2_full_2d", 1)) {
        g_beFull2D = true;
        g_beScissorOn = false;
        void* ctx = g_beCtx;
        if (ctx && oRSSetScissorRects) ApplyScissor(ctx);
    }
}
const uint8_t kProResolveViews[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};

// RB_Draw3D (0x0043c630, render thread, once per issued buffer). Every new sample of the entity
// light cache (R_GetEntityLightCacheHandle 0x00457970 -> 0x0044b840) appends a patch to the
// frontend buffer ("modelLightingPatchList": count fe+0x462c68, entries of 0x384 bytes from
// fe+0xdec68, first word = cache slot). The backend writes the patches into the model lighting
// texture in 0x00457da0(list, count), called only at the end of 0x0043bb60, and that function
// runs only for an issue with flag bit 1 (present), after the buffer was drawn. A moving entity
// takes a new slot every frame, so it is always drawn with what the slot held before; in the
// split the slots of the two passes mix (one view gets the slot of something in the other
// view's room) and the patches of the first pass's buffer (issued without bit 1) would never
// be written.
// p2_light_patch_early=1: write the buffer's patches before it is drawn and clear the count.
// p2_light_log=N: N lines with the patch count per drawn buffer.
RbDraw3D_t oRbDraw3D;
void __cdecl HookRbDraw3D() {
    uint8_t* fe = *(uint8_t**)engine::kBackendData;
    if (fe) {
        int* count = (int*)(fe + 0x462c68);
        int n = *count;
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_light_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        bool early = g_p2.started && n > 0 && n <= 0x1000 && ReadIniInt(L"p2_light_patch_early", 1);
        if (left > 0) {
            --left;
            Log("[light] draw3d buffer %p flags %x pass %d: %d patches%s", fe, *(const uint32_t*)(fe + 0x4751bc),
                g_bePass, n, early ? " (written before the draw)" : "");
        }
        if (early) {
            ((void(__cdecl*)(void*, int))0x00457da0)(fe + 0xdec68, n);
            *count = 0;
        }
    }
    oRbDraw3D();
}
const uint8_t kProRbDraw3D[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
static void InstallRSSetViewportsHook(void* ctx) {
    static bool done;
    if (done || !ctx) return;
    done = true;
    {
        uintptr_t* vt = *(uintptr_t**)ctx;
        DWORD old;
        if (VirtualProtect(vt + 12, 2 * sizeof(uintptr_t), PAGE_READWRITE, &old)) {
            oDrawIndexed = (DrawIndexed_t)vt[12];
            vt[12] = (uintptr_t)HookDrawIndexed;
            oDraw = (Draw_t)vt[13];
            vt[13] = (uintptr_t)HookDraw;
            VirtualProtect(vt + 12, 2 * sizeof(uintptr_t), old, &old);
        }
    }
    {
        uintptr_t* slot = *(uintptr_t**)ctx + 45;
        DWORD old;
        if (VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old)) {
            oRSSetScissorRects = (RSSetScissorRects_t)*slot;
            *slot = (uintptr_t)HookRSSetScissorRects;
            VirtualProtect(slot, sizeof(uintptr_t), old, &old);
            Log("[view] RSSetScissorRects vtable hook installed (original %p)", (void*)oRSSetScissorRects);
        }
    }
    {
        uintptr_t* slot = *(uintptr_t**)ctx + 50;
        DWORD old;
        if (VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old)) {
            oClearRTV = (ClearRTV_t)*slot;
            *slot = (uintptr_t)HookClearRTV;
            VirtualProtect(slot, sizeof(uintptr_t), old, &old);
            Log("[view] ClearRenderTargetView vtable hook installed (original %p)", (void*)oClearRTV);
        }
    }
    uintptr_t* slot = *(uintptr_t**)ctx + 44;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &old)) {
        Log("[view] RSSetViewports hook: VirtualProtect failed");
        return;
    }
    oRSSetViewports = (RSSetViewports_t)*slot;
    *slot = (uintptr_t)HookRSSetViewports;
    VirtualProtect(slot, sizeof(uintptr_t), old, &old);
    Log("[view] RSSetViewports vtable hook installed (original %p)", (void*)oRSSetViewports);
}

void* oSetTargetViewport;
// The depth-range switch for the viewmodel (0x0046b7b0 and inline copies) re-sends the
// viewport from the state's rect (+0x5328 x, +0x532c y, +0x5330 w, +0x5334 h) with the
// depth range (+0x116c min, +0x1168 max); the rect is stored there too so that re-send
// keeps the half-screen view.
// p2_be_log=N: N backend calls (render-target switch, RSSetViewports) with the backend view
// index / count / rect, armed once the playerState swap runs, to see what the render thread
// reads while the two views are drawn (double-buffered frontend data).
static void BeLog(const char* what, uintptr_t ret, const float* vps, const uint8_t* state) {
    if (g_beLogLeft <= 0) return;
    --g_beLogLeft;
    LONG draws = InterlockedExchange(&g_drawCount, 0);
    Log("[be] draws since last event: %ld", draws);
    uint8_t* fe = *(uint8_t**)engine::kBackendData;
    int idx = fe ? *(int*)(fe + engine::kViewIndex) : -1, cnt = fe ? *(int*)(fe + engine::kViewCount) : -1;
    const int* vd = (fe && idx >= 0 && idx <= 3)
                        ? (const int*)(*(uint8_t**)(fe + engine::kViewData) + idx * engine::kViewDataSize)
                        : nullptr;
    Log("[be] %-6s tid %lu ret %08x fe %p idx %d cnt %d view rect %d %d %d %d | vps %g %g %g %g | target %u state rect "
        "%d %d %d %d",
        what, GetCurrentThreadId(), (unsigned)ret, fe, idx, cnt, vd ? vd[engine::kViewRectX] : -1,
        vd ? vd[engine::kViewRectY] : -1, vd ? vd[engine::kViewRectW] : -1, vd ? vd[engine::kViewRectH] : -1,
        vps ? vps[0] : -1.0f, vps ? vps[1] : -1.0f, vps ? vps[2] : -1.0f, vps ? vps[3] : -1.0f,
        state ? state[0x534c] : 0u, state ? *(const int*)(state + 0x5328) : -1,
        state ? *(const int*)(state + 0x532c) : -1, state ? *(const int*)(state + 0x5330) : -1,
        state ? *(const int*)(state + 0x5334) : -1);
}
static void __cdecl OnAfterSetTargetViewport(uint8_t* state) {
    if (!g_viewOverride) return;
    g_beThread = GetCurrentThreadId();
    g_beLastTarget = state[0x534c];
    g_beCtx = *(void**)(state + 0x90);
    BeLog("rtswap", 0, nullptr, state);
    // p2_rt_rect=0: the viewport after a render-target switch stays the full
    // target. The post-processing passes draw full-screen quads that map the whole source
    // target onto the viewport; with the view rect here the whole scene target (all views)
    // would be squeezed into one part (scene 256 px wide, black bar). The scene itself gets its
    // rect from the view's own viewport command, the viewmodel from the depth-range rewrite.
    int rtMode = ReadIniInt(L"p2_rt_rect", 3);
    InstallRSSetViewportsHook(*(void**)(state + 0x90));
    if (rtMode >= 3) {
        // 3: scissor = view rect on every screen-sized target, viewport stays the full target;
        // 4: same but not on the backbuffer (id 0); 5: only the first screen-sized target after
        // the buffer was dequeued (the scene target with its clear).
        int sw = *(int*)engine::kScreenWidth, sh = *(int*)engine::kScreenHeight;
        bool screenSized = *(int*)(state + 0x5330) == sw && *(int*)(state + 0x5334) == sh;
        bool want = screenSized && g_beRectValid && oRSSetScissorRects;
        if (rtMode == 4 && state[0x534c] == 0) want = false;
        if (g_beFull2D) want = false;
        if (rtMode == 5) {
            if (g_beFirstDone) want = false;
            if (screenSized) g_beFirstDone = true;
        }
        g_beScissorOn = want;
        // Re-send the engine's scissor (cut to the half while active) on every switch, so a
        // target without the half scissor does not keep the half from the previous target.
        if (oRSSetScissorRects) ApplyScissor(*(void**)(state + 0x90));
        return;
    }
    if (rtMode == 0 || (rtMode == 2 && state[0x534c] == 0))
        return; // 2: all screen-sized targets except the backbuffer (id 0)
    InstallRSSetViewportsHook(*(void**)(state + 0x90));
    if (g_vpLogLeft > 0)
        Log("[vp]   (render target switch, target %u, state rect %d %d %d %d)", state[0x534c], *(int*)(state + 0x5328),
            *(int*)(state + 0x532c), *(int*)(state + 0x5330), *(int*)(state + 0x5334));
    // Only render targets of screen size get the view rect; the small ones (shadow maps,
    // the bloom / downsample chain 512x384, 256x192, ...) keep their own full-target viewport.
    // Applying the view rect to them would shift the post-processing (black bar, offset image).
    {
        int sw = *(int*)engine::kScreenWidth, sh = *(int*)engine::kScreenHeight;
        if (ReadIniInt(L"p2_be_rect", 1) && (*(int*)(state + 0x5330) != sw || *(int*)(state + 0x5334) != sh)) return;
    }
    int rect[4];
    int idx = -1, cnt = -1;
    if (g_beRectValid && ReadIniInt(L"p2_be_rect", 1)) {
        rect[0] = g_beRect[0];
        rect[1] = g_beRect[1];
        rect[2] = g_beRect[2];
        rect[3] = g_beRect[3];
    } else {
        uint8_t* fe = *(uint8_t**)engine::kBackendData;
        if (!fe) return;
        idx = *(int*)(fe + engine::kViewIndex);
        cnt = *(int*)(fe + engine::kViewCount);
        if (cnt < 2 || idx < 0 || idx > 3) return;
        if (rect[2] <= 0 || rect[3] <= 0) return;
    }
    float vp[6] = {(float)rect[0], (float)rect[1], (float)rect[2], (float)rect[3], 0.0f, 1.0f};
    static int logged;
    if (logged < 6) {
        ++logged;
        Log("[view] target viewport: view %d/%d rect %d %d %d %d depth %.3f..%.3f state rect was %d %d %d %d", idx, cnt,
            rect[0], rect[1], rect[2], rect[3], vp[4], vp[5], *(int*)(state + 0x5328), *(int*)(state + 0x532c),
            *(int*)(state + 0x5330), *(int*)(state + 0x5334));
    }
    void* ctx = *(void**)(state + 0x90);
    if (!ctx) return;
    typedef void(__stdcall * RSSetViewports_t)(void*, unsigned, const float*);
    (*(RSSetViewports_t*)(*(uintptr_t*)ctx + 0xb0))(ctx, 1, vp);
}
__declspec(naked) void HookSetTargetViewport() {
    __asm {
        mov eax, dword ptr [esp + 4]
        push eax
        call dword ptr [oSetTargetViewport]
        add esp, 4
        pushad
        push esi
        call OnAfterSetTargetViewport
        add esp, 4
        popad
        ret
    }
}
const uint8_t kProSetTargetViewport[] = {0x0f, 0xb6, 0x86, 0x4c, 0x53, 0x00, 0x00};
