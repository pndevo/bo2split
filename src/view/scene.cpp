// The scene of a view pass: the scene submit, the camera of a shadow player, the frame's LOD
// origin, the actor cull against each pass's own camera, and the diagnostics around the
// renderer's scene build.

#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/render.h"
#include "view/view_private.h"

// CG_DrawView ends in 0x00545900(refdef, ...) right before the scene is
// rendered; by then refdef (cg+0x4d890) holds vieworg (+0x34), viewaxis (+0x44..+0x64) and the
// scene viewport (12 dwords at +0xe54). Each view gets its part of the screen (a half, or a
// quarter with three or four views); the views of the shadow players use their own camera when
// p2_cam=1.
typedef void(__cdecl* DrawSceneFinal_t)(int*, int, int, int);
static DrawSceneFinal_t oDrawSceneFinal;
static void __cdecl HookDrawSceneFinal(int* refdef, int a, int b, int c) {
    static bool dumped;
    int* vp = refdef + 0x395;
    if (!dumped) {
        dumped = true;
        Log("[view] refdef+0xe54 at draw: %d %d %d %d %d %d %d %d %d %d %d %d (full %d x %d)", vp[0], vp[1], vp[2],
            vp[3], vp[4], vp[5], vp[6], vp[7], vp[8], vp[9], vp[10], vp[11], refdef[2], refdef[3]);
    }
    if (g_inRenderViews && ReadIniInt(L"p2_view", 1)) {
        // refdef x/y/width/height (+0..+0xc) already come from the cgs viewport (p2_scrplace=1);
        // CG recomputes the refdef every frame.
        {
            // The engine keeps cg_fov as the horizontal fov of the half and derives the vertical
            // one from the half's aspect (very wide for a left/right split); fov_scale (percent)
            // scales both tangents, 50 = the vertical fov of the full screen.
            float k = (float)ReadIniInt(L"fov_scale", 70) / 100.0f;
            float vf[4];
            ShadowViewFractions(g_viewPass, vf);
            if (k > 0.0f && k != 1.0f && vf[2] != vf[3]) {
                ((float*)refdef)[8] *= k;
                ((float*)refdef)[9] *= k;
            } // a quarter has the screen's aspect
        }
        if (g_viewPass >= 1 && g_p2.started && ReadIniInt(L"p2_cam", 1) &&
            *(uint32_t*)(ShadowViewCl() + engine::kClSnapshot)) {
            const float* ps = (const float*)ShadowFramePs(*(const uint8_t**)engine::kCg);
            float* org = (float*)(refdef + 0xd);   // +0x34
            float* axis = (float*)(refdef + 0x11); // +0x44: forward, left, up
            org[0] = ps[10];
            org[1] = ps[11];
            org[2] = ps[12] + (float)ReadIniInt(L"p2_viewheight", 60);
            float yaw = ViewYaw() * 3.14159265f / 180.0f, pitch = ViewPitch() * 3.14159265f / 180.0f;
            float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
            axis[0] = cp * cy;
            axis[1] = cp * sy;
            axis[2] = -sp; // forward
            axis[3] = -sy;
            axis[4] = cy;
            axis[5] = 0.0f; // left
            axis[6] = sp * cy;
            axis[7] = sp * sy;
            axis[8] = cp; // up
        }
    }
    static DWORD nextLog;
    if (g_frameLogLeft > 0 && g_inRenderViews) {
        const float* f = (const float*)refdef;
        const uint8_t* cg = *(const uint8_t**)engine::kCg;
        const float* ps = cg ? (const float*)(cg + engine::kCgPredictedPs) : nullptr;
        const float* sp = (const float*)g_savedPs;
        Log("[frame] pass %d submit: rect %d %d %d %d fovT %.3f %.3f org %.1f %.1f %.1f fwd %.2f %.2f %.2f | ps ang "
            "%.1f %.1f delta %.1f %.1f | pad %.1f %.1f | p1 ang %.1f %.1f",
            g_viewPass, refdef[0], refdef[1], refdef[2], refdef[3], f[8], f[9], f[13], f[14], f[15], f[17], f[18],
            f[19], ps ? ps[0x1f8 / 4] : 0.0f, ps ? ps[0x1fc / 4] : 0.0f, ps ? ps[0x98 / 4] : 0.0f,
            ps ? ps[0x9c / 4] : 0.0f, g_p2Pitch, g_p2Yaw, sp[0x1f8 / 4], sp[0x1fc / 4]);
    }
    if (g_inRenderViews && (LONG)(GetTickCount() - nextLog) >= 0 && g_viewPass == 1) {
        nextLog = GetTickCount() + 2000;
        const float* f = (const float*)refdef;
        if (TraceOn())
            Log("[view] submit pass %d: rect %d %d %d %d fovT %.3f %.3f org %.1f %.1f %.1f fwd %.2f %.2f %.2f",
                g_viewPass, refdef[0], refdef[1], refdef[2], refdef[3], f[8], f[9], f[13], f[14], f[15], f[17], f[18],
                f[19]);
    }
    oDrawSceneFinal(refdef, a, b, c);
    // p2_vm_trace: the viewmodel's scene entity (0x8c bytes at 0x0345b408 + index * 0x8c, filled
    // by R_AddDObjToScene) after the renderer built the view.
    if (g_vmTraceLeft > 0 && g_inRenderViews && g_vmSceneIdx >= 0 && g_vmSceneIdx < 0x400) {
        const uint32_t* e = (const uint32_t*)(engine::kSceneEntities + (size_t)g_vmSceneIdx * 0x8c);
        char line[400];
        size_t n = 0;
        for (int i = 0; i < 0x23; ++i)
            n += (size_t)_snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %08x", e[i]);
        Log("[vm] scene ent %d after render, pass %d:%s", g_vmSceneIdx, g_viewPass, line);
        {
            int dc = *(const int*)engine::kDpvsClient;
            const uint8_t* ps = (const uint8_t*)engine::kViewPlanes + dc * 0x360;
            const uint8_t* fe = *(const uint8_t**)engine::kFrontendData;
            unsigned pi = *(const uint16_t*)((const uint8_t*)e + 0x80);
            Log("[vm] pass %d: dpvs client %d, special entnum %d, plane set masks %08x %08x %08x (plane counts %d %d "
                "%d), ent params idx %u flags %08x, linked %d, vis %d %d %d",
                g_viewPass, dc, *(const int*)engine::kTraceSpecialEntnum, *(const uint32_t*)ps,
                *(const uint32_t*)(ps + 0x120), *(const uint32_t*)(ps + 0x240), *(const int*)(ps + 0x11c),
                *(const int*)(ps + 0x23c), *(const int*)(ps + 0x35c), pi,
                *(const uint32_t*)(fe + (pi + 0x462d8) * 0x10),
                (int)(*(const uint8_t* const*)(engine::kTraceLinkedPtrs + dc * 4))[0x3ff],
                (int)(*(const uint8_t* const*)engine::kTraceVisPtr0)[0x3ff],
                (int)(*(const uint8_t* const*)engine::kTraceVisPtr1)[0x3ff],
                (int)(*(const uint8_t* const*)engine::kTraceVisPtr2)[0x3ff]);
        }
        g_vmSceneIdx = -1;
    }
}
// p2_lod_origin=1: the renderer's per-frame view data for the second pass. 0x0041ba60(refdef),
// called once per CG_DrawActiveFrame (0x0052548b), copies refdef+0x34 into the global LOD
// parameters 0x03509a54 (the DObj LOD 0x007874a0 -> 0x0043eef0 / 0x0043efc0 measures every
// scene entity's distance against that origin, already when the cgame asks for a tag of a
// just-added DObj) and into 0x03458770, and runs two more refdef consumers (0x00413340,
// 0x004115e0). In the second pass the refdef still holds player 1's eye at that point: with
// the players far apart every DObj near player 2 would get LOD -1 (scene entity +0x4c = 0xffff,
// cull state 4, never skinned), so player 2's weapon and hands and the script models around it
// would not be drawn. The origin of player 2's camera goes into the refdef before the call.
typedef void(__cdecl* SetFrameLodOrigin_t)(int*);
static SetFrameLodOrigin_t oSetFrameLodOrigin;
static void __cdecl HookSetFrameLodOrigin(int* refdef) {
    if (g_inRenderViews && g_viewPass >= 1 && g_psSwapped && g_p2.started &&
        *(uint32_t*)(ShadowViewCl() + engine::kClSnapshot) && ReadIniInt(L"p2_lod_origin", 1)) {
        const float* ps = (const float*)ShadowFramePs(*(const uint8_t**)engine::kCg);
        float* org = (float*)(refdef + 0xd);
        float want[3] = {ps[10], ps[11], ps[12] + (float)ReadIniInt(L"p2_viewheight", 60)};
        float dx = org[0] - want[0], dy = org[1] - want[1];
        static DWORD nextLog;
        if (dx * dx + dy * dy > 100.0f * 100.0f && (LONG)(GetTickCount() - nextLog) >= 0) {
            nextLog = GetTickCount() + 5000;
            if (TraceOn())
                Log("[p2] refdef origin at the frame's LOD setup of the second pass was %.0f %.0f %.0f, player 2's "
                    "camera %.0f %.0f %.0f",
                    org[0], org[1], org[2], want[0], want[1], want[2]);
        }
        org[0] = want[0];
        org[1] = want[1];
        org[2] = want[2];
    }
    oSetFrameLodOrigin(refdef);
}
static const uint8_t kProSetFrameLodOrigin[] = {0xa1, 0x00, 0x53, 0x4c, 0x03, 0x56, 0x50};
static const uint8_t kProDrawSceneFinal[] = {0x8b, 0x44, 0x24, 0x04, 0xf3, 0x0f, 0x10, 0x40, 0x34};

