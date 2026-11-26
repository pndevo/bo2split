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
#include "engine/offsets.h"

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

// The muzzle flash of the first-person weapon. The game plays it when it adds the view weapon
// (0x005390c0): the fire event sets bits on the player's entity (+0x378), and for the view
// weapon 0x005304a0(lc, 0x700 + lc, weapon, 1, tag_flash) starts the weapon's view flash
// effect, bound to the tag of the first-person weapon's DObj. The fire events of the shadow
// players run in player 1's cgame, where they are other characters: no view flash, only the
// flash of the world model, which sits beside the first-person weapon in the player's own view.
//
// p_view_flash=1: a shadow player's shot (counted by the fire sound hook) starts the view
// flash in its own pass, the way the game does for player 1.
//
// An effect bound to the first-person weapon is drawn in every view pass, in the weapon space
// of that pass (all passes are local client 0 on one effect system), so a flash would show at
// every player's muzzle. The render thread can not tell the effects apart; it knows the pass.
// So each pass remembers when its player last started a view flash, and the effects of the
// first-person weapon are drawn in a pass only for p_fx_weapon_ms after that (WeaponFxInPass,
// used by the viewport hook in render_backend.cpp). Player 1's pass draws them at all other
// times as well, except while only a shadow player's flash is running: effects that scripts
// put on player 1's weapon stay, and a shadow player's flash does not show on player 1's gun.
typedef void(__cdecl* PlayWeaponFlash_t)(int, int, int, int, unsigned);
static PlayWeaponFlash_t oPlayWeaponFlash;
static volatile DWORD g_viewFlashTick[4]; // per pass: tick of the last view flash, 0 = never
static void NoteViewFlash(int handle, int weapon, int viewWeapon, uintptr_t from) {
    if (viewWeapon) {
        int pass = (g_inRenderViews && g_viewPass >= 0 && g_viewPass <= 3) ? g_viewPass : 0;
        DWORD now = GetTickCount();
        g_viewFlashTick[pass] = now ? now : 1;
    }
    // p_view_flash_log=N (diagnostic): N lines, one per flash a weapon starts.
    static int left = -1, armed = -1;
    int v = ReadIniInt(L"p_view_flash_log", 0);
    if (v != armed) {
        armed = v;
        left = v;
    }
    if (left > 0) {
        --left;
        Log("[flash] weapon %d, view weapon %d, handle %x, view pass %d, in the view loop %d, called from %08x", weapon,
            viewWeapon, handle, (int)g_viewPass, g_inRenderViews ? 1 : 0, (unsigned)from);
    }
}
static void PlayViewFlashUnbound(int lc, int handle, int weapon, unsigned tag);
static void __cdecl HookPlayWeaponFlash(int lc, int handle, int weapon, int viewWeapon, unsigned tag) {
    NoteViewFlash(handle, weapon, viewWeapon, (uintptr_t)_ReturnAddress());
    // While more than one view is drawn, player 1's view flash is taken off its binding as a
    // shadow player's is (see below): bound, it would follow the first-person weapon of
    // whichever pass the effect update finds behind the handle, and show there in every view.
    if (viewWeapon && g_viewCount >= 2 && g_p2.started) {
        PlayViewFlashUnbound(lc, handle, weapon, tag);
        return;
    }
    oPlayWeaponFlash(lc, handle, weapon, viewWeapon, tag);
}
static const uint8_t kProPlayWeaponFlash[] = {0x8b, 0x44, 0x24, 0x0c, 0x83, 0xec, 0x24};

