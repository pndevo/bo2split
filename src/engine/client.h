// Engine globals of the client and the cgame (the client structures, the cgame block and its
// entity array, input state, the gamepad state) and the typed calls of the entry points the shadow
// players use.
// Addresses are those of the one supported game build (image base 0x00400000).
#pragma once

#include <cstdint>

namespace engine {
constexpr uintptr_t kCompareClientFields =
    0x00493a30; // void (set, lc, entNum, oldBase, newBase, force, demoJump): queues the changed client fields of a set
constexpr uintptr_t kBG_FindPlayerWeapon =
    0x0049a0e0; // int (uint8_t* client, int weapon): the entry of the weapon in the playerState weapon list, 0 if it is not there
constexpr uintptr_t kCG_ScoresUp = 0x004ac5a0;                // void (int lc): closes the scoreboard
constexpr uintptr_t kCG_ScoresDown = 0x004ac5e0;              // void (int lc): opens the scoreboard
constexpr uintptr_t kCG_ProcessPlayerNotetracks = 0x004ba6e0; // void (int lc, int clientNum)
constexpr uintptr_t kCG_AdvanceDObjAnimByFrametime =
    0x004bb0b0; // void (cg, dobj, flags): advances the animation of a DObj by the frame time
constexpr uintptr_t kCG_CalcEntityLerpPositions = 0x004bfc40; // void (int lc, cent)
constexpr uintptr_t kCG_AddEntityByType = 0x004c0750;         // void (lc, cent, cg): the add by type
constexpr uintptr_t kCG_SendHudInfoEvent = 0x0050fe30;        // void (int lc, int type)
constexpr uintptr_t kCG_SelectWeapon = 0x005369a0;            // void (int lc, playerState*, int weapon)
constexpr uintptr_t kCG_ActionSlotDown = 0x0053a3e0;          // void (int lc, int slot 0..3)
constexpr uintptr_t kCG_WeapNext_f = 0x0053be10;              // CG_WeapNext_f
constexpr uintptr_t kCG_WeapPrev_f = 0x0053be50;              // CG_WeapPrev_f
constexpr uintptr_t kCL_SendCmd =
    0x00541180; // void (int lc): the command packet send step, reads the ready-to-send check first
constexpr uintptr_t kCL_CheckForResend = 0x00550c90; // void (int lc): getchallenge / connect
constexpr uintptr_t kCL_ToggleMenu = 0x00556870;     // void (int lc): opens or closes menu 3 in state 10
constexpr uintptr_t kCom_GetClientDObj = 0x006cab90; // void* (handle, lc): the DObj of an entity through the handle map
constexpr uintptr_t kGetLoopbackPacket =
    0x006d3290; // int (int channel, from, msg): the next packet of a loopback channel, 0 when empty
constexpr uintptr_t kSND_QueueDisconnectListener = 0x00838ea0; // void (int lc): queues sound command 7
constexpr uintptr_t kSND_QueueSetListener =
    0x00838ed0;                                 // void (lc, clientNum, team, origin, axis): queues sound command 8
constexpr uintptr_t kBG_GetWeapon = 0x008db250; // int (playerState*): the weapon the playerState holds
constexpr uintptr_t kBG_TakePlayerWeapon =
    0x008db350; // void (client, weapon): removes the weapon from the playerState list
constexpr uintptr_t kBG_WeaponFireRecoil =
    0x008e0a90; // void (ps, weapon kick, view kick velocity, ..): the weapon kick of one shot
constexpr uintptr_t kBG_GetWeaponDef =
    0x008e46a0; // uint8_t* (int weapon): the weapon def (+0x2c inventory type, 0 = primary)
constexpr uintptr_t kBG_IsDualWield = 0x008e8980; // bool (int weapon): true for a dual wield weapon
constexpr uintptr_t kCG_SendHudUpdateEvents =
    0x0094ded0; // void (int lc, int flag): sends the hud_update events for the changed playerState fields
constexpr uintptr_t kPadGlyphTable = 0x00c85420;     // 16 entries of { int key; const char* name }
constexpr uintptr_t kPrimaryController = 0x00c9025c; // primary controller
constexpr uintptr_t kFireRecoilParam = 0x00ca1588;   // last argument of BG_WeaponFireRecoil in the engine's own call
// The pad turn rates of player 1 (0x0048d4e0), float value of each dvar at +0x18.
constexpr uintptr_t kAimInputGraphIndexDvar = 0x00f85e54;   // dvar* aim_input_graph_index (int)
constexpr uintptr_t kAimInputGraphEnabledDvar = 0x00f85e6c; // dvar* aim_input_graph_enabled (bool)
constexpr uintptr_t kAimInputGraphs =
    0x00f85e80; // the stick response graphs: name, knots at +0x40, knot count at +0x140, scale at +0x144
constexpr size_t kAimInputGraphSize = 0x148;
constexpr int kAimInputGraphCount = 4;
constexpr uintptr_t kAimScaleViewAxisDvar = 0x00f86400; // dvar* aim_scale_view_axis (bool)
constexpr uintptr_t kCG_PlayWeaponFlash =
    0x005304a0; // void (lc, dobj handle, weapon, bool view weapon, tag name): plays the weapon's flash effect on that tag
constexpr uintptr_t kCG_EjectWeaponBrass =
    0x00533b20; // void (lc, int* entity number, weapon, tag name, bool left hand, bool): plays the weapon's shell eject effect
constexpr uintptr_t kBG_GetViewFlashEffect =
    0x008e9100; // uint8_t* (int weapon): the effect definition of the weapon's first-person muzzle flash, or null
constexpr uintptr_t kCG_PlayEffectOnTag =
    0x004c2250; // int (lc, effect def, dobj handle, tag name, weapon def + 0x7c, axis): plays the effect on that tag of the DObj, returns the effect's handle (0 = none)
constexpr uintptr_t kFX_EffectFromHandle = 0x005c7850; // uint8_t* (fx system, handle): the effect of a handle
// The bolt word of an effect: what it is bound to. Bits 8 and 9 the kind (0 = nothing, 1 = a
// bone of a DObj: every update of the effect, 0x005c9290, reads the bone's place again), bits
// 10 to 20 the DObj handle, bits 22 to 30 the bone. kFxBoltNone is what the engine writes when
// the bone is gone: the effect keeps the place it has.
constexpr int kFxEffectBolt = 0xb4;
constexpr unsigned kFxBoltKindMask = 0x00000300;
constexpr unsigned kFxBoltNone = 0x7fdffc00;
constexpr uintptr_t kTagFlashName = 0x02405500; // uint16: the script string "tag_flash"
constexpr int kViewWeaponDObjHandle = 0x700;    // plus the local client: the first-person weapon's DObj
constexpr uintptr_t kCG_PlayRumbleOnClient =
    0x004ef8a0; // void (lc, const char* name): starts a rumble on the player of the snapshot
constexpr uintptr_t kGraphFloat_GetValue =
    0x006cd610; // float (graph*, float fraction): the graph's value at the fraction
constexpr uintptr_t kAimAccelTurnrateEnabledDvar = 0x00f85e24; // dvar* aim_accel_turnrate_enabled (bool)
constexpr uintptr_t kAimTurnrateYawDvar = 0x00f85e34;          // dvar* aim_turnrate_yaw
constexpr uintptr_t kAimTurnratePitchAdsDvar = 0x00f85e64;     // dvar* aim_turnrate_pitch_ads
constexpr uintptr_t kAimTurnratePitchDvar = 0x00f863b8;        // dvar* aim_turnrate_pitch
constexpr uintptr_t kAimTurnrateYawAdsDvar = 0x00f863d8;       // dvar* aim_turnrate_yaw_ads
constexpr uintptr_t kAimAccelTurnrateLerpDvar = 0x00f86408;    // dvar* aim_accel_turnrate_lerp
constexpr uintptr_t kAimState = 0x00f86420;                    // aim state of local client 0
constexpr size_t kAimStateSize = 0x12d0;                       // bytes of aim state per local client
constexpr size_t kAimStateFovTurnScale =
    0xcc; // float: tanHalfFovY of the view * 2.0929, the turn rate's field of view factor
// The blur a script puts on a player's view (GSC setblur), one state per local client outside
// the cgame block. The view setup reads its current value into cg+0x4dae8 in every pass
// (0x004b03a0), the frame of a pass advances it (0x004b0500).
constexpr uintptr_t kScriptBlur = 0x00fbc668; // state of local client 0
constexpr size_t kScriptBlurSize = 0x1c;      // bytes per local client
constexpr uintptr_t kCG_ScriptBlurFade =
    0x004b03c0; // void (lc, ms, float value): that blur now, fading out over the time
constexpr uintptr_t kCG_ScriptBlurSet =
    0x004b0330; // void (lc, ms, float value, int time type, int priority): blur towards the value over the time
constexpr uintptr_t kCG_ScriptBlurReset = 0x004b0420; // void (lc): no blur
// bool (lc, slot, style, const char* name, int ms): blends the vision set slot to the named set
// over the time. The cgame calls it with slot 7 and style 3 when config string 0xaf2 (the last
// stand set: "<name> <ms>") changes (0x00526790).
constexpr uintptr_t kCG_VisionSetStartLerpTo = 0x005264f0;
constexpr int kCsVisionSetLastStand = 0xaf2;
constexpr uintptr_t kOverheadNameList = 0x00fbc7f8;  // overhead name list: 16 bytes per entry, the entity number first
constexpr uintptr_t kOverheadNameCount = 0x00fbd2f0; // overhead name list: count
constexpr uintptr_t kViewModel = 0x00fc83e8;         // pointer to the viewmodel block (0xc0 bytes)
constexpr uintptr_t kVmTraceDvar = 0x00fc8404;       // dvar* read by the viewmodel trace (float value at +0x18)
constexpr uintptr_t kCgs = 0x0105f738;               // pointer to cgs
constexpr uintptr_t kCg = 0x0105f75c;                // pointer to player 1's cgame block
constexpr uintptr_t kCgEntities = 0x01060e48;        // pointer to the cgame entity array (0x380 per entity)
constexpr uintptr_t kRumbleState = 0x01064690;       // rumble state of local client 0 (0x5250 bytes per local client)
constexpr uintptr_t kKeyStates =
    0x010df614; // per key, stride 0x10: the first dword is nonzero once the engine saw the key go down
constexpr uintptr_t kUiState =
    0x010e7e10; // UI state of local client 0 (stride 0x458): flags (bit 0 = active), +8 connection state, +0x10 migration
constexpr uintptr_t kKeyCatchers = 0x010e7e14; // key catchers
constexpr uintptr_t kClClock =
    0x010e84b0; // int: the client's clock in ms, moves on every frame, also while the match is paused
constexpr uintptr_t kLocalClientCount = 0x010e833c; // stored count after hunk allocation
constexpr uintptr_t kClPtr = 0x010e8344;            // clients (0x8a3e0 per local client)
constexpr uintptr_t kClcPtr = 0x010e8348;           // clientConnections (0x358a8)
constexpr uintptr_t kFxSystem = 0x020d2864;         // pointer to the FX system of local client 0
constexpr uintptr_t kCtrlMap = 0x024806f0;          // slot 0: flags, +4 lcn, +8 controller, +0x10 netsrc
constexpr uintptr_t kDObjHandleMap = 0x0254d0d0;    // client DObj handle map (short per entity number)
constexpr uintptr_t kGpadEnabledDvar = 0x02a884e4;  // dvar* gpad_enabled
constexpr uintptr_t kGpadConnected = 0x02a88504;    // byte
constexpr uintptr_t kGpadDebugDvar = 0x02a88514;    // dvar* gpad_debug
constexpr uintptr_t kGpadActive = 0x02a88518;       // byte
constexpr uintptr_t kGpadButtons = 0x02a8851a;      // processed pad state: buttons word
constexpr uintptr_t kGpadLt = 0x02a88520;           // processed pad state: left trigger (float)
constexpr uintptr_t kGpadRt = 0x02a88524;           // processed pad state: right trigger (float)
constexpr uintptr_t kGpadLx = 0x02a88530;           // processed pad state: left stick x (float)
constexpr uintptr_t kGpadLy = 0x02a88534;           // processed pad state: left stick y (float)
constexpr uintptr_t kGpadRx = 0x02a88538;           // processed pad state: right stick x (float)
constexpr uintptr_t kGpadRy = 0x02a8853c;           // processed pad state: right stick y (float)
constexpr uintptr_t kGpadCountdown = 0x02a88598;    // int
constexpr uintptr_t kWindowHandle = 0x02a8f148;     // HWND of the game window

// Typed calls: one function per engine function above, with its signature and calling convention.
// Forced inline, so a call compiles to the same code as a cast to a function pointer.
__forceinline void CompareClientFields(int set, int lc, int entNum, const uint8_t* oldBase, const uint8_t* newBase,
                                       int force, int demoJump) {
    ((void(__cdecl*)(int, int, int, const uint8_t*, const uint8_t*, int, int))kCompareClientFields)(
        set, lc, entNum, oldBase, newBase, force, demoJump);
}
__forceinline int BG_FindPlayerWeapon(uint8_t* client, int weapon) {
    return ((int(__cdecl*)(uint8_t*, int))kBG_FindPlayerWeapon)(client, weapon);
}
__forceinline float GraphFloat_GetValue(const void* graph, float fraction) {
    return ((float(__cdecl*)(const void*, float))kGraphFloat_GetValue)(graph, fraction);
}
__forceinline void CG_ScoresUp(int lc) {
    ((void(__cdecl*)(int))kCG_ScoresUp)(lc);
}
__forceinline void CG_ScoresDown(int lc) {
    ((void(__cdecl*)(int))kCG_ScoresDown)(lc);
}
__forceinline void CG_ProcessPlayerNotetracks(int lc, int clientNum) {
    ((void(__cdecl*)(int, int))kCG_ProcessPlayerNotetracks)(lc, clientNum);
}
__forceinline void CG_AdvanceDObjAnimByFrametime(void* cg, void* dobj, int flags) {
    ((void(__cdecl*)(void*, void*, int))kCG_AdvanceDObjAnimByFrametime)(cg, dobj, flags);
}
__forceinline void CG_CalcEntityLerpPositions(int lc, void* cent) {
    ((void(__cdecl*)(int, void*))kCG_CalcEntityLerpPositions)(lc, cent);
}
__forceinline void CG_AddEntityByType(int lc, void* cent, void* cg) {
    ((void(__cdecl*)(int, void*, void*))kCG_AddEntityByType)(lc, cent, cg);
}
__forceinline void CG_SendHudInfoEvent(int lc, int type) {
    ((void(__cdecl*)(int, int))kCG_SendHudInfoEvent)(lc, type);
}
__forceinline void CG_SelectWeapon(int lc, uint8_t* ps, int weapon) {
    ((void(__cdecl*)(int, uint8_t*, int))kCG_SelectWeapon)(lc, ps, weapon);
}
__forceinline void CG_ActionSlotDown(int lc, int slot) {
    ((void(__cdecl*)(int, int))kCG_ActionSlotDown)(lc, slot);
}
__forceinline void CG_WeapNext_f() {
    ((void(__cdecl*)())kCG_WeapNext_f)();
}
__forceinline void CG_WeapPrev_f() {
    ((void(__cdecl*)())kCG_WeapPrev_f)();
}
__forceinline void CL_SendCmd(int lc) {
    ((void(__cdecl*)(int))kCL_SendCmd)(lc);
}
__forceinline void CL_CheckForResend(int lc) {
    ((void(__cdecl*)(int))kCL_CheckForResend)(lc);
}
__forceinline void CL_ToggleMenu(int lc) {
    ((void(__cdecl*)(int))kCL_ToggleMenu)(lc);
}
__forceinline void* Com_GetClientDObj(int handle, int lc) {
    return ((void*(__cdecl*)(int, int))kCom_GetClientDObj)(handle, lc);
}
__forceinline int GetLoopbackPacket(int channel, void* from, void* msg) {
    return ((int(__cdecl*)(int, void*, void*))kGetLoopbackPacket)(channel, from, msg);
}
__forceinline void SND_QueueDisconnectListener(int lc) {
    ((void(__cdecl*)(int))kSND_QueueDisconnectListener)(lc);
}
__forceinline void SND_QueueSetListener(int lc, int clientNum, int team, const float* origin, const float* axis) {
    ((void(__cdecl*)(int, int, int, const float*, const float*))kSND_QueueSetListener)(lc, clientNum, team, origin,
                                                                                       axis);
}
__forceinline int BG_GetWeapon(const uint8_t* ps) {
    return ((int(__cdecl*)(const uint8_t*))kBG_GetWeapon)(ps);
}
__forceinline void BG_TakePlayerWeapon(uint8_t* client, int weapon) {
    ((void(__cdecl*)(uint8_t*, int))kBG_TakePlayerWeapon)(client, weapon);
}
__forceinline void BG_WeaponFireRecoil(uint8_t* ps, uint8_t* weaponKick, uint8_t* viewKickVelocity, uint8_t* arg3,
                                       int flag, uint8_t* arg5, const void* arg6) {
    ((void(__cdecl*)(uint8_t*, uint8_t*, uint8_t*, uint8_t*, int, uint8_t*, const void*))kBG_WeaponFireRecoil)(
        ps, weaponKick, viewKickVelocity, arg3, flag, arg5, arg6);
}
__forceinline uint8_t* BG_GetViewFlashEffect(int weapon) {
    return ((uint8_t*(__cdecl*)(int))kBG_GetViewFlashEffect)(weapon);
}
__forceinline int CG_PlayEffectOnTag(int lc, uint8_t* effectDef, int dobjHandle, unsigned tagName,
                                     uint8_t* weaponDefPart, float* axis) {
    return ((int(__cdecl*)(int, uint8_t*, int, unsigned, uint8_t*, float*))kCG_PlayEffectOnTag)(
        lc, effectDef, dobjHandle, tagName, weaponDefPart, axis);
}
__forceinline uint8_t* FX_EffectFromHandle(uint8_t* fxSystem, int handle) {
    return ((uint8_t*(__cdecl*)(uint8_t*, int))kFX_EffectFromHandle)(fxSystem, handle);
}
__forceinline uint8_t* BG_GetWeaponDef(int weapon) {
    return ((uint8_t*(__cdecl*)(int))kBG_GetWeaponDef)(weapon);
}
__forceinline bool BG_IsDualWield(int weapon) {
    return ((bool(__cdecl*)(int))kBG_IsDualWield)(weapon);
}
__forceinline void CG_SendHudUpdateEvents(int lc, int flag) {
    ((void(__cdecl*)(int, int))kCG_SendHudUpdateEvents)(lc, flag);
}
} // namespace engine
