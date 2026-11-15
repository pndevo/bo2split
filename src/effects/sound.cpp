#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/offsets.h"

// p_snd_listeners=1: one sound listener per player, as the console build's split screen has.
// The sound system keeps four listeners (0x02d5e000, 0x3c bytes each: origin +0, axis +0xc,
// clientNum +0x30, active +0x34, team +0x38) and works every 3D sound out against the nearest
// active one (0x00828940(origin, outDistSq) returns its index; 0x02d5ded4 counts the active
// ones, with one it takes listener 0 without a compare). The cgame sets the listener of its
// local client once per CG_DrawActiveFrame: 0x00518010(lc) queues command 8 for the sound
// thread through 0x00838ed0(lc, clientNum, team, origin, axis) with the refdef origin
// (cg+0x4d8c4) and view axis (cg+0x4d8d4), then sets the "cod_ads" / "cod_menu" snapshots and
// the "plr_stance" context. Every pass runs as local client 0, so the shadow passes would move
// listener 0 to their own player while listeners 1 to 3 stay off: one listener that jumps
// between the players. Here a shadow pass sets the listener of its own index (pass 1 =
// listener 1 ...) with its player's clientNum and leaves the snapshots and the stance context
// to player 1's pass. A listener whose pass did not run for a second is switched off again
// (0x00838ea0(lc) queues command 7, which clears the slot).
typedef void(__cdecl* UpdateListener_t)(int);
static UpdateListener_t oUpdateListener;
static DWORD g_listenerTick[4];
static bool g_listenerOn[4];
static void __cdecl HookUpdateListener(int lc) {
    int pass = (g_inRenderViews && g_cgSwapped && g_viewPass >= 1 && g_viewPass <= 3) ? g_viewPass : 0;
    DWORD now = GetTickCount();
    if (!ReadIniInt(L"p_snd_listeners", 1)) {
        oUpdateListener(lc);
        return;
    }
    if (pass == 0) {
        oUpdateListener(lc);
        for (int i = 1; i < 4; ++i) {
            if (g_listenerOn[i] && (LONG)(now - g_listenerTick[i]) > 1000) {
                g_listenerOn[i] = false;
                engine::SND_QueueDisconnectListener(i);
                Log("[snd] listener %d off (its view pass stopped)", i);
            }
        }
        return;
    }
    const uint8_t* cg = *(const uint8_t**)engine::kCg;
    const uint8_t* cl = pass == 1 ? g_p2.cl : ExtraCl(pass - 2);
    if (!cg || !cl) return;
    int clientNum = *(const int*)(cl + engine::kClPlayerState + engine::kPsClientNum);
    if (clientNum < 0 || clientNum >= 18) return;
    int team = *(const int*)(cg + engine::kCgClientInfo + clientNum * engine::kClientInfoSize + 0x2c);
    float origin[3] = {*(const float*)(cg + 0x4d8c4), *(const float*)(cg + 0x4d8c8), *(const float*)(cg + 0x4d8cc)};
    float axis[9];
    ((void(__cdecl*)(const float*, float*))0x0074bda0)((const float*)(cg + 0x4d8d4), axis);
    engine::SND_QueueSetListener(pass, clientNum, team, origin, axis);
    if (!g_listenerOn[pass]) {
        g_listenerOn[pass] = true;
        Log("[snd] listener %d on: client %d team %d at %.0f %.0f %.0f", pass, clientNum, team, origin[0], origin[1],
            origin[2]);
    }
    g_listenerTick[pass] = now;
}
static const uint8_t kProUpdateListener[] = {0x83, 0xec, 0x30, 0x56, 0x8b, 0x35, 0x5c, 0xf7, 0x05, 0x01};
extern const Detour kSoundListenerHook = {"cgame listener update (one sound listener per player)",
                                          0x00518010,
                                          kProUpdateListener,
                                          sizeof(kProUpdateListener),
                                          (void*)HookUpdateListener,
                                          (void**)&oUpdateListener};

