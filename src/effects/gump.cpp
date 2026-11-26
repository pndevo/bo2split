#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/common.h"

// p2_gump=1: a gump slot of its own for player 2.
//
// TranZit keeps the models and textures of each area in a "gump" fastfile
// (zm_transit_gump_busstation, _town, ...). The client script asks for the gump of the area
// its local player is in with the builtins loadgump(name, slot) (0x0050bfc0) and
// flushgump(slot) (0x0050c0f0); the slot is the local client number. loadgump stores the
// name in the dvar gump_name<slot> (pointers at 0x0123c598) and queues the command
// "loadGump <name> 0 <slot>", whose handler ends in DB_LoadGump(name, slot, sync)
// (0x005832f0): four slots (zone mask per slot at 0x01207b00), a gump another slot already
// holds is shared, a slot that holds a different one is flushed first.
//
// Both views run as local client 0, so with the players in different areas the script asks
// for both gumps in slot 0, in turn with every snapshot: the game loads and unloads the two
// fastfiles without end, and the models of the area just unloaded show as black default
// boxes or not at all, in both halves. The
// requests all arrive in the first pass on player 1's cgame block (the script threads run
// there, whichever pass noticed the change), so the caller does not tell whose gump it is.
// Here the slot is chosen by name instead: p2_gump = number of slots used as a cache (2 by
// default, up to 4). A gump that is already in a slot keeps it, a new one takes an empty
// slot or the one whose gump was asked for longest ago. The load itself stays the engine's.
namespace {
typedef const char*(__cdecl* Va_t)(const char* fmt, ...);
typedef int(__cdecl* GetCmdLocalClient_t)();
constexpr uintptr_t kVa = 0x0075e5c0, kGetCmdLocalClient = 0x006be410;
constexpr int kClientScripts = 1; // script instance of the client scripts

char g_slotName[4][64];
DWORD g_slotTick[4];

int GumpSlot(int slot, const char* name) {
    int n = ReadIniInt(L"p2_gump", 2);
    if (n <= 0 || slot != 0) return slot;
    if (n > 4) n = 4;
    DWORD now = GetTickCount();
    int to = -1;
    for (int i = 0; i < n; ++i)
        if (_stricmp(g_slotName[i], name) == 0) {
            to = i;
            break;
        }
    if (to < 0) {
        for (int i = 0; i < n; ++i)
            if (!g_slotName[i][0]) {
                to = i;
                break;
            }
        if (to < 0) {
            to = 0;
            for (int i = 1; i < n; ++i)
                if ((LONG)(g_slotTick[i] - g_slotTick[to]) < 0) to = i;
        }
        Log("[gump] %s takes slot %d (held '%s'; slots: '%s' '%s' '%s' '%s')", name, to, g_slotName[to], g_slotName[0],
            g_slotName[1], g_slotName[2], g_slotName[3]);
        strncpy_s(g_slotName[to], name, _TRUNCATE);
    }
    g_slotTick[to] = now;
    return to;
}
} // namespace

static void* oScrLoadGump;
static void __cdecl HookScrLoadGump() {
    const char* name = engine::ScriptArgString(kClientScripts, 0);
    if (!name || !*name) return;
    int slot = engine::ScriptArgInt(kClientScripts, 1);
    if (slot < 0 || slot > 3) slot = 0;
    slot = GumpSlot(slot, name);
    engine::DvarSetString(*(void**)(engine::kGumpNameDvars + slot * 4), name);
    const char* text = ((Va_t)kVa)("loadGump %s %u %u\n", name, 0, slot);
    engine::Cbuf_AddText(((GetCmdLocalClient_t)kGetCmdLocalClient)(), text);
}
static void* oScrFlushGump;
static void __cdecl HookScrFlushGump() {
    int slot = engine::ScriptArgInt(kClientScripts, 0);
    if (slot < 0 || slot > 3) slot = 0;
    Log("[gump] flushgump slot %d (held '%s')", slot, g_slotName[slot]);
    g_slotName[slot][0] = 0;
    engine::DvarSetString(*(void**)(engine::kGumpNameDvars + slot * 4), "");
    engine::FlushGumpSlot(slot);
}
static const uint8_t kProScrGump[] = {0x56, 0x6a, 0x00, 0x6a, 0x01}; // push esi; push 0; push 1 (both builtins)

// The gump fade. While a gump loads, the cgame hides the view behind a rectangle of one colour:
// the state lies in the cgame block (cg+0x87668: 0 off, 1 started, 2 held while the engine's
// "a gump is loading" word 0x0279fa1c is set, 3 fading out; colour at +0x8766c, alpha at
// +0x87678, start time at +0x8767c) and 0x004b0fd0 draws it first in the 2D of the view (dark
// grey, 0.1 0.1 0.1; with no gump loading it is opaque for 750 ms after a start and gone 250 ms
// later). It is started by the engine when a spectating player switches the watched player
// (0x004b0f60) and by the client script builtin sethidegumpalpha(localClientNum [, colour])
// (0x0050c130).
//
// The engine runs the client script threads in every view pass (CG_AddPacketEntities
// 0x004c0ff0 ends in the script's time step 0x007f3c40), and every pass is local client 0: the
// script meets another player in each pass. On TranZit (zclassic) it calls the builtin in every
// pass whose player is dead and watching while the player of the pass before it is not, and
// the other way round, so in every frame; the call lands in the cgame block of that pass. Those
// views would stay dark grey with only the HUD on top, the view of a living player among them
// (players 3 and 4 with player 3 dead, players 1 and 3 with players 3 and 4 dead).
//
// While more than one view is drawn the builtin starts no fade. What the fade is there to hide
// does not occur then: the watched player has a view of its own, whose pass has asked for the
// gump of its area already. The hook sits behind the builtin's argument check (the first five
// bytes of the builtin end inside a call); the stack is as on entry there, so returning from the
// hook returns to the script.
static void* oScrHideGump;
static void __cdecl HookScrHideGump() {
    if (g_p2.started && g_viewCount > 1) {
        static bool logged;
        if (!logged) {
            logged = true;
            Log("[gump] sethidegumpalpha of the client script starts no fade while %d views are drawn",
                (int)g_viewCount);
        }
        return;
    }
    ((void(__cdecl*)())oScrHideGump)();
}
static const uint8_t kProScrHideGump[] = {0xf3, 0x0f, 0x10, 0x05, 0x68, 0xa2, 0xb0, 0x00}; // movss xmm0, [0x00b0a268]

const Detour kGumpHooks[] = {
    {"CScr_LoadGump (gump slots as a cache)", 0x0050bfc0, kProScrGump, sizeof(kProScrGump), (void*)HookScrLoadGump,
     &oScrLoadGump},
    {"CScr_FlushGump (gump slots as a cache)", 0x0050c0f0, kProScrGump, sizeof(kProScrGump), (void*)HookScrFlushGump,
     &oScrFlushGump},
    {"CScr_SetHideGumpAlpha (no gump fade from the script while several views are drawn)", 0x0050c160, kProScrHideGump,
     sizeof(kProScrHideGump), (void*)HookScrHideGump, &oScrHideGump},
};
