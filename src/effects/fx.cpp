// Effects (FX) in the split: the engine culls effect elements against one camera, the split
// has two.
//
// The FX system of local client 0 (pointer 0x020d2864, FX_GetSystem 0x005c3120) holds cameras
// of 0xa0 bytes each: origin at +0, valid flag at +0xc, frustum planes of four floats from
// +0x10, axis at +0x70, plane count at +0x94 (5 or 6). System +0 is the "next update" camera,
// written by FX_SetNextUpdateCamera 0x005c9d60 at the end of every view pass; the update takes
// it over into +0xa0, and the spawn culling tests against +0xa0 (and +0x1e0 when that one is
// valid). Both view passes run as local client 0 on the same system, so an element spawns
// only when the camera of one pass has it in range and in the frustum: the chalk outline of
// a wall buy shows in player 2's half only while player 1 looks at it, and not at all when
// player 1 is far away (spawn range).
//
// p2_fx_cull=1: the camera of each pass is kept (ShadowFxCaptureCamera), and an element or a
// sphere counts as culled only when both cameras cull it.
//   0x005c3bf0 (system, elemDef on the stack, origin in EAX; returns a mask, bit 0 = not
//     culled for the camera +0xa0): spawn culling of one element. Tests the spawn range
//     (elemDef +0xc base, +0x10 amplitude; skipped when the amplitude is 0) and, with
//     elemDef flag 4 and 0x005c7ad0() == 0, the sphere (radius elemDef +0x24).
//   FX_CullSphere 0x005cd6d0 (camera, plane count, origin, radius): the frustum test of the
//     other callers (effect bounds, draw).
//   0x005c9710 (system, mins, maxs, effect flags; returns a mask, bit 0 = culled by the camera
//     +0xa0, bit 1 = culled by the camera +0x1e0 or that camera is not valid): the bounding box
//     of a whole effect against the frustum, through 0x005cd740 (camera, plane count, mins,
//     maxs). The effect spawn (0x005c4890) and the effect update (0x005cc430) call it; with
//     mask 3 an effect is not spawned, spawned asleep (effect +0xfa bit 4) or stopped.
//
// p2_fx_cells=1: 0x005b4bd0 (system, mins, maxs) asks the renderer whether the box lies in a
// cell the last scene of the local client saw (0x0040be80). The last scene is the other
// player's, so an effect near player 2 would sleep while player 1 is in another part of the
// map (effect definition flag 0x400, e.g. the chalk outline). The split answers "visible".
#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/render.h"

static const int kFxCameraSize = 0xa0;
// Two buffers per pass: the capture writes the idle one and then flips the index, so a worker
// thread in the effect update never reads a half written camera.
// 16-byte aligned: 0x005cd740 reads the planes with movaps, which faults on an unaligned
// address (for example when the linker places this array at an address ending in 8).
// One camera per view pass (up to four views).
__declspec(align(16)) static uint8_t g_fxCam[4][2][kFxCameraSize];
static volatile LONG g_fxCamIndex[4] = {-1, -1, -1, -1};
static volatile int g_fxCams = 2; // passes of the current frame
// The ini switch, read once per pass (the hooks run per element, on worker threads too).
static volatile int g_fxCullOn;
static volatile int g_fxCellsOn;

void ShadowFxCaptureCamera(const uint8_t* sys) {
    if (!sys || g_viewPass < 0 || g_viewPass > 3) return;
    int pass = g_viewPass;
    LONG next = g_fxCamIndex[pass] == 0 ? 1 : 0;
    memcpy(g_fxCam[pass][next], sys, kFxCameraSize);
    InterlockedExchange(&g_fxCamIndex[pass], next);
    {
        int n = g_viewCount;
        g_fxCams = n < 2 ? 2 : n > 4 ? 4 : n;
    }
    g_fxCullOn = ReadIniInt(L"p2_fx_cull", 1);
    g_fxCellsOn = ReadIniInt(L"p2_fx_cells", 1);
}

static const uint8_t* FxCamera(int pass) {
    LONG i = g_fxCamIndex[pass];
    return i < 0 ? nullptr : g_fxCam[pass][i];
}

static bool FxOn() {
    if (!g_fxCullOn || !g_p2.started) return false;
    for (int i = 0, n = g_fxCams; i < n; ++i)
        if (g_fxCamIndex[i] < 0) return false;
    return true;
}

typedef char(__cdecl* FxCullSphere_t)(const uint8_t*, int, const float*, float);
static FxCullSphere_t oFxCullSphere;

static bool IsSystemCamera(const uint8_t* camera) {
    const uint8_t* sys = *(const uint8_t**)engine::kFxSystem;
    return sys && camera >= sys && camera < sys + 0x280;
}

static char __cdecl HookFxCullSphere(const uint8_t* camera, int planeCount, const float* origin, float radius) {
    if (planeCount && IsSystemCamera(camera) && FxOn()) {
        for (int i = 0, n = g_fxCams; i < n; ++i) {
            const uint8_t* c = FxCamera(i);
            if (!oFxCullSphere(c, *(const int*)(c + 0x94), origin, radius)) return 0;
        }
        return 1;
    }
    return oFxCullSphere(camera, planeCount, origin, radius);
}
static const uint8_t kProFxCullSphere[] = {0x8b, 0x54, 0x24, 0x08, 0x33, 0xc9};

