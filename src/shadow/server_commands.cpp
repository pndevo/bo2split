// The server commands of the shadow players. The cgame runs the commands of the connection
// that is player 1's; the weapon switches the server sends to a shadow player are carried out
// here on that player's own connection.

#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "view/view_private.h"

// p_srvcmd_weapon=1: the server's weapon switch for a shadow player. The server tells a client
// to change weapon with the reliable command "h <weapon id>" (GSC switchtoweapon); the cgame
// runs it in CG_DispatchServerCommand (0x00511740, case 'h': CG_SelectWeapon 0x005369a0 when the
// snapshot playerState holds the weapon). CG_ExecuteNewServerCommands (0x00512230) reads the
// commands of the one client connection at *0x010e8348, which is player 1's while the cgame
// runs, so a shadow player's commands would be stored and never run. A perk machine takes the
// points, gives the bottle, calls switchtoweapon and waits for "weapon_change_complete": without
// the switch the bottle never comes up and the script waits for good with is_drinking set, so
// that player could buy nothing any more. The commands are read from the shadow player's own
// connection (clc+0x4344 newest sequence, text at clc+0x434c + (sequence & 0x7f) * 0x400) in
// its pass, and an "h" selects the weapon on its cgame block once its playerState holds that
// weapon.
// p_srvcmd_log=N (diagnostic): N lines with the commands a shadow player received.
// p_srvcmd_hold_ms: the usercmd side keeps the weapon of the last switch for that long (see
// shadow_client.cpp); g_srvWeaponSwitched tells an extra player's usercmd to follow its block.
int g_srvWeapon[3];
DWORD g_srvWeaponTick[3];
volatile LONG g_srvWeaponSwitched[3];
void ShadowServerCommands(int slot, uint8_t* cg) {
    static int last[3] = {-1, -1, -1}, pending[3];
    static DWORD pendingUntil[3];
    static int logLeft = -1, logArmed = -1;
    {
        int v = ReadIniInt(L"p_srvcmd_log", 0);
        if (v != logArmed) {
            logArmed = v;
            logLeft = v;
        }
    }
    if (slot < 0 || slot > 2) return;
    const uint8_t* clc = ShadowSlotClc(slot);
    if (!clc) {
        last[slot] = -1;
        return;
    }
    int seq = *(const int*)(clc + 0x4344);
    if (last[slot] < 0 || seq < last[slot] || seq - last[slot] > 0x7f)
        last[slot] = seq; // first look, new match, or commands cycled out
    bool act = ReadIniInt(L"p_srvcmd_weapon", 1) != 0;
    for (int s = last[slot] + 1; s <= seq; ++s) {
        const char* text = (const char*)(clc + 0x434c + (s & 0x7f) * 0x400);
        if (logLeft > 0) {
            --logLeft;
            Log("[srvcmd] player %d command %d: %.120s", slot + 2, s, text);
        }
        if (act && text[0] == 'h' && text[1] == ' ') {
            pending[slot] = atoi(text + 2);
            pendingUntil[slot] = GetTickCount() + 3000;
        }
        // "2 3 <id>": config string 3, the server id. "Restart level" (fast_restart) raises it
        // by one, and from then on the server drops every message of a client that still
        // sends the old id: the player takes no input and raises no weapon (its playerState
        // on the server keeps the command time of the restart). The cgame takes the new id
        // into the client struct of the connection that is player 1's (0x005592f0: config
        // string 3 -> cl+0x2abc); a shadow player's own client struct gets it here.
        if (text[0] == '2' && text[1] == ' ' && atoi(text + 2) == 3) {
            const char* value = strchr(text + 2, ' ');
            uint8_t* cl = ShadowSlotCl(slot);
            if (value && cl) {
                int id = atoi(value + 1);
                Log("[srvcmd] player %d: server id %u -> %d", slot + 2, *(const uint32_t*)(cl + shadowcl::kClServerId),
                    id);
                *(uint32_t*)(cl + shadowcl::kClServerId) = (uint32_t)id;
                ShadowHudResend(slot); // the restart reset the HUD: its lines are sent once more
            }
        }
    }
    last[slot] = seq;
    if (!pending[slot]) return;
    uint8_t* ps = cg + engine::kCgPredictedPs;
    if (engine::BG_FindPlayerWeapon(ps, pending[slot])) {
        int before = *(const int*)(cg + engine::kCgWeaponSelect);
        engine::CG_SelectWeapon(0, ps, pending[slot]);
        Log("[srvcmd] player %d: server switched the weapon to %d, weaponSelect %d -> %d", slot + 2, pending[slot],
            before, *(const int*)(cg + engine::kCgWeaponSelect));
        g_srvWeapon[slot] = pending[slot];
        g_srvWeaponTick[slot] = GetTickCount();
        InterlockedExchange(&g_srvWeaponSwitched[slot], 1);
        pending[slot] = 0;
    } else if ((LONG)(GetTickCount() - pendingUntil[slot]) >= 0) {
        Log("[srvcmd] player %d: weapon %d of the server's switch never came into the playerState, dropped", slot + 2,
            pending[slot]);
        pending[slot] = 0;
    }
}