// p_snd_own_fire=1: a shadow player's shots use the weapon's first-person fire sound. The
// fire sound function 0x005345d0 (event data in EAX: +0 lc, +4 entity number and flags, +8
// weapon, +0xc origin, +0x2d "is the player's own view") picks the weapon's player sound
// (0x008e8f60, weapon def +0xb4; 0x008e8fe0 for the alternate set) when the flag is set or
// the entity is the one in cg+0x4851c, else the sound for other characters (0x008e8f30, weapon
// def +0xb0; 0x008e8f90). The events run in player 1's cgame, where players 2 to 4 are other
// characters: their shots would come with the quieter third-person sound. While 0x005345d0
// runs for the entity of a shadow player, the two
// third-person lookups answer with the first-person sound.
static bool g_fireOwn;
volatile LONG g_shadowShots[3]; // shots of players 2 to 4 since their last view pass (the recoil runs there, hud.cpp)
static int ShadowPlayerSlot(int ent) {
    for (int pass = 1; pass <= 3; ++pass) {
        const uint8_t* cl = pass == 1 ? (g_p2.started ? g_p2.cl : nullptr) : ExtraCl(pass - 2);
        if (cl && *(const int*)(cl + engine::kClPlayerState + engine::kPsClientNum) == ent) return pass - 1;
    }
    return -1;
}
static void __cdecl FireSoundEnter(const uint32_t* ev) {
    int slot = ev && !*((const uint8_t*)ev + 0x2d) ? ShadowPlayerSlot((int)(ev[1] & 0xfff)) : -1;
    if (slot >= 0) InterlockedIncrement(&g_shadowShots[slot]);
    g_fireOwn = slot >= 0 && ReadIniInt(L"p_snd_own_fire", 1);
    static int logLeft = 4;
    if (g_fireOwn && logLeft > 0) {
        --logLeft;
        Log("[snd] fire sound of entity %u (weapon %u): first-person sound for a shadow player", ev[1] & 0xfff, ev[2]);
    }
}
static void __cdecl FireSoundLeave() {
    g_fireOwn = false;
}
static void* oFireSound;
static __declspec(naked) void HookFireSound() {
    __asm {
        push eax
        push eax
        call FireSoundEnter
        add esp, 4
        pop eax
        call dword ptr [oFireSound]
        push eax
        call FireSoundLeave
        pop eax
        ret
    }
}
typedef int(__cdecl* WeaponSound_t)(int);
static WeaponSound_t oFireSoundNpc, oFireSoundNpcAlt;
static int __cdecl HookFireSoundNpc(int weapon) {
    return g_fireOwn ? ((WeaponSound_t)0x008e8f60)(weapon) : oFireSoundNpc(weapon);
}
static int __cdecl HookFireSoundNpcAlt(int weapon) {
    return g_fireOwn ? ((WeaponSound_t)0x008e8fe0)(weapon) : oFireSoundNpcAlt(weapon);
}
static const uint8_t kProFireSound[] = {0x83, 0xec, 0x4c, 0x53, 0x55, 0x56, 0x57};
static const uint8_t kProWeaponSound[] = {0x8b, 0x44, 0x24, 0x04, 0x56};
extern const Detour kSoundFireHooks[3] = {
    {"weapon fire sound (first-person sound for the shadow players)", 0x005345d0, kProFireSound, sizeof(kProFireSound),
     (void*)HookFireSound, &oFireSound},
    {"weapon fire sound lookup, other characters", 0x008e8f30, kProWeaponSound, sizeof(kProWeaponSound),
     (void*)HookFireSoundNpc, (void**)&oFireSoundNpc},
    {"weapon fire sound lookup, other characters, alternate set", 0x008e8f90, kProWeaponSound, sizeof(kProWeaponSound),
     (void*)HookFireSoundNpcAlt, (void**)&oFireSoundNpcAlt},
};