// The engine's test of one element against one camera (the inlined half of 0x005c3bf0).
static bool FxElemCulled(const uint8_t* camera, const uint8_t* elemDef, const float* origin) {
    float base = *(const float*)(elemDef + 0xc), amplitude = *(const float*)(elemDef + 0x10);
    if (amplitude != 0.0f) {
        const float* c = (const float*)camera;
        float dx = c[0] - origin[0], dy = c[1] - origin[1], dz = c[2] - origin[2];
        float distSq = dx * dx + dy * dy + dz * dz;
        if (base * base > distSq || distSq > (base + amplitude) * (base + amplitude)) return true;
    }
    if ((elemDef[0] & 4) && !((char(__cdecl*)())0x005c7ad0)())
        return oFxCullSphere(camera, *(const int*)(camera + 0x94), origin, *(const float*)(elemDef + 0x24)) != 0;
    return false;
}

static volatile LONG g_fxKept;
static volatile LONG g_fxBoxKept;
static volatile LONG g_fxCellKept;

typedef unsigned(__cdecl* FxCullEffectBox_t)(const uint8_t*, const float*, const float*, unsigned);
static FxCullEffectBox_t oFxCullEffectBox;

static unsigned __cdecl HookFxCullEffectBox(const uint8_t* sys, const float* mins, const float* maxs, unsigned flags) {
    unsigned mask = oFxCullEffectBox(sys, mins, maxs, flags);
    if ((mask & 1) && FxOn()) {
        for (int i = 0, n = g_fxCams; i < n; ++i) {
            const uint8_t* c = FxCamera(i);
            if (engine::FX_CullBoxAgainstPlanes(c, *(const int*)(c + 0x94), mins, maxs)) continue;
            InterlockedIncrement(&g_fxBoxKept);
            mask &= ~1u;
            break;
        }
    }
    return mask;
}
static const uint8_t kProFxCullEffectBox[] = {0x53, 0x8b, 0x5c, 0x24, 0x10};

typedef int(__cdecl* FxBoxInVisibleCell_t)(const uint8_t*, const float*, const float*);
static FxBoxInVisibleCell_t oFxBoxInVisibleCell;

static int __cdecl HookFxBoxInVisibleCell(const uint8_t* sys, const float* mins, const float* maxs) {
    int visible = oFxBoxInVisibleCell(sys, mins, maxs);
    if (!visible && g_fxCellsOn && FxOn()) {
        InterlockedIncrement(&g_fxCellKept);
        return 1;
    }
    return visible;
}
static const uint8_t kProFxBoxInVisibleCell[] = {0x8b, 0x44, 0x24, 0x04, 0x0f, 0xb6, 0x88, 0xd6, 0x02, 0x00, 0x00};

static int __cdecl FxSpawnCullFix(const uint8_t* sys, const uint8_t* elemDef, const float* origin, int mask) {
    (void)sys;
    if ((mask & 1) || !FxOn()) return mask;
    for (int i = 0, n = g_fxCams; i < n; ++i) {
        if (FxElemCulled(FxCamera(i), elemDef, origin)) continue;
        InterlockedIncrement(&g_fxKept);
        return mask | 1;
    }
    return mask;
}

static void* oFxCullElemForSpawn;
// 0x005c3bf0: origin in EAX, (system, elemDef) on the stack, result in AL. The function keeps
// ECX (push / pop), so the hook keeps ECX and EDX and the upper xmm registers too.
__declspec(naked) static void HookFxCullElemForSpawn() {
    __asm {
        push edx
        push eax // origin
        push dword ptr [esp + 0x10] // elemDef
        push dword ptr [esp + 0x10] // system
        call dword ptr [oFxCullElemForSpawn]
        add esp, 8
        movzx eax, al
        pop edx // origin
        push ecx
        sub esp, 0x40
        movups [esp], xmm4
        movups [esp + 0x10], xmm5
        movups [esp + 0x20], xmm6
        movups [esp + 0x30], xmm7
        push eax // mask
        push edx // origin
        push dword ptr [esp + 0x58] // elemDef
        push dword ptr [esp + 0x58] // system
        call FxSpawnCullFix
        add esp, 0x10
        movups xmm4, [esp]
        movups xmm5, [esp + 0x10]
        movups xmm6, [esp + 0x20]
        movups xmm7, [esp + 0x30]
        add esp, 0x40
        pop ecx
        pop edx
        ret
    }
}
static const uint8_t kProFxCullElemForSpawn[] = {0x51, 0x53, 0x8b, 0x5c, 0x24, 0x0c};

// p2_fx_log=N (diagnostic), called once per pass: the count of elements the second camera
// kept since the last report.
void ShadowFxLog(int pass) {
    if (pass != 0) return;
    LONG kept = InterlockedExchange(&g_fxKept, 0);
    LONG box = InterlockedExchange(&g_fxBoxKept, 0);
    LONG cell = InterlockedExchange(&g_fxCellKept, 0);
    Log("[fxlog] overruled since the last report: element spawn culls %ld, effect box culls %ld, cell visibility %ld",
        kept, box, cell);
}

const Detour kFxHooks[] = {
    {"FX_CullSphere (culled only when both players' cameras cull)", 0x005cd6d0, kProFxCullSphere,
     sizeof(kProFxCullSphere), (void*)HookFxCullSphere, (void**)&oFxCullSphere},
    {"FX spawn culling of an element (same, with the spawn range)", 0x005c3bf0, kProFxCullElemForSpawn,
     sizeof(kProFxCullElemForSpawn), (void*)HookFxCullElemForSpawn, (void**)&oFxCullElemForSpawn},
    {"FX box culling of an effect (culled only when both players' cameras cull)", 0x005c9710, kProFxCullEffectBox,
     sizeof(kProFxCullEffectBox), (void*)HookFxCullEffectBox, (void**)&oFxCullEffectBox},
    {"FX cell visibility of an effect box (always visible while the split runs)", 0x005b4bd0, kProFxBoxInVisibleCell,
     sizeof(kProFxBoxInVisibleCell), (void*)HookFxBoxInVisibleCell, (void**)&oFxBoxInVisibleCell},
};