// R_RenderScene(refdef) (0x0041bb10) copies vieworg/viewaxis/fov into the renderer before
// CG_DrawView reaches 0x00545900, so the player-2 camera has to be in place here: origin
// from the shadow client's snapshot, axis from the pad yaw/pitch.
static void ShadowApplyCamera(int* refdef) {
    bool native = ReadIniInt(L"p2_cam_native", 1) != 0;
    // Native camera: origin, bob and fov come from the cgame's view calc; the axis is still
    // forced from the pad facing here (p2_cam_native_axis=1) so a late viewangles overwrite
    // cannot change the facing of the submitted scene.
    if (native && !ReadIniInt(L"p2_cam_native_axis", 1)) return;
    if (!(g_viewPass >= 1 && g_p2.started && ReadIniInt(L"p2_cam", 1) &&
          *(uint32_t*)(ShadowViewCl() + engine::kClSnapshot)))
        return;
    const float* ps = (const float*)ShadowFramePs(*(const uint8_t**)engine::kCg);
    float* org = (float*)(refdef + 0xd);   // +0x34
    float* axis = (float*)(refdef + 0x11); // +0x44: forward, left, up
    if (!native) {
        org[0] = ps[10];
        org[1] = ps[11];
        org[2] = ps[12] + (float)ReadIniInt(L"p2_viewheight", 60);
    }
    // The server adds ps.delta_angles to the usercmd angles; the facing it uses is
    // ps.viewangles (+0x1f8 pitch, +0x1fc yaw), not the raw pad angles.
    // viewangles = usercmd angles + ps.delta_angles (+0x98 pitch, +0x9c yaw); the server does
    // not send viewangles.
    float yawDeg = ViewYaw(), pitchDeg = ViewPitch();
    if (ReadIniInt(L"p2_cam_delta_angles", 1)) {
        pitchDeg += ps[0x98 / 4];
        yawDeg += ps[0x9c / 4];
    }
    // The view calc may have turned the playerState's viewangles away from the facing written
    // before it: a player that is down sways. The weapon follows those angles, so with only the
    // pad facing forced here the hand moved and the camera stood still. A difference of less
    // than 20 degrees is taken into the facing; a larger one is a viewangles overwrite.
    if (native) {
        const uint8_t* block = *(const uint8_t**)engine::kCg;
        if (block) {
            const float* va = (const float*)(block + engine::kCgPredictedPs + 0x1f8);
            float dp = va[0] - pitchDeg, dy = va[1] - yawDeg;
            dy -= 360.0f * floorf((dy + 180.0f) / 360.0f);
            if (fabsf(dp) < 20.0f && fabsf(dy) < 20.0f) {
                pitchDeg += dp;
                yawDeg += dy;
            }
        }
    }
    float yaw = yawDeg * 3.14159265f / 180.0f, pitch = pitchDeg * 3.14159265f / 180.0f;
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    // p2_cam_kick=1: what the cgame's view calc turned the camera by on top of the
    // playerState's viewangles (weapon kick, the camera motion of the reload and melee
    // animations, damage kick) is kept. The forced axis above would throw it away (players 2
    // to 4 would have no camera movement when they fire, reload or knife). The engine's axis is
    // expressed in the frame of the viewangles it started from (predicted playerState +0x1f8
    // pitch, +0x1fc yaw, +0x200 roll) and that rotation is applied to the forced axis.
    // Offsets above 45 degrees are not taken (the two facings of a late viewangles overwrite).
    float c[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const uint8_t* cg = *(const uint8_t**)engine::kCg;
    if (native && cg && ReadIniInt(L"p2_cam_kick", 1)) {
        const float* va = (const float*)(cg + engine::kCgPredictedPs + 0x1f8);
        float bp = va[0] * 3.14159265f / 180.0f, by = va[1] * 3.14159265f / 180.0f, br = va[2] * 3.14159265f / 180.0f;
        float sp2 = sinf(bp), cp2 = cosf(bp), sy2 = sinf(by), cy2 = cosf(by), sr2 = sinf(br), cr2 = cosf(br);
        float base[9] = {cp2 * cy2,
                         cp2 * sy2,
                         -sp2,
                         sr2 * sp2 * cy2 - cr2 * sy2,
                         sr2 * sp2 * sy2 + cr2 * cy2,
                         sr2 * cp2,
                         cr2 * sp2 * cy2 + sr2 * sy2,
                         cr2 * sp2 * sy2 - sr2 * cy2,
                         cr2 * cp2};
        float t[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                t[i][j] =
                    axis[i * 3] * base[j * 3] + axis[i * 3 + 1] * base[j * 3 + 1] + axis[i * 3 + 2] * base[j * 3 + 2];
        bool sane = t[0][0] > 0.7071f && t[1][1] > 0.7071f && t[2][2] > 0.7071f;
        if (sane) memcpy(c, t, sizeof(c));
        // p2_cam_kick_log=N (diagnostic): N lines, one a second per pass, with the largest offset of that second.
        static int left = -1, armed = -1;
        static DWORD next[4];
        static float worst[4];
        {
            int v = ReadIniInt(L"p2_cam_kick_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0 && g_viewPass >= 1 && g_viewPass <= 3) {
            float deg = acosf(t[0][0] > 1.0f ? 1.0f : t[0][0] < -1.0f ? -1.0f : t[0][0]) * 180.0f / 3.14159265f;
            if (deg > worst[g_viewPass]) worst[g_viewPass] = deg;
            if ((LONG)(GetTickCount() - next[g_viewPass]) >= 0) {
                next[g_viewPass] = GetTickCount() + 1000;
                --left;
                Log("[view] pass %d camera offset over the viewangles: up to %.2f degrees in the last second (taken "
                    "%d)",
                    g_viewPass, worst[g_viewPass], (int)sane);
                worst[g_viewPass] = 0.0f;
            }
        }
    }
    float f[9] = {cp * cy, cp * sy, -sp, -sy, cy, 0.0f, sp * cy, sp * sy, cp}; // forward, left, up
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            axis[i * 3 + k] = c[i][0] * f[k] + c[i][1] * f[3 + k] + c[i][2] * f[6 + k];
}
// p2_actor_cull: the actor add (0x00499a40, eType 16, the zombies) skips an actor whose bounds
// fail R_CullBoxAgainstLastView 0x0040fd70(mins, maxs, lc): the frustum planes the renderer
// kept from the local client's last scene (0x0334577c + lc * 0x360: planes at +4, count at
// +0x11c; tested by 0x004070d0(mins, maxs, planes, count)). Both passes are local client 0,
// so each pass would test against the other player's camera: zombies near player 2 would only
// be added to player 1's scene and the other way round (zombies would not be visible when
// the players are apart). The planes are not written inside R_RenderScene: a copy taken
// right after it still holds the other pass's camera. So what a
// pass finds there while it adds its entities is kept as the other pass's planes.
// 2 = each pass tests against the planes of its own camera (one frame old for the first
// pass), 1 = no cull while the split runs, 0 = engine.
static uint8_t g_lastViewPlanes[4][0x360];
static bool g_lastViewPlanesValid[4];
typedef int(__cdecl* CullBoxLastView_t)(const float*, const float*, int);
static CullBoxLastView_t oCullBoxLastView;
static int __cdecl HookCullBoxLastView(const float* mins, const float* maxs, int lc) {
    if (g_p2.started && g_inRenderViews && g_viewPass < 4 && lc == 0) {
        // What a pass finds are the planes of the pass before it (the frame's last pass for pass 0).
        int n = g_viewCount, prev = (g_viewPass + n - 1) % n;
        memcpy(g_lastViewPlanes[prev], (const void*)engine::kViewPlanes, 0x360);
        g_lastViewPlanesValid[prev] = true;
        int mode = ReadIniInt(L"p2_actor_cull", 2);
        if (mode == 1) return 0;
        if (mode == 2) {
            if (!g_lastViewPlanesValid[g_viewPass]) return 0;
            const uint8_t* v = g_lastViewPlanes[g_viewPass];
            return ((int(__cdecl*)(const float*, const float*, const void*, int))0x004070d0)(mins, maxs, v + 4,
                                                                                             *(const int*)(v + 0x11c));
        }
    }
    return oCullBoxLastView(mins, maxs, lc);
}
static const uint8_t kProCullBoxLastView[] = {0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x54, 0x24, 0x08};
typedef int(__cdecl* RenderScene_t)(int*);
static RenderScene_t oRenderScene;
static int __cdecl HookRenderScene(int* refdef) {
    if (g_inRenderViews && ReadIniInt(L"p2_view", 1)) ShadowApplyCamera(refdef);
    // p_cam_far_log=N (diagnostic): N lines, each for a shadow pass whose camera is more than
    // 100 units (in the plane) from the place of the pass's player. The camera of a pass belongs
    // at its player's eyes; a line here means the pass ran on another player's state.
    if (g_inRenderViews && g_viewPass >= 1 && g_psSwapped) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p_cam_far_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0) {
            const float* org = (const float*)(refdef + 0xd);
            const float* own = (const float*)(ShadowFramePs(*(const uint8_t**)engine::kCg) + engine::kPsOrigin);
            float dx = org[0] - own[0], dy = org[1] - own[1];
            if (dx * dx + dy * dy > 100.0f * 100.0f) {
                --left;
                Log("[scene] pass %d: CAMERA FAR from its player: camera %.0f %.0f %.0f, player %d at %.0f %.0f %.0f",
                    g_viewPass, org[0], org[1], org[2], g_viewPass + 1, own[0], own[1], own[2]);
            }
        }
    }
    // p2_scene_log=N (diagnostic): N lines about the scene the second pass hands to the
    // renderer: scene DObj count 0x0345b404 and scene model count 0x0348000c (the two lists
    // R_AddDObjToScene 0x004106f0 fills), camera origin against player 2's origin. A line is
    // written when a count moves by 3 or more against the previous frame or the camera is
    // more than 200 units from player 2.
    if (g_inRenderViews && g_viewPass == 1 && g_psSwapped) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_scene_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0) {
            static int lastDObj = -1, lastModel = -1;
            static unsigned frame;
            ++frame;
            int dobj = *(const int*)engine::kSceneDObjCount, model = *(const int*)engine::kSceneModelCount;
            const float* org = (const float*)(refdef + 0xd);
            const float* p2o = (const float*)(g_p2.cl + engine::kClPlayerState + engine::kPsOrigin);
            float dx = org[0] - p2o[0], dy = org[1] - p2o[1];
            bool camFar = dx * dx + dy * dy > 200.0f * 200.0f;
            if (abs(dobj - lastDObj) >= 3 || abs(model - lastModel) >= 3 || camFar) {
                --left;
                Log("[scene] frame %u pass 1: dobj %d (was %d) models %d (was %d) camera %.0f %.0f %.0f, player 2 %.0f "
                    "%.0f %.0f%s",
                    frame, dobj, lastDObj, model, lastModel, org[0], org[1], org[2], p2o[0], p2o[1], p2o[2],
                    camFar ? " CAMERA FAR" : "");
            }
            lastDObj = dobj;
            lastModel = model;
        }
    }
    // p2_no_lmonly_ents=1 (default 0): dvar r_lightmapOnlyEntSurfs (pointer 0x034c54ac, cheat
    // protected, current value at +0x18) off while the split runs. With it on, the scene entity
    // jobs (0x00409780 DObjs, 0x00409910 models) draw an entity that is in neither sun shadow
    // cascade (visibility bytes 0x0347e808 / 0x0347ec08) with the lightmap-only technique.
    if (g_p2.started) {
        static int applied = -1;
        static uint8_t saved;
        int want = ReadIniInt(L"p2_no_lmonly_ents", 0);
        uint8_t* dvar = *(uint8_t**)engine::kLightmapOnlyEntSurfsDvar;
        if (dvar && want != applied) {
            if (applied <= 0 && want) saved = dvar[0x18];
            dvar[0x18] = want ? 0 : saved;
            Log("[p2] r_lightmapOnlyEntSurfs %d (p2_no_lmonly_ents=%d)", dvar[0x18], want);
            applied = want;
        }
    }
    int r = oRenderScene(refdef);
    // p2_vis_log=N (diagnostic): the seven visibility bytes per scene DObj that 0x0040ae70 fills
    // per pass (0x0347e408 camera, 0x0347e808 / 0x0347ec08 sun shadow cascades, 0x0347f008 ..
    // 0x0347fc08 spot shadows; index = position in the scene list 0x0345b408, entity number at
    // entry+0x84). N lines, one per entity whose bytes differ from the previous frame's same pass.
    if (g_inRenderViews && g_viewPass < 2 && g_p2.started) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_vis_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0) {
            static uint8_t last[2][0x1000][8];
            static unsigned frame;
            if (g_viewPass == 0) ++frame;
            int count = *(const int*)engine::kSceneDObjCount;
            if (count > 0x400) count = 0x400;
            for (int i = 0; i < count && left > 0; ++i) {
                const uint8_t* ent = (const uint8_t*)engine::kSceneEntities + i * 0x8c;
                unsigned num = *(const uint16_t*)(ent + 0x84) & 0xfff;
                uint8_t now[8];
                for (int k = 0; k < 7; ++k)
                    now[k] = *((const uint8_t*)engine::kSceneVisBytes + k * 0x400 + i);
                now[7] = 1; // seen
                uint8_t* was = last[g_viewPass][num];
                if (was[7] && memcmp(was, now, 7) != 0) {
                    --left;
                    const float* o = (const float*)ent;
                    Log("[vis] frame %u pass %d ent %u at %.0f %.0f %.0f: cam %d sun %d %d spot %d %d %d %d (was cam "
                        "%d sun %d %d spot %d %d %d %d) cull %d lod %04x",
                        frame, g_viewPass, num, o[0], o[1], o[2], now[0], now[1], now[2], now[3], now[4], now[5],
                        now[6], was[0], was[1], was[2], was[3], was[4], was[5], was[6], *(const int*)(ent + 0x30),
                        *(const uint16_t*)(ent + 0x4c));
                }
                memcpy(was, now, 8);
            }
        }
    }
    // p2_ent_log=N (diagnostic): N reports, one a second, of the scene DObjs with an entity
    // number below 64 (players and actors) per pass: scene origin, camera visibility byte,
    // cull state (+0x30), LOD word (+0x4c).
    if (g_inRenderViews && g_viewPass < 2 && g_p2.started) {
        static int left = -1, armed = -1;
        static DWORD next;
        static bool both;
        {
            int v = ReadIniInt(L"p2_ent_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (g_viewPass == 0) {
            both = left > 0 && (LONG)(GetTickCount() - next) >= 0;
            if (both) {
                next = GetTickCount() + 1000;
                --left;
            }
        }
        if (both) {
            int count = *(const int*)engine::kSceneDObjCount;
            Log("[entlog] pass %d: %d scene dobjs", g_viewPass, count);
            if (count > 0x400) count = 0x400;
            for (int i = 0; i < count; ++i) {
                const uint8_t* ent = (const uint8_t*)engine::kSceneEntities + i * 0x8c;
                unsigned num = *(const uint16_t*)(ent + 0x84) & 0xfff;
                if (num >= 64) continue;
                const float* o = (const float*)ent;
                Log("[entlog] pass %d ent %u index %d at %.0f %.0f %.0f cam %d cull %d lod %04x", g_viewPass, num, i,
                    o[0], o[1], o[2], *((const uint8_t*)engine::kSceneVisBytes + i), *(const int*)(ent + 0x30),
                    *(const uint16_t*)(ent + 0x4c));
            }
        }
    }
    // p2_fx_log=N (diagnostic): N reports, one a second, of the FX system's three cameras per
    // pass (system +0 next update, +0xa0 current, +0x1e0 previous): origin, valid flag (+0xc),
    // frustum plane count (+0x94); plus the count of spawn culls p2_fx_cull overruled.
    if (g_inRenderViews && g_viewPass < 2 && g_p2.started) {
        static int left = -1, armed = -1;
        static DWORD next;
        static bool both;
        {
            int v = ReadIniInt(L"p2_fx_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (g_viewPass == 0) {
            both = left > 0 && (LONG)(GetTickCount() - next) >= 0;
            if (both) {
                next = GetTickCount() + 1000;
                --left;
            }
        }
        const uint8_t* sys = *(const uint8_t**)engine::kFxSystem;
        if (both && sys) {
            ShadowFxLog(g_viewPass);
            static const int kCam[3] = {0, 0xa0, 0x1e0};
            for (int i = 0; i < 3; ++i) {
                const uint8_t* c = sys + kCam[i];
                const float* o = (const float*)c;
                Log("[fxlog] pass %d camera +%03x: origin %.0f %.0f %.0f valid %d planes %d", g_viewPass, kCam[i], o[0],
                    o[1], o[2], *(const int*)(c + 0xc), *(const int*)(c + 0x94));
            }
        }
    }
    // Same switch, after the renderer built the view: the draw surface counts of the seven
    // lists 0x0045b070 fills (0x034285dc ...), the static model lighting cache (handles in
    // use 0x03954c84 of 0x03954c80, stealable 0x03954c88; R_AllocStaticModelLighting
    // 0x004577f0 refuses a model when nothing is stealable) and the "entity lighting pool
    // exhausted" flag 0x03954e30. One line per pass when a list count moves by 8 or more.
    if (g_inRenderViews && g_viewPass < 2 && g_p2.started) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_scene_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0) {
            static const uintptr_t kCounts[7] = {
                engine::kDrawSurfCount0, engine::kDrawSurfCount1, engine::kDrawSurfCount2, engine::kDrawSurfCount3,
                engine::kDrawSurfCount4, engine::kDrawSurfCount5, engine::kDrawSurfCount6};
            static int last[2][7];
            static unsigned frame;
            if (g_viewPass == 0) ++frame;
            int now[7];
            bool moved = false;
            for (int i = 0; i < 7; ++i) {
                now[i] = *(const int*)kCounts[i];
                if (abs(now[i] - last[g_viewPass][i]) >= 8) moved = true;
            }
            if (moved) {
                --left;
                Log("[scene] frame %u pass %d surfs %d %d %d %d %d %d %d (was %d %d %d %d %d %d %d) smodel light %d/%d "
                    "stealable %d, entity pool full %d",
                    frame, g_viewPass, now[0], now[1], now[2], now[3], now[4], now[5], now[6], last[g_viewPass][0],
                    last[g_viewPass][1], last[g_viewPass][2], last[g_viewPass][3], last[g_viewPass][4],
                    last[g_viewPass][5], last[g_viewPass][6], *(const int*)engine::kTraceStaticModelLightsInUse,
                    *(const int*)engine::kTraceStaticModelLights, *(const int*)engine::kTraceStaticModelStealable,
                    *(const int*)engine::kTraceEntityLightPoolFull);
            }
            memcpy(last[g_viewPass], now, sizeof(now));
        }
    }
    return r;
}
static const uint8_t kProRenderScene[] = {0xa1, 0x30, 0x50, 0x4c, 0x03, 0x50};

const Detour kViewSceneHooks[] = {
    {"actor cull against the last view (each pass against its own camera)", 0x0040fd70, kProCullBoxLastView,
     sizeof(kProCullBoxLastView), (void*)HookCullBoxLastView, (void**)&oCullBoxLastView},
    {"CG_DrawView scene submit (viewports, player 2 camera)", 0x00545900, kProDrawSceneFinal,
     sizeof(kProDrawSceneFinal), (void*)HookDrawSceneFinal, (void**)&oDrawSceneFinal},
    {"R_RenderScene (player 2 camera before the copy)", 0x0041bb10, kProRenderScene, sizeof(kProRenderScene),
     (void*)HookRenderScene, (void**)&oRenderScene},
    {"R frame LOD origin from the refdef (player 2's camera in the second pass)", 0x0041ba60, kProSetFrameLodOrigin,
     sizeof(kProSetFrameLodOrigin), (void*)HookSetFrameLodOrigin, (void**)&oSetFrameLodOrigin},
};
