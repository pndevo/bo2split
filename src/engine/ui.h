// Engine globals of the user interface (the LUI state and its roots, the HUD caches, the screen size,
// the screen placements) and the typed calls of the LUI and text functions.
// Addresses are those of the one supported game build (image base 0x00400000).
#pragma once

#include <cstdint>

namespace engine {
constexpr uintptr_t kCG_DrawTextAligned =
    0x004b8020; // int (lc, scrPlace, x, y, xScale, yScale, text, colour, style, font): text aligned by style
constexpr uintptr_t kScrPlace_SetupViewport =
    0x0057e7d0; // void (place, x, y, w, h, aspect, flag): sets up a screen placement
constexpr uintptr_t kUI_SetActiveMenu = 0x0070d370; // int (int lc, int menuId)
constexpr uintptr_t kLUI_EndEvent = 0x0092a600;     // void (L): sends the event
constexpr uintptr_t kLUI_SetTableBool =
    0x0092ce00; // void (const char* key, int value, L): a bool field of the event table
constexpr uintptr_t kLUI_SetTableNumber =
    0x0092ce40; // void (const char* key, int value, L): a number field of the event table
constexpr uintptr_t kLUI_SetTableString =
    0x0092cec0; // void (const char* key, const char* value, L): a string field of the event table
constexpr uintptr_t kLUI_BeginEvent =
    0x00930360; // char (root name, const char* name, L): starts an event, nonzero when it was created
constexpr uintptr_t kLuiRootAddMenu = 0x00930c60; // void (root name, menu name, controller, L): adds a menu to a root
constexpr uintptr_t kLUI_DrawRoot = 0x00931380;   // void (int lc, rootStruct): draws one root
constexpr uintptr_t kLUI_GetRootNameForController =
    0x009317a0;                                         // int (int controller): the root of the menus of a controller
constexpr uintptr_t kLUI_ResizeRootStruct = 0x00931800; // void (rootStruct, x, y, w, h)
constexpr uintptr_t kLUI_SetRootSize =
    0x00931990; // void (int controller): lays the root out to the viewport of the cgs fractions
constexpr uintptr_t kVidWidth = 0x010f0d14;       // screen width
constexpr uintptr_t kVidHeight = 0x010f0d18;      // screen height
constexpr uintptr_t kVidAspect = 0x010f0d34;      // aspect ratio (float)
constexpr uintptr_t kScrPlaceView0 = 0x011e2950;  // screen placement of view 0
constexpr uintptr_t kScrPlaceText = 0x011e29d0;   // screen placement passed to the text draw
constexpr uintptr_t kActiveMenu = 0x02878080;     // active menu id of local client 0
constexpr uintptr_t kDebugFont = 0x02879010;      // font pointer of the debug text draw
constexpr uintptr_t kVisBits = 0x028a5f28;        // HUD visibility bits
constexpr uintptr_t kStringTable = 0x02b42ea4;    // pointer to the string table: 0x18 bytes per entry, the name at +4
constexpr uintptr_t kLuiState = 0x031708a0;       // lua_State*
constexpr uintptr_t kLuiElements = 0x031708d0;    // LUI elements, 0x148 bytes each
constexpr uintptr_t kLuiDrawPrimary = 0x033256e4; // int: set to -2 around the engine's draw of all roots
constexpr uintptr_t kLuiRootFull = 0x03325750;    // root struct drawn after the menu root (flag byte at +0xac)
constexpr uintptr_t kLuiRoot0 = 0x03325800;       // LUI root UIRoot0
constexpr uintptr_t kLuiRoot0Name = 0x0332588c;   // name of UIRoot0
constexpr uintptr_t kLuiRoot0Active = 0x033258ac; // byte: UIRoot0 is active
constexpr uintptr_t kScoreCache = 0x03329d58;     // HUD score cache (0x64 bytes)
constexpr uintptr_t kTlsIndex = 0x03337df4;       // TLS index

// Typed calls: one function per engine function above, with its signature and calling convention.
// Forced inline, so a call compiles to the same code as a cast to a function pointer.
__forceinline int CG_DrawTextAligned(int lc, uintptr_t scrPlace, float x, float y, float xScale, float yScale,
                                     const char* text, const float* colour, unsigned style, void* font) {
    return ((int(__cdecl*)(int, uintptr_t, float, float, float, float, const char*, const float*, unsigned,
                           void*))kCG_DrawTextAligned)(lc, scrPlace, x, y, xScale, yScale, text, colour, style, font);
}
__forceinline void ScrPlace_SetupViewport(void* place, int x, int y, int w, int h, float aspect, char flag) {
    ((void(__cdecl*)(void*, int, int, int, int, float, char))kScrPlace_SetupViewport)(place, x, y, w, h, aspect, flag);
}
__forceinline int UI_SetActiveMenu(int lc, int menuId) {
    return ((int(__cdecl*)(int, int))kUI_SetActiveMenu)(lc, menuId);
}
__forceinline void LUI_EndEvent(void* L) {
    ((void(__cdecl*)(void*))kLUI_EndEvent)(L);
}
__forceinline void LUI_SetTableBool(const char* key, int value, void* L) {
    ((void(__cdecl*)(const char*, int, void*))kLUI_SetTableBool)(key, value, L);
}
__forceinline void LUI_SetTableNumber(const char* key, int value, void* L) {
    ((void(__cdecl*)(const char*, int, void*))kLUI_SetTableNumber)(key, value, L);
}
__forceinline void LUI_SetTableString(const char* key, const char* value, void* L) {
    ((void(__cdecl*)(const char*, const char*, void*))kLUI_SetTableString)(key, value, L);
}
__forceinline char LUI_BeginEvent(const char* root, const char* name, void* L) {
    return ((char(__cdecl*)(const char*, const char*, void*))kLUI_BeginEvent)(root, name, L);
}
__forceinline void LuiRootAddMenu(const char* root, const char* menu, int controller, void* L) {
    ((void(__cdecl*)(const char*, const char*, int, void*))kLuiRootAddMenu)(root, menu, controller, L);
}
__forceinline void LUI_DrawRoot(int lc, void* rootStruct) {
    ((void(__cdecl*)(int, void*))kLUI_DrawRoot)(lc, rootStruct);
}
__forceinline int LUI_GetRootNameForController(int controller) {
    return ((int(__cdecl*)(int))kLUI_GetRootNameForController)(controller);
}
__forceinline void LUI_ResizeRootStruct(uint8_t* rootStruct, float x, float y, float w, float h) {
    ((void(__cdecl*)(uint8_t*, float, float, float, float))kLUI_ResizeRootStruct)(rootStruct, x, y, w, h);
}
__forceinline void LUI_SetRootSize(int controller) {
    ((void(__cdecl*)(int))kLUI_SetRootSize)(controller);
}
} // namespace engine
