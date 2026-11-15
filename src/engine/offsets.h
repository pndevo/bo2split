// Offsets and sizes inside the engine's data structures that the code reads and writes: the
// client block (cl), the playerState it holds, the cgame block (cg), the clientinfo array of the
// cgame block and the frontend view data.
// Offsets are in bytes from the start of the structure, except the view rect indices, which
// count ints. They are those of the one supported game build.
#pragma once

#include <cstddef>
#include <cstdint>

namespace engine {

// --- Client block (cl, one per local client) ---
constexpr int kClSnapshot = 0x50;    // the current snapshot, stored inside the client block; its first dword is
                                     // non-zero once a snapshot has arrived
constexpr int kClPlayerState = 0x70; // the playerState of the current snapshot

// --- playerState ---
constexpr size_t kPlayerStateSize = 0x2a08;
constexpr int kPsOtherFlags = 0x18;            // int, bits: 1 = takes no damage, kPofFollow, 4 = a player
constexpr int kPofFollow = 2;                  // the playerState is a copy of the player this client watches
constexpr int kPsOrigin = 0x28;                // vec3
constexpr int kPsClientNum = 0x1a0;            // int
constexpr int kPsWeapon = 0x1b8;               // int, the weapon in hand
constexpr size_t kPsWeaponPosFrac = 0x1e0;     // float, the aim-down-sights fraction (0 = hip, 1 = fully aimed)
constexpr size_t kPsViewHeightCurrent = 0x208; // float; +0x204 is the target height as an int (60 standing)
constexpr int kPsDamageEvent = 0x228;          // int, counts the hits taken; +4 yaw and +8 pitch of the last hit
                                               // (0 to 255 for 0 to 360 degrees, both 255 = no direction), +0xc amount
constexpr int kPsDamageFieldsSize = 0x10;      // the four damage fields from kPsDamageEvent on

// --- cgame block (cg, pointer engine::kCg) ---
constexpr size_t kCgSize = 0x87700;
constexpr int kCgNextSnap = 0x24;             // pointer, the next snapshot
constexpr int kCgSnap = 0x28;                 // pointer, the current snapshot
constexpr int kCgFrametime = 0x48088;         // int, ms
constexpr int kCgTime = 0x4808c;              // int, the cgame's server time
constexpr uintptr_t kCgPredictedPs = 0x480a8; // the predicted playerState (kPlayerStateSize bytes)
constexpr size_t kCgPredictedEnt = 0x4aab4;   // the predicted player entity (kCentSize bytes)
constexpr int kCgWeaponSelect = 0x69674;      // int, the selected weapon
constexpr int kCgClientInfo = 0x69a60;        // array of 18 clientinfo entries
constexpr int kCgOldPsDamageEvent = 0x80b80;  // int, the damage counter of the playerState before the prediction
constexpr int kClientInfoSize = 0x808;        // bytes per clientinfo entry
constexpr size_t kCentSize = 0x380;           // bytes per entity in the cgame entity array

// --- Frontend and backend view data (pointers engine::kFrontendData / engine::kBackendData) ---
constexpr int kViewIndex = 0x474208; // int, the view being built or drawn
constexpr int kViewCount = 0x47420c; // int, the number of views of the frame
constexpr int kViewData = 0x474210;  // pointer, array of kViewDataSize bytes per view
constexpr int kViewDataSize = 0xc50;
constexpr int kViewRectX = 0x54; // index in ints (byte offset 0x150): the view's rect x, y, width, height
constexpr int kViewRectY = 0x55;
constexpr int kViewRectW = 0x56;
constexpr int kViewRectH = 0x57;

} // namespace engine
