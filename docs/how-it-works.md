---
title: How it works
kind: architecture
description: The design of the split-screen mod, from the proxy DLL and its per-frame driver through the shadow clients, the view passes, the per-player cgame block, HUD roots, input devices and sound listeners to the render-thread side, with a table of the source files.
status: active
updated: 2026-11-14
---

# How it works

The mod is one DLL that the game loads in place of its XInput library. The game has exactly one local player; the mod makes it run up to four, in the same process, and draws each of them into a part of the screen. It does this by reusing the engine's own code paths with the engine's global state switched from one player to the next, not by running four games. Addresses below belong to the one supported Steam build of `t6zm.exe` (image base `0x00400000`).

## The proxy DLL and the per-frame driver

The DLL is named `XINPUT1_3.dll` and sits next to `t6zm.exe`, so the game loads it instead of the system library. It exports the XInput functions, loads the real library from the system folder and forwards the calls (`src/core/xinput_exports.cpp`). It starts no thread of its own. The engine polls the pad on its main thread every frame (`UpdateGamepadStatePad0`, `0x0077c820`), and a poll of pad 0 is the hook that runs the mod's per-frame function, `OnFrame` in `src/core/main.cpp`. The first such call also installs the patches and hooks: at that point the executable has decrypted itself in memory.

Before it changes anything, the DLL checks the PE timestamp of the executable against the one build it knows, and asks the Steam client whether it is online. It acts only for that build and only while Steam is offline.

Two things get the game from its main menu to a local match. A few byte patches and a replacement for the "Online" button make the engine believe in a signed-in local session, so the game's own local lobby opens (`src/menu/steam_offline.cpp`). The DLL then opens that lobby by itself after the first frames. The lobby, the map choice and the match start are the game's own menus; the mod only sends them the same button events the engine sends. Players join in the lobby by a press of A on a pad, which `src/menu/lobby.cpp` reads itself, and writes the joined players into the lobby's party list, so the game's own rows show and count them.

## Shadow clients

A shadow client is an extra local client of the game, created and driven by the mod. Player 1 is the engine's normal local client. Players 2 to 4 each get their own copies of the client structures: the client state (`cl`), the connection state (`clc`), the client UI block and the controller slot, allocated by the DLL.

After the engine has run its client frame for player 1 (`CL_Frame`, `0x00551cd0`), the mod runs the engine's own client-0 network code again for each shadow player, with the global pointers to those structures switched to that player's copies. This is the context switch: save player 1's pointers and blocks, point the globals at the shadow player's copies, call the engine, switch back. The shadow player connects to the game's listen server through its own loopback channel (channel 0 is player 1, channel 1 player 2, channel 2 + i the extra players, channel 4 the server), with its own query port and its own player id. To the server these are separate clients, so each gets a player entity, a spawn, points and a weapon like any other player. The usercmds they send are built by the mod from their input device each frame (`src/shadow/shadow_client.cpp`).

Because the server treats them as ordinary clients, the game's script logic needs no change. What does not carry over by itself is everything that the client side of the engine handles for the one local player: server commands, effects, sound, the HUD. The rest of the design is about those.

## View passes and the split of the screen

The engine draws the frame in one function, `UpdateScreenRenderLocalViews` (`0x0055c100`), which counts the active local clients, sets the number of viewports and draws each view in turn. It was written for the console's split screen, but its loops are bounded to one client on this build. The mod raises those bounds to four with byte patches and makes the engine's "is this local client active" test (`IsLocalClientActive`) answer yes for as many views as there are players. The engine then runs its normal draw routine once per view. Every pass runs as local client 0; what differs between the passes is what the mod has switched in.

The screen is divided by `ShadowViewFractions` (`src/view/view.cpp`): two players get two halves (left and right, or top and bottom), three players get one wide view across the top and two quarters below it (or three quarters with the fourth black, a setting), four players get four quarters. A player who is alone gets the whole screen. Each pass runs the engine's `CG_SetupViewport` for its rectangle, so the engine's own placement of HUD and crosshair works inside the part. The field of view is scaled per view, because a half of the screen is narrower than the field of view the engine would set for the whole.

## The cgame block per player

The engine's client game module (cgame) keeps most of its per-player state in one block of about 0x87700 bytes (`cg`, pointer `0x0105f75c`): the first-person weapon model and its animation, view bob, the predicted player entity, event counters, HUD state. Two players sharing one block would share their weapon, so the mod gives each shadow player a block of its own (`src/shadow/cgame_swap.cpp`) and swaps it in around that player's pass, together with the view model array, the view model's skeleton handle and a set of smaller static blocks. The bookkeeping of the network snapshot stays with player 1's block, so a pass never advances a snapshot twice and entity events do not run twice.