// The view flash of a shadow player, started in its own pass. It is what 0x005304a0 does for a
// view weapon: 0x004c2250(lc, effect, DObj handle, tag name, weapon def + 0x7c, axis) plays the
// weapon's view flash effect (0x008e9100) on the tag of the first-person weapon and returns the
// effect's handle.
//
// An effect that has an element moving with it (element flags & 0x1c0 == 0x80, or a trail) is
// bound to the bone (0x005c52e0): its bolt word (effect +0xb4) holds the kind of the binding
// in bits 8 and 9 (1 = a DObj), the DObj handle in bits 10 to 20 and the bone in bits 22 to
// 30, and every update of the effect (0x005c9290) reads the bone's place again. That update
// finds player 1's first-person weapon behind the handle. Most view flashes only run child
// effects, which are born at the muzzle of the player that fired and stay there. The one of the
// upgraded FN FAL (fx_zmb_rifle1_flash_base) draws its fourteen elements itself and keeps them
// for some hundred milliseconds: a shadow player's burst showed as a cloud at player 1's muzzle
// and not at its own.
//
// So the effect of a shadow player is taken off its binding right after
// its start, with the value the engine writes when a bone is gone (kind 0, no handle, no
// bone): it keeps the place and the axis it was born with, the muzzle of the player that fired.
// Without a bone but still bound, the engine would take the weapon origin of player 1's cgame
// block (0x004d56d0).
static void PlayViewFlashUnbound(int lc, int dobjHandle, int weapon, unsigned tag) {
    uint8_t* def = engine::BG_GetWeaponDef(weapon);
    uint8_t* effect = engine::BG_GetViewFlashEffect(weapon);
    if (!def || !effect) return;
    float axis[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    int handle = engine::CG_PlayEffectOnTag(lc, effect, dobjHandle, tag, def + 0x7c, axis);
    uint8_t* sys = *(uint8_t**)engine::kFxSystem;
    uint8_t* fx = handle && sys ? engine::FX_EffectFromHandle(sys, handle) : nullptr;
    if (!fx) return;
    volatile LONG* bolt = (volatile LONG*)(fx + engine::kFxEffectBolt);
    for (;;) {
        LONG was = *bolt;
        LONG now = (LONG)(((unsigned)was & ~engine::kFxBoltKindMask) | engine::kFxBoltNone);
        if (InterlockedCompareExchange(bolt, now, was) == was) break;
    }
}
void ShadowViewFlash(const uint8_t* cg) {
    if (!cg || !oPlayWeaponFlash || !ReadIniInt(L"p_view_flash", 1)) return;
    int weapon = engine::BG_GetWeapon(cg + engine::kCgPredictedPs);
    if (!weapon) return;
    NoteViewFlash(engine::kViewWeaponDObjHandle, weapon, 1, 0);
    PlayViewFlashUnbound(0, engine::kViewWeaponDObjHandle, weapon, *(const uint16_t*)engine::kTagFlashName);
}
bool WeaponFxInPass(int pass) {
    if (pass < 0 || pass > 3) return true;
    DWORD now = GetTickCount();
    DWORD window = (DWORD)ReadIniInt(L"p_fx_weapon_ms", 200);
    auto recent = [&](int p) {
        DWORD t = g_viewFlashTick[p];
        return t && now - t < window;
    };
    if (pass >= 1) return recent(pass);
    return recent(0) || !(recent(1) || recent(2) || recent(3));
}

// The shell casings. 0x00533b20 plays a weapon's shell eject effect: for the view's own player
// the first-person variant, bound to the first-person weapon's DObj (handle 0x700 + lc), for
// everybody else the variant of the world model, bound to the player's entity. It takes the
// first branch when the entity is the snapshot's player and cg+0x480a0 is not 1. Like the
// muzzle flash the first-person variant is drawn in every view pass, in the weapon space of
// that pass: player 1's casings flew next to the weapon of every other player. Unlike the
// flash the casings are models and live for seconds, so the rule by time does not fit them.
//
// p_brass_world=1: while more than one view is drawn, the function runs with cg+0x480a0 = 1,
// so player 1's casings are the ones of the world model, at player 1's place in every view.
typedef void(__cdecl* EjectBrass_t)(int, int*, unsigned, unsigned, int, int);
static EjectBrass_t oEjectBrass;
static void __cdecl HookEjectBrass(int lc, int* entNum, unsigned weapon, unsigned tag, int leftHand, int flag) {
    uint8_t* cg = *(uint8_t**)engine::kCg;
    if (!cg || g_viewCount < 2 || !ReadIniInt(L"p_brass_world", 1)) {
        oEjectBrass(lc, entNum, weapon, tag, leftHand, flag);
        return;
    }
    int* outside = (int*)(cg + 0x480a0);
    int saved = *outside;
    *outside = 1;
    oEjectBrass(lc, entNum, weapon, tag, leftHand, flag);
    *outside = saved;
}
static const uint8_t kProEjectBrass[] = {0x57, 0x8b, 0x7c, 0x24, 0x0c};

const Detour kFxHooks[] = {
    {"shell eject effect of a weapon (the world model's variant while the split runs)", engine::kCG_EjectWeaponBrass,
     kProEjectBrass, sizeof(kProEjectBrass), (void*)HookEjectBrass, (void**)&oEjectBrass},
    {"view flash of a weapon (remembers the pass that started it)", engine::kCG_PlayWeaponFlash, kProPlayWeaponFlash,
     sizeof(kProPlayWeaponFlash), (void*)HookPlayWeaponFlash, (void**)&oPlayWeaponFlash},
    {"FX_CullSphere (culled only when both players' cameras cull)", 0x005cd6d0, kProFxCullSphere,
     sizeof(kProFxCullSphere), (void*)HookFxCullSphere, (void**)&oFxCullSphere},
    {"FX spawn culling of an element (same, with the spawn range)", 0x005c3bf0, kProFxCullElemForSpawn,
     sizeof(kProFxCullElemForSpawn), (void*)HookFxCullElemForSpawn, (void**)&oFxCullElemForSpawn},
    {"FX box culling of an effect (culled only when both players' cameras cull)", 0x005c9710, kProFxCullEffectBox,
     sizeof(kProFxCullEffectBox), (void*)HookFxCullEffectBox, (void**)&oFxCullEffectBox},
    {"FX cell visibility of an effect box (always visible while the split runs)", 0x005b4bd0, kProFxBoxInVisibleCell,
     sizeof(kProFxBoxInVisibleCell), (void*)HookFxBoxInVisibleCell, (void**)&oFxBoxInVisibleCell},
};
