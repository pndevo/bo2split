#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/server.h"

// A shadow player standing on a mover (the TranZit bus) is carried with it.
//
// The Pmove wrapper 0x008c7e60 carries a player on its ground entity (playerState +0xa8) before
// it moves the player: 0x00965780(isServer, localClient, ground entity, from time, to time,
// origin, angles) takes the mover's pose at the from time (the playerState's command time) and
// at the to time (the usercmd's server time) and moves origin and angles by the difference. On
// the server the poses come from a ring of 32 samples per mover (found by 0x009650b0, index of
// the newest sample at ring +0x380, 0x1c bytes per sample with its time at +0x18), read by
// 0x00964bd0: a time between two samples is interpolated, a time at or after the newest sample
// gives the newest pose.
//
// Player 1's command times lie behind the newest sample. A shadow player's do not: its usercmds
// follow the snapshot time and step 1 ms per frame past it until the next snapshot (P2PushUsercmd,
// ExtraPush in shadow_client.cpp). Those commands are carried from the newest pose to the newest
// pose, which is no move at all, and the first command after the next sample starts at its own
// command time, some ms after the old sample, so the bus's travel between the old sample and that
// time is lost. Every server frame the player stays a little behind the bus: in a driving bus
// players 2 to 4 slid steadily towards its back.
//
// Here the from time of a shadow player's server-side carry is the pose time the command before it
// carried the player to (its to time, or the newest sample's time when the to time lay beyond
// it), while the ground entity stays the same. The steps then add up to the mover's whole travel.
// Player 1's carries are left alone; for them the two times are the same anyway.
namespace {
typedef void(__cdecl* MoverCarry_t)(char isServer, int localClient, int groundEnt, int fromTime, int toTime,
                                    float* origin, float* angles);
MoverCarry_t oMoverCarry;
constexpr uintptr_t kMoverCarry = 0x00965780;
constexpr uintptr_t kMoverSampleRing = 0x009650b0; // void* (gentity*): the pose ring of a mover, 0 when it has none
constexpr int kRingNewest = 0x380, kRingSampleSize = 0x1c, kRingSampleTime = 0x18;
constexpr int kEntitySize = 0x31c, kEntityClient = 0x154, kPsOrigin = 0x28;
constexpr int kGroundNone = 0x3ff, kGroundWorld = 0x3fe;

// The last carries per shadow player: a command that ended at commandTime left the player carried
// to carriedTo. Several are kept because a carry is not always followed by the command that starts
// at its end: the server also carries a player from its command time on to the server frame's time
// and afterwards goes on from the old command time (a carry 53804 -> 53850, then 53804 -> 53805),
// so a command looks up the carry that ended at its start instead of taking the last one.
struct Carried {
    int ent;         // ground entity of that carry
    int commandTime; // the command's end (its to time)
    int carriedTo;   // the time of the mover's pose the player was carried to
};
constexpr int kCarriedKept = 8;
Carried g_carried[3][kCarriedKept];
int g_carriedNext[3];

// The newest kept carry of that slot on that mover that ended at commandTime.
const Carried* FindCarry(int slot, int ent, int commandTime) {
    for (int i = 1; i <= kCarriedKept; ++i) {
        const Carried& c = g_carried[slot][(g_carriedNext[slot] - i + kCarriedKept) % kCarriedKept];
        if (c.carriedTo && c.ent == ent && c.commandTime == commandTime) return &c;
    }
    return nullptr;
}

int ShadowSlotOfOrigin(const float* origin) {
    for (int slot = 0; slot < 3; ++slot) {
        const uint8_t* ent = (const uint8_t*)engine::kGEntities + (slot + 1) * kEntitySize;
        const uint8_t* client = *(const uint8_t* const*)(ent + kEntityClient);
        if (client && (const uint8_t*)origin == client + kPsOrigin) return slot;
    }
    return -1;
}

void __cdecl HookMoverCarry(char isServer, int localClient, int groundEnt, int fromTime, int toTime, float* origin,
                            float* angles) {
    int slot = isServer == 1 ? ShadowSlotOfOrigin(origin) : -1;
    if (slot < 0) {
        oMoverCarry(isServer, localClient, groundEnt, fromTime, toTime, origin, angles);
        return;
    }
    const uint8_t* ring = nullptr;
    if (groundEnt != kGroundNone && groundEnt != kGroundWorld)
        ring = ((const uint8_t*(__cdecl*)(const void*))kMoverSampleRing)((const uint8_t*)engine::kGEntities +
                                                                         groundEnt * kEntitySize);
    if (!ring) {
        memset(g_carried[slot], 0, sizeof(g_carried[slot]));
        oMoverCarry(isServer, localClient, groundEnt, fromTime, toTime, origin, angles);
        return;
    }
    int newest = *(const int*)(ring + kRingNewest);
    int newestTime = *(const int*)(ring + newest * kRingSampleSize + kRingSampleTime);
    int from = fromTime;
    // The command that ended at this command's start carried the player on the same mover to an
    // earlier pose: go on from there.
    const Carried* last = FindCarry(slot, groundEnt, fromTime);
    if (last && last->carriedTo < fromTime && fromTime - last->carriedTo < 1000) from = last->carriedTo;
    // p_mover_log=N (diagnostic): the next N carries of the shadow players on a mover.
    static int logLeft = -1, logArmed = -1;
    int want = ReadIniInt(L"p_mover_log", 0);
    if (want != logArmed) logArmed = logLeft = want;
    if (logLeft > 0) {
        --logLeft;
        Log("[mover] player %d on %d: command %d -> %d, carried from %d, newest sample %d", slot + 2, groundEnt,
            fromTime, toTime, from, newestTime);
    }
    oMoverCarry(isServer, localClient, groundEnt, from, toTime, origin, angles);
    Carried& c = g_carried[slot][g_carriedNext[slot]];
    g_carriedNext[slot] = (g_carriedNext[slot] + 1) % kCarriedKept;
    c.ent = groundEnt;
    c.commandTime = toTime;
    c.carriedTo = toTime < newestTime ? toTime : newestTime;
}

const uint8_t kProMoverCarry[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};
} // namespace

const Detour kMoverHooks[] = {
    {"mover carry (a shadow player rides the bus without falling behind)", kMoverCarry, kProMoverCarry,
     sizeof(kProMoverCarry), (void*)HookMoverCarry, (void**)&oMoverCarry},
};