A shadow player's player state (position, weapon, health) is swapped into the cgame for its pass, and its camera comes from the engine's own view calculation with the facing taken from that player's input. Things the engine only does for the one local player are repeated or redirected for the shadow players: prediction and the cgame's input state (`src/view/predict.cpp`), the server commands that switch weapons (`src/shadow/server_commands.cpp`), the player state swap, eye height and weapon sway between snapshots (`src/view/playerstate.cpp`), the camera, recoil and camera kick (`src/view/scene.cpp`), and the player entity of player 1 that every other view must draw in third person (`src/view/entities.cpp`). Effects are culled against every player's camera (`src/effects/fx.cpp`), zombies and their animation are tested per pass, and, on the TranZit map, the area fastfiles ("gumps") are spread over the engine's gump slots by name, so players in different areas do not make the engine load and unload them in turn (`src/effects/gump.cpp`).

## HUD roots

The Zombies HUD is written in Lua and drawn by the engine's UI layer (LUI). The engine creates one root for the local controller, `UIRoot0`, and sends it events (`hud_update_ammo`, `hud_update_scores`, and so on) when the snapshot's player state changes. The mod creates one more root per shadow player (`UIRoot1` to `UIRoot3`, controllers 1 to 3) with the HUD menu in it, ticks and draws it in that player's pass, and routes the events of that pass to it by swapping the root name at the function that starts an event. The HUD visibility bits and the score cache are kept per pass, so each root gets its own full set of values (`src/hud/hud.cpp`). While a menu such as the pause menu is up, player 1's root is drawn once in the middle of the whole screen, because the menu is wider than one part.

## Menus of the shadow players

The HUD menu of every root opens another Lua menu as a popup on the event `open_ingame_menu` and its scoreboard on `open_scoreboard_menu`. The mod sends these events to the root of one shadow player, so the game's own controls options and scoreboard appear in that player's view only, and it passes the player's pad to the menu as `gamepad_button` events (`src/menu/player_menu.cpp`). The options keep their values in the player profile, of which the PC build has one. A shadow player's menu is therefore opened for controller 0, and while one of its events is handled or its root ticks, the profile block holds a copy that belongs to that player; what the engine would do for player 1 in that moment (lock its input, hide its HUD, blur its view, change its pad layout, save the hardware settings) is held back. The values the player chose are read from that copy and used by the mod's own pad code. A root is laid out for the height of its view, and a menu needs the width of a 16:9 screen of that height, so in a narrower view the root is drawn scaled around the middle of the view.

## Input devices

Every player has an input device: keyboard and mouse, one of the four XInput pads, or nothing (`src/input/input.cpp`). A shadow player's usercmd is built from the pad state, or from the key state and mouse movement for keyboard and mouse. The layout of the pad buttons and the fixed keyboard layout are in `src/input/controls.cpp`; stance, the dive to prone and the action slots are small state machines kept per player there.

Player 1 is the engine's own client, so its device is switched on the engine's side. The engine reads pad 0 only, so the proxy answers that call with the state of the pad assigned to player 1, or with "not connected" when player 1 has no pad. When a shadow player owns the keyboard and mouse, three hooks keep both away from player 1: the key event function, the mouse event function and the input-source setter. Escape and the console keys still reach the engine.

## Sound listeners

The engine's sound system has four listeners and works every 3D sound out against the nearest active one, but the PC build's cgame sets only listener 0, once per frame. Since every pass runs as local client 0, each pass moved that one listener to its own player. The mod hooks the listener update so that a shadow pass sets the listener of its own index with its player's number, and switches a listener off again when its pass has not run for a second (`src/effects/sound.cpp`). The shots of players 2 to 4 use the weapon's first-person fire sound, as player 1's do.

## The render-thread side

