// Engine globals of the server side (svs, the game entity array, the map table, the content pack
// state, the weapon table) and the typed calls of its functions.
// Addresses are those of the one supported game build (image base 0x00400000).
#pragma once

#include <cstdint>

namespace engine {
constexpr uintptr_t kG_GivePlayerWeapon =
    0x00671420; // int (uint8_t* client, weapon, model, options): 0 when the player has the weapon, else adds it
constexpr uintptr_t kDLC_IsPackLoaded = 0x006c9ce0;    // unsigned (bit): the content pack is loaded
constexpr uintptr_t kDLC_IsPackAvailable = 0x006c9df0; // unsigned (index): the content pack of this index is available
constexpr uintptr_t kSV_GameSendServerCommand =
    0x006e7230; // void (entity number, 0, const char* text): a reliable command to a client
constexpr uintptr_t kDLC_IsPackOwnedOnSteam =
    0x00966700;                                    // unsigned (bit): the content pack is subscribed and installed
constexpr uintptr_t kContentPackBits = 0x00c9067c; // the pack's bit is the dword at this address + index * 0x90
constexpr uintptr_t kGEntities = 0x0210f640;       // g_entities, 0x31c bytes per entity, the gclient pointer at +0x154
constexpr uintptr_t kAiDisableSpawnDvarPtr = 0x022868bc; // dvar* ai_disableSpawn
constexpr uintptr_t kDlcLoadedMask = 0x0250e07c;         // mask of the loaded content packs
constexpr uintptr_t kSvTime = 0x0283c030;                // svs.time (server frame time, ms)
constexpr uintptr_t kSvClients = 0x0283c040;             // client_t* svs.clients, stride 0x4e180
constexpr uintptr_t kMapCount = 0x02885210;              // map table: count
constexpr uintptr_t kMapTable =
    0x02885218; // map table: 0x130 bytes per map (name +0, load name +0x20, content pack index +0x6c)
constexpr uintptr_t kWeaponDefs =
    0x0316d0f8; // weapon table: variant def pointers by index (the name is the def's first field)
constexpr uintptr_t kWeaponCountMinusOne = 0x0316dd00; // weapon table of the loaded map: count - 1

// Typed calls: one function per engine function above, with its signature and calling convention.
// Forced inline, so a call compiles to the same code as a cast to a function pointer.
__forceinline int G_GivePlayerWeapon(uint8_t* client, int weapon, int model, int options) {
    return ((int(__cdecl*)(uint8_t*, int, int, int))kG_GivePlayerWeapon)(client, weapon, model, options);
}
__forceinline unsigned DLC_IsPackLoaded(unsigned bit) {
    return ((unsigned(__cdecl*)(unsigned))kDLC_IsPackLoaded)(bit);
}
__forceinline unsigned DLC_IsPackAvailable(unsigned index) {
    return ((unsigned(__cdecl*)(unsigned))kDLC_IsPackAvailable)(index);
}
__forceinline void SV_GameSendServerCommand(int entNum, int arg1, const char* text) {
    ((void(__cdecl*)(int, int, const char*))kSV_GameSendServerCommand)(entNum, arg1, text);
}
__forceinline unsigned DLC_IsPackOwnedOnSteam(unsigned bit) {
    return ((unsigned(__cdecl*)(unsigned))kDLC_IsPackOwnedOnSteam)(bit);
}
} // namespace engine
