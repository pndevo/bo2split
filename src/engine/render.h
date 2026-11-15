// Engine globals of the renderer (the frontend and backend data blocks, the command buffers, the
// scene lists, the visibility data, the render target table, the lighting caches) and the typed calls
// of its functions.
// Addresses are those of the one supported game build (image base 0x00400000).
#pragma once

#include <cstdint>

namespace engine {
constexpr uintptr_t kR_AddCmdSetViewport = 0x0041e680; // void (x, y, w, h): the viewport command
constexpr uintptr_t kFX_CullBoxAgainstPlanes =
    0x005cd740; // char (planes, count, mins, maxs): 1 = the box is outside one of the planes
constexpr uintptr_t kViewPlanes =
    0x0334577c; // frustum planes kept from the last scene, 0x360 per local client (planes at +4, count at +0x11c)
constexpr uintptr_t kTraceLinkedPtrs =
    0x03346500; // pointer per dpvs client; the viewmodel trace logs its element 0x3ff as "linked" (role not known)
constexpr uintptr_t kFrontendData = 0x033487d8;   // pointer to the frontend data block
constexpr uintptr_t kCmdBuffer = 0x033487dc;      // pointer to the current command buffer
constexpr uintptr_t kDrawSurfCount0 = 0x034285dc; // draw surface count of list 0
constexpr uintptr_t kDrawSurfCount6 = 0x034285e8; // draw surface count of list 6
constexpr uintptr_t kDrawSurfCount1 = 0x034285f8; // draw surface count of list 1
constexpr uintptr_t kDrawSurfCount2 = 0x03428600; // draw surface count of list 2
constexpr uintptr_t kDrawSurfCount3 = 0x0342861c; // draw surface count of list 3
constexpr uintptr_t kDrawSurfCount4 = 0x03428628; // draw surface count of list 4
constexpr uintptr_t kDrawSurfCount5 = 0x03428630; // draw surface count of list 5
constexpr uintptr_t kSceneDObjCount = 0x0345b404; // scene DObj count
constexpr uintptr_t kSceneEntities = 0x0345b408;  // scene entities, 0x8c bytes each
constexpr uintptr_t kSceneVisBytes =
    0x0347e408; // visibility bytes per scene DObj, one list of 0x400 per camera (camera first)
constexpr uintptr_t kSceneModelCount = 0x0348000c; // scene model count
constexpr uintptr_t kDpvsClient = 0x034adc70;      // dpvs client
constexpr uintptr_t kTraceVisPtr0 =
    0x034adc74; // pointer; the viewmodel trace logs its element 0x3ff as "vis" (role not known)
constexpr uintptr_t kTraceVisPtr1 =
    0x034adc78; // pointer; the viewmodel trace logs its element 0x3ff as "vis" (role not known)
constexpr uintptr_t kTraceVisPtr2 =
    0x034adc7c; // pointer; the viewmodel trace logs its element 0x3ff as "vis" (role not known)
constexpr uintptr_t kSceneIndexMap =
    0x034adc94; // short per entity number: the scene entity index, -1 = not in the scene yet
constexpr uintptr_t kCmdBuffers = 0x034b40d0;               // the two command buffers, 0x1c bytes each (size at +0x10)
constexpr uintptr_t kLightmapOnlyEntSurfsDvar = 0x034c54ac; // dvar* r_lightmapOnlyEntSurfs (current value at +0x18)
constexpr uintptr_t kTraceSpecialEntnum =
    0x034c7624;                                  // word the viewmodel trace logs as "special entnum" (role not known)
constexpr uintptr_t kRtWidth = 0x03504300;       // render target width
constexpr uintptr_t kRtHeight = 0x03504304;      // render target height
constexpr uintptr_t kScreenWidth = 0x03504308;   // screen width of the renderer
constexpr uintptr_t kScreenHeight = 0x0350430c;  // screen height of the renderer
constexpr uintptr_t kTraceViewIdx = 0x03509dd0;  // word the draw surface log prints as "viewIdx" (role not known)
constexpr uintptr_t kRefdefStatics = 0x03509e00; // static refdef globals (0x03509e00..0x0350a200)
constexpr uintptr_t kBackendData = 0x0350c884;   // pointer to the backend view data
constexpr uintptr_t kSkinCacheAvailable = 0x03942d50; // skin-cache "available" word
constexpr uintptr_t kSkinCacheRearm = 0x03942d80;     // word set to 1 when the skin cache is re-armed for a view
constexpr uintptr_t kRenderTargetTable =
    0x03944a24; // render target table, 0x14 bytes per entry (the backbuffer view is entry 1)
constexpr uintptr_t kTraceStaticModelLights =
    0x03954c80; // word the scene log prints as the second number of "smodel light" (role not known)
constexpr uintptr_t kTraceStaticModelLightsInUse =
    0x03954c84; // word the scene log prints as the first number of "smodel light" (role not known)
constexpr uintptr_t kTraceStaticModelStealable =
    0x03954c88; // word the scene log prints as "stealable" (role not known)
constexpr uintptr_t kTraceEntityLightPoolFull =
    0x03954e30; // word the scene log prints as "entity pool full" (role not known)

// Typed calls: one function per engine function above, with its signature and calling convention.
// Forced inline, so a call compiles to the same code as a cast to a function pointer.
__forceinline void R_AddCmdSetViewport(int x, int y, int w, int h) {
    ((void(__cdecl*)(int, int, int, int))kR_AddCmdSetViewport)(x, y, w, h);
}
__forceinline char FX_CullBoxAgainstPlanes(const uint8_t* planes, int count, const float* mins, const float* maxs) {
    return ((char(__cdecl*)(const uint8_t*, int, const float*, const float*))kFX_CullBoxAgainstPlanes)(planes, count,
                                                                                                       mins, maxs);
}
} // namespace engine