The engine's renderer runs on a second thread. The main thread builds a command buffer per view and the render thread executes it later, so the view a command belongs to is not known by the frame's current state when it runs. The mod therefore attaches the view rectangle to the buffer: when a buffer is issued (`R_IssueRenderCommands`, `0x0041c800`) the rectangle of its pass is recorded, and the render thread reads it back from the buffer it executes (`src/view/render_backend.cpp`). Hooks on the Direct3D 11 context (the viewport call, the scissor call, the clear call) and on `R_SetRenderTargetViewport` (`0x0046ad90`) then apply that rectangle in place of the full-screen one the engine sets after each render target change. The scissor is cut down to the view's part while its pass is drawn, and the unused quarter of the three-quarter layout is cleared after the last pass. The first pass's 2D drawing runs in its own buffer, so that each part gets its own HUD, hint texts and name tags, and the frame's own full-screen 2D (menus, pause screen) covers the whole screen again afterwards.

## Source files

One module per concern, one folder per group of modules under `src/`: `core`, `engine`, `menu`, `input`, `shadow`, `view`, `hud` and `effects`. Includes are written from `src/` (`#include "core/common.h"`).

| File | Holds |
|---|---|
| `core/common.h`, `core/common.cpp` | includes, `Log`, ini access (`ReadIniInt`, cached and re-read when the file changes), memory helpers (`ReadSafe`, `BytesMatch`, `WriteCode`), and the `BytePatch`, `Detour` and `InstallDetour` structs and functions |
| `core/xinput_exports.cpp`, `core/xinput.def` | loader for the real XInput library and the exported XInput functions; a call for pad 0 runs `OnFrame` |
| `core/main.cpp` | `DllMain`, exception logging, the table of supported game builds, `OnFrame`, window and message hook drivers, the frame rate line |
| `menu/steam_offline.cpp` | the Steam offline mode: sign-in byte patches, the replacement for the "Online" button, the local lobby, loading of the game's script update, the automatic menu and start |
| `menu/lobby.cpp` | players join in the lobby: reads the pads, writes joined players into the party list, draws the hint line |
| `menu/player_menu.cpp` | the game's controller options and the scoreboard of a shadow player in its own view: open and close, the pad as menu input, the profile copy per player, the values the pad code uses, the engine calls held back meanwhile |
| `engine/common.h`, `engine/server.h`, `engine/client.h`, `engine/ui.h`, `engine/render.h` | the addresses of the engine's globals and functions for the one supported game build, one header per engine area, each constant with a one-line comment, and a typed inline call for each engine function the mod calls |
| `engine/offsets.h` | the offsets and sizes inside the engine's structures that the code reads: the client block, the playerState, the cgame block and the view data |
| `shadow/shadow.h` | the structure sizes of the player modules and the declarations they share |
| `shadow/shadow_client.cpp` | the shadow clients (players 2 to 4): context switch, allocation, loopback connection, usercmd from the input device, the client frame driver and its hooks |
| `input/input.cpp` | input device per player; the hooks that keep keyboard and mouse away from player 1 while a shadow player owns them; the hook that keeps another player's pad away from player 1 |
| `input/controls.cpp` | pad layout of the shadow players (actions per button, the button layouts of the controller options, stance, dive, action slots; state per player), their fixed keyboard layout, pad button names in their hint texts |
| `view/view.cpp` | the extra views: view rectangles, viewport setup, the draw passes and their per-pass state, the view hook table, the byte patches of the view loop |
| `view/playerstate.cpp` | the shadow player's player state swapped into the cgame for its pass, its interpolation between snapshots, the re-apply after the engine's prediction |
| `view/predict.cpp` | the cgame's prediction and input hooks while a shadow pass runs, weapon cycling of the shadow players |
| `view/entities.cpp` | the entity add and animation hooks of a shadow pass, player 1's body in the other views, name tags, crosshair scan |
| `view/scene.cpp` | scene submit, the camera of a shadow player, the frame's LOD origin, the actor cull per pass |
| `shadow/server_commands.cpp` | the server commands the shadow players receive: the weapon switch |
| `view/view_private.h` | declarations shared by the view files |
| `shadow/cgame_swap.cpp` | the cgame block per shadow player (state, view model, skeleton handle), swapped in for its pass |
| `hud/hud.cpp` | HUD per player: a LUI root per shadow player, score cache, client fields, visibility bits, the menu drawn in the middle, the scaled draw of a root with a menu or scoreboard, events and Lua table fields for the menus; the shadow players' recoil call |
| `effects/sound.cpp` | one sound listener per player, first-person fire sound for the shadow players |
| `effects/fx.cpp` | effect culling against every player's camera |
| `effects/gump.cpp` | TranZit area fastfiles spread over the engine's gump slots |
| `view/render_backend.cpp` | the render-thread side: view rectangles of the command buffers, Direct3D 11 hooks, the render target viewport hook |
