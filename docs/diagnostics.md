---
title: Diagnostics
kind: reference
description: How to read bo2ss.log for a bug report, and the switches that are not in bo2ss.ini - the fix switches that turn single parts of the mod on and off, the experimental switches and the diagnostic switches that write extra lines to the log.
status: active
updated: 2026-11-23
---

# Diagnostics

This document is for tracking down a problem. The settings a player changes are in [configuration](configuration.md); the keys below are not in the shipped `bo2ss.ini` and the defaults are the tested behaviour. To use one, add a line `key=value` under `[bo2ss]` in `bo2ss.ini` in the game folder. The mod reads most keys every frame, so a change shows while the game runs; a key marked "read at game start" needs a restart of the game. Delete the line to get the default back.

## Reading bo2ss.log

The mod writes `bo2ss.log` into the game folder, next to `bo2ss.ini`. The file is started anew every time the game starts, so copy it before you start the game again. Every line begins with the time in milliseconds since the computer started, in square brackets. Attach the whole file to a bug report, together with your `bo2ss.ini`, and say which map and how many players were in the match.

**The first lines** say whether the mod could work at all:

| Line | What it tells |
|---|---|
| `bo2ss build 2 release loaded into ...\t6zm.exe` | The DLL was loaded by the game. If this line is missing, the DLL is not in the game folder or the game loads another `XINPUT1_3.dll`. |
| `game build: ... (timestamp ...)` | The executable is the build the mod knows. `unknown game image ...; nothing patched` means another build: the mod does nothing. |
| `pad 0: connected` and so on | Which XInput pads Windows reports at start. |
| `steam client: offline mode` | The mod works. `steam client: ONLINE, the mod stays passive (nothing patched, nothing hooked)` means Steam is online: the game runs unchanged, and Steam must be put into offline mode for split-screen. |
| `[bo2ss] script update: 71808 bytes saved to ...` | Steam was online, the game fetched its script update and the mod wrote it to `zone\all\ffotd_tu17_zm_147.ff`. `the file on disk is current` means it was there already. `writing ... FAILED` means the game folder could not be written to. |
| `version v0.2.0` | The version of the DLL. |
| `[update] this is v0.2.0, the newest release is v0.2.0: up to date` | The update check got an answer. `release v... is out` means a newer release exists; `no answer about the newest release` means no connection or an unexpected answer; `check switched off` means `update_check=0`. |
| `[bo2ss] script update missing (...): the lobby stays closed` | Offline mode without the script update. Get it as the README describes under "The script update". |

**The split has started** when these lines are in the log, in this order:

1. `[bo2ss] opening local split-screen lobby (menu 11), ...` and `[bo2ss] UI_SetActiveMenu(0, 11) returned ...`: the lobby was opened.
2. `[lobby] pad N joins as player M ...`: a player joined in the lobby (only with `lobby_join=1`).
3. `[p2] connecting: ...` and `[p2] connstate ...`: the second player connects to the game's own local server.
4. `[view] N view passes per frame (was M)`: the screen is split into N views.
5. `[p2] ps swap: commandTime ...`: the second view runs on the second player's own state. From here the split is up.

A match that never gets past step 3 or 4 is the typical bug report: say which line was the last one.

**Lines that point to a fault:**

- `EXCEPTION <code> at eip=...` followed by a `stack:` line: the game crashed inside the mod or inside the game code the mod called. Quote the whole block.
- `[p2] shadow client stopped: <reason>`: the second player was dropped; the reason says why.
- `[p2] Com_Error code ...`: the game reported an error for the second player.
- `hook '<name>': unexpected bytes at ..., skipped`, `hook ... FAILED`, `patch ... FAILED`, or a line with `prologue mismatch`: a hook or patch was not installed because the game's code did not look as expected. The part it belongs to stays off.

**What `trace=1` adds.** With `trace=0` (the default) the log stays short. `trace=1` also logs the game's console output, the console commands the mod and the game run, the menu events and the calls of the game's Lua engine, and, every few seconds, a line with the state of the menus (`state (frame ...)`), a heartbeat line (`heartbeat: frame ...`) and the pad lines. Switch it on when a bug report needs to show what the menus were doing: it makes the file large, and the console output can be many thousand lines. The diagnostic switches at the end of this document are separate: each one adds only its own lines.

## Fix switches

Each of these turns one part of the mod on or off. All are on (or at their working value) by default; a switch is useful to find out whether a part causes a problem. Most are read every frame, the ones marked "read at game start" only once.

### Start and lobby

| Key | Default | What it does |
|---|---|---|
| `party_maxplayers` | `4` | Sets the console variable `party_maxplayers` when the lobby opens. |
| `lobby_hint` | `1` | 1 shows the line in the lobby that says which button joins; 0 hides it. |
| `lobby_hint_x` | `1068` | Left edge of that line, in pixels of a 1920x1080 screen. |
| `lobby_hint_y` | `136` | Top of that line with one player in the list, in pixels of a 1920x1080 screen. |
| `lobby_hint_step` | `45` | How far the line moves down for each joined player, in pixels of a 1920x1080 screen. |
| `lobby_hint_scale` | `55` | Size of the line in percent. |
| `load_ffotd` | `1` | Loads the game's script update file (`zone\all\ffotd_tu17_zm_147.ff`) from disk before the party starts. 0 skips it, and also the check that keeps the lobby closed while the file is missing. |
| `force_zombies_mode` | `1` | Makes sure the session is in zombies mode when the lobby opens. 0 leaves the engine's value. |
| `offline_signin_fix` | `1` | Patch that skips the game's "Steam is offline" pop-up when the Online button is used. 0 does not apply it. |
| `offline_fake_live` | `1` | Makes the Online button of the main menu call the mod, which opens the local lobby. 0 leaves the button alone, so no lobby opens. |
| `offline_menu` | `11` | Menu the Online button opens: 11 is the split-screen lobby. 20 is the system link lobby, which is not used. |
| `lua_demonware_true` | `1` | Patch that makes the menu scripts see a signed-in online account. 0 does not apply it. |
| `steam_server_no_vac` | `1` | Patch that stops the local server from asking Steam for an anti-cheat policy. 0 does not apply it. |

### Players and input

| Key | Default | What it does |
|---|---|---|
| `p2_enable` | `1` | 0 turns the extra players off: the game runs as one player. |
| `p2_connect_after_ms` | `300` | Waits this long after player 1 is in the game before the second player connects. |
| `p2_exec` | `r_dof_enable 0` | Console commands run once when the second player starts, separated by `;`. The default turns off the depth-of-field effect, which showed the other half's picture while aiming. |
| `p2_input` | `1` | 0 makes players 2 to 4 send empty input, and player 1 keeps every device. |
| `p1_pad_only` | `1` | While player 1 plays on a connected pad, keyboard and mouse do nothing in the match and still work in the menus. 0 lets player 1 use both in the match. |
| `p_input_hooks` | `1` | Hooks that keep keyboard and mouse away from player 1 while another player uses them. Read at game start. |
| `p_extra_input` | `1` | 0 makes players 3 and 4 send empty input, so they stand still. |
| `p2_expected` | `1` | Makes the game's scripts expect at least two players, so player 2 spawns in round 1. |
| `p2_drain_loopback` | `1` | Drops stale network packets of the extra players' loopback connections before they connect and after they stop. |

### Pad layout and stance

| Key | Default | What it does |
|---|---|---|
| `p2_jump_step_ms` | `8` | A jump of player 2 starts only in a command whose time step is at least this many milliseconds. 0 turns the rule off. |
| `p2_actionslot` | `1` | Action slots of players 2 to 4. 1 runs the game's own handler, 2 sends only the slot's button bit, 0 turns them off. |
| `p2_cg_buttons` | `1` | Button presses the game's client code adds for player 2 (action slots) go into player 2's input instead of player 1's. |
| `p2_start_menu` | `1` | Start on the pad of a player 2 to 4 does something: the player's own controller options, or player 1's pause menu with `player_menu=0`. |
| `p2_hint_pad` | `1` | Hint texts in the half of player 2 name the pad buttons of the layout. 0 shows keyboard names. |
| `p2_dive` | `1` | Dive to prone: hold the stance button while sprinting. |
| `p2_sprint_stand` | `1` | A sprint press out of crouch or prone keeps the player standing when the sprint ends. |
| `p2_stance_events` | `1` | A stance the game sets for a player 2 to 4 (stand up, respawn) goes to that player and not to player 1. |

### Views and camera

| Key | Default | What it does |
|---|---|---|
| `p2_view_hook` | `1` | Installs the second-viewport hook. Read at game start. 0 leaves the screen unsplit. |
| `p2_view` | `1` | 0 stops drawing the extra views while the game runs. |
| `p2_ps` | `1` | The extra view runs on the second player's own state (weapon, HUD, camera). |
| `p2_cam` | `1` | Camera of the second player is set again when the scene is submitted, as a fallback for the engine's own camera. |
| `p2_cam_native` | `1` | Camera of the extra views comes from the engine's own view calculation. |
| `p2_cam_native_axis` | `1` | Forces the view axes of the extra views from the pad's facing when the scene is rendered. |
| `p2_cam_delta_angles` | `1` | Adds the view angle offset the server keeps for the player (delta angles) to the pad's facing when the camera is set. |
| `p2_cam_kick` | `1` | Players 2 to 4 keep the camera motion the game adds on top of the facing (weapon kick, reload, melee, damage). |
| `p2_viewheight` | `60` | Eye height above the second player's origin for the camera the mod sets itself (`p2_cam`) and for the level-of-detail origin. |
| `p2_scrplace` | `1` | Each view runs the engine's viewport set-up for its half, so the HUD and crosshair sit inside the half. |
| `p2_scrplace_uniform` | `0` | Left / right split only: the same HUD scale in x and y, so the HUD is not stretched vertically. |
| `p2_ps_after_ms` | `0` | Delays the second player's state swap by this many milliseconds after player 2 joined; until then its view shows player 1's state. |
| `p2_ps_keep_vision` | `1` | Keeps player 1's vision-set values in the swapped state. |
| `p2_ps_keep_clientnum` | `1` | Keeps player 1's client number in the swapped state. |
| `p2_ps_reswap` | `1` | Puts the second player's origin back into the swapped state when the engine's view origin calculation finds it changed. |
| `p2_cg_swap` | `1` | The extra view runs on a copy of the game's client block of its own (viewmodel, animations, bob, events). |
| `p2_cg_swap_vm` | `1` | The copy includes the viewmodel array. |
| `p2_cg_swap_dobj` | `1` | The copy gives the second player a viewmodel object of its own. |
| `p2_no_replay` | `1` | The extra view does not replay player 1's input on player 2's state. |
| `p2_interp` | `1` | The origin of the second player is interpolated between its last two snapshots. |
| `p2_interp_ads` | `2` | Interpolates the aim-down-sights fraction too. 2 also starts the zoom locally at once for each of the players 2 to 4, 1 interpolates only. |
| `p2_interp_viewheight` | `1` | The eye height of players 2 to 4 is interpolated, so a stance change moves the camera smoothly. |
| `p2_interp_bob` | `1` | The weapon bob and the velocity of players 2 to 4 are interpolated. |
| `p2_pred_ground` | `1` | The prediction of player 2 sees no ground entity, so standing on a moving entity (the bus) does not copy player 1's state over it. |
| `p2_cg_clientnum` | `1` | The extra view uses the second player's client number, so the game hides that player's body in the own view. |
| `p2_hide_body` | `1` | The second player's own entity is flagged hidden while the extra view adds the entities. |
| `p2_ent_frametime0` | `1` | Entity animations advance once per frame, not once per view. |
| `pad_turn_once` | `1` | Player 1's gamepad turns the view once per frame, not once per view, so the turn speed does not grow with the number of players. |
| `p2_ent_clientnum` | `1` | The second player's client number is set only while the entities are added. |
| `p2_ent_anim_once` | `1` | The extra views skip the per-frame animation advance of the entities, so zombies and other players do not animate at double speed. |
| `p2_p1_anim_once` | `1` | Player 1's model advances its animation in player 1's view only. |
| `p2_show_p1` | `1` | Player 1's body is drawn in the view of player 2. |
| `p2_show_p1_anim` | `1` | Advances the animation of that body once per frame. |
| `p2_show_p1_notetracks` | `0` | Also runs the animation events (footstep sounds) of that body. The sounds belong to player 1's own view, so the default is 0. |
| `p2_show_p1_ci` | `1` | Keeps the client info slot of player 1 in the block of player 2 instead of copying it every pass. |
| `p2_show_p1_snapnum` | `1` | Uses the second player's client number while that body is added. |
| `p2_link_p1` | `1` | An entity linked to player 1 (the burning player in TranZit) is added in the other views too. |
| `p2_p1_pose` | `1` | Player 1's slot in the entity array keeps player 1's position, so things attached to player 1 stay with player 1. |
| `p_views` | `0` | Number of view passes per frame. 0 is automatic (2, plus one per extra player in the game). 2 to 4 fix the count. |
| `p2_force_viewport` | `1` | Sets the viewport of the scene surfaces of the extra views to the view's own rectangle. |

### HUD

| Key | Default | What it does |
|---|---|---|
| `p2_hud_own` | `1` | The second player gets a HUD of its own. 0 shows player 1's HUD in both halves. |
| `p2_hud_controller` | `1` | Controller index of the second player's HUD root. |
| `p2_hud_slot` | `6` | View parameter slot of the second player's HUD root. |
| `p2_hud_tick` | `1` | Ticks the second player's HUD root in the extra view. |
| `p2_hud_draw` | `2` | How the second player's HUD is drawn: 0 not at all, 1 direct root draw, 2 through the engine's draw path. |
| `p2_hud_route` | `1` | HUD events of the extra view that the engine addresses to the first HUD root go to the second player's root. 0 drops them. |
| `p2_hud_update` | `1` | Feeds the second player's HUD from its own state. |
| `p2_hud_first_snapshot` | `1` | Sends the "first snapshot" event to the second player's HUD root, which hides its intro card. |
| `p2_hud_draw_p0` | `1` | What the first view draws with the own HUD: 1 the engine's HUD, 0 nothing, 2 the second player's root. |
| `p_hud_extra` | `1` | Players 3 and 4 get a HUD root of their own. 0 shows no HUD in their quarters. |
| `p_hud_bits` | `1` | The HUD visibility bits reach every player's HUD root. |
| `p_fx_weapon_rect` | `1` | The effects of a first-person weapon (muzzle flash) are drawn into the rect of their view; with `0` they sit where the muzzle would be on an undivided screen. |
| `p_fx_weapon_own` | `1` | Those effects are drawn in a view only shortly after its own player's shot; with `0` a muzzle flash shows at the muzzle of every player. |
| `p_fx_weapon_ms` | `200` | How long after a player's shot its view draws the effects of the first-person weapon, in ms. |
| `p_view_flash` | `1` | A shot of players 2 to 4 plays the muzzle flash of the first-person weapon in their own view. |
| `p_view_flash_log` | `0` | N lines (`[flash]`): each flash a weapon starts and each fire sound, with the weapon, the view it runs in and the player it is counted for. |
| `p_mover_log` | `0` | N lines (`[mover]`): each time the server carries a player 2 to 4 that stands on a mover such as the TranZit bus, with the command's times, the time it is carried from and the time of the mover's newest pose sample. |
| `p_brass_world` | `1` | While more than one view is drawn, player 1's shell casings are the ones of the world model; with `0` the casings of the first-person weapon fly next to every player's weapon. |
| `p_hud_round` | `1` | The round counter of players 2 to 4 follows the round; with `0` it stays one round behind. |
| `p_hud_dpad_show` | `1` | The ammo counter and the d-pad block of players 2 to 4 fade in again when their ammo or weapon changes. |
| `p2_hud_score` | `1` | Score lines in the half of player 2 follow both players' points. |
| `p2_hud_cf` | `1` | The second player's own HUD fields (collected parts, power-up icons) reach its HUD. |
| `p2_menu_center` | `1` | While a menu is open (pause menu), it is drawn once in the middle of the whole screen. |
| `p_scoreboard_per_view` | `1` | The scoreboard stays inside each player's view. 0 centres player 1's on the screen, where it overlaps the others at the end of a match. |
| `player_menu_aspect` | `178` | Width over height, in percent, that a view needs to show a menu at full size. In a narrower view the own menu of a player 2 to 4 is drawn smaller by view aspect over this value. |
| `player_scores_aspect` | `97` | The same for the scoreboard of a player, and of everyone at the end of a match. |
| `p2_tag` | `1` | Name tags in the extra views show player 1's tag over player 1's body. |
| `p2_lui_root` | `1` | Resizes the HUD layout once to the first half when the split starts. |
| `p2_lui_resize` | `1` | Lays the HUD roots out again when the size of a half changes during the match. |

### Weapons and viewmodel

| Key | Default | What it does |
|---|---|---|
| `p2_own_weapon` | `2` | Weapon sent in the usercmd of player 2: 1 the weapon its state holds, 2 the weapon its own client block selected. |
| `p2_weapnext` | `1` | Next weapon on the pad runs the engine's weapon switch on the second player's block. Needs `p2_own_weapon=2`. |
| `p2_skip_cmd_weapons` | `1` | The extra view does not copy the second player's weapon fields into player 1's client. |
| `p2_keep_cl_input` | `1` | The extra view does not leave its weapon selection and button bits in player 1's next command. |
| `p_weapon_adopt` | `1` | A weapon in hand without a selection becomes the selection. |
| `p_srvcmd_weapon` | `1` | Players 2 to 4 carry out the server's weapon switch (a perk bottle, a build). |
| `p_srvcmd_hold_ms` | `1500` | After such a switch the selection stays the server's weapon for this many milliseconds. 0 turns the hold off. |
| `p2_vm_own_hands` | `1` | The viewmodel of the second player uses its own character's hands. |
| `p2_recoil` | `1` | Weapon recoil for players 2 to 4. Needs `p2_cam_kick=1` to show. |
| `p2_recoil_fpu` | `1` | The recoil call runs with an empty floating-point stack. |
| `p2_recoil_dw_hip` | `1` | With two weapons the recoil of players 2 to 4 always uses the hip values. |
| `p_crosshair_self` | `1` | The crosshair trace of players 2 to 4 skips their own body. |
| `p_snap_ps` | `1` | The low-health overlay and the other HUD menus of players 2 to 4 follow their own health. |
| `p_block_handles` | `1` | The cgame block of a shadow player starts without player 1's physics handles (the game would crash when a player dies). |
| `p_body_handles` | `1` | The copy of player 1's body in the other views keeps physics handles of its own. |
| `p_damage_feedback` | `1` | A hit on player 2, 3 or 4 shows in that player's view: view kick, damage direction indicator, red flash. |
| `p_spectate_view` | `1` | A dead player 2, 3 or 4 that watches another player looks where that player looks. 0 lets its own pad turn the camera. |

### Rendering

| Key | Default | What it does |
|---|---|---|
| `p2_vp_hook` | `1` | Rewrites every full-screen Direct3D viewport during the extra views to the current view rectangle. |
| `p2_be_rect` | `1` | The render thread takes the view rectangle from the viewport command in the command stream. |
| `p2_rt_rect` | `3` | When the view rectangle is applied: 3 after every render-target switch. |
| `p2_issue_2d` | `1` | The first view's 2D (HUD, hint texts) runs in its own buffer. Needs `p2_rt_rect=3` and `p2_hud_draw=2`. |
| `p2_skip_backdrop` | `1` | Skips the render thread's single-view backdrop in the first view's buffer. |
| `p2_full_2d` | `1` | The half scissor ends after the second view, so menus and the pause screen cover the whole screen. |
| `p2_scissor_clip` | `1` | Cuts every scissor the engine sets itself down to the current half. |
| `p2_light_patch_early` | `1` | The render thread writes the entity lighting patches before it draws a buffer, so moving entities such as the bus keep their brightness in both halves. |
| `p2_keep_light_handle` | `1` | Entity jobs keep an entity's light cache handle when the current view does not see it. Read at game start. |
| `p2_gump` | `2` | TranZit: number of area file slots used as a cache. 0 is the engine's behaviour. |
| `p_view_stale` | `2` | View slots below a view's own slot get an empty viewport, so the HUD is not drawn twice. `1` gives them the view's own viewport instead; a translucent full-screen element (a fade, a flash) is then drawn several times on the views of players 2 to 4. |
| `p2_actor_cull` | `2` | Zombie culling: 0 engine, 1 no culling, 2 each view tests against its own camera. |
| `p2_lod_origin` | `1` | The second view sets the level-of-detail origin to its own camera. |

### Sound and effects

| Key | Default | What it does |
|---|---|---|
| `p_snd_listeners` | `1` | One sound listener per player. 0 is the engine's single listener. |
| `p_snd_own_fire` | `1` | The shots of players 2 to 4 use the weapon's first-person fire sound. |
| `p2_skip_duck` | `1` | The extra views leave the sound ducking alone. |
| `p2_fx_cull` | `1` | Effects are culled against the cameras of both players. |
| `p2_fx_cells` | `1` | An effect near player 2 stays awake while player 1 is in another part of the map. |

## Experimental switches

These change what the game shows or does, are off by default and are not part of the normal play. Leave them alone unless you are looking for a particular problem.

| Key | Default | What it does |
|---|---|---|
| `p2_hint_gamepad_state` | `0` | The extra views report a gamepad as the input source, which changes the button prompts. |
| `p2_no_lmonly_ents` | `0` | Controls the engine variable `r_lightmapOnlyEntSurfs` while the split runs. With 0 the variable is forced off. |
| `p2_fx_nocull` | `0` | Effects are not culled by the camera frustum at all while the split runs. |

## Diagnostic switches

These write extra lines to `bo2ss.log` or change how the mod reads the file. All are 0 by default (`ini_cache` is 1). A number means "log this many lines or reports", then stop; change the value while the game runs to log again. The switches `perf_log` and `ini_cache` change nothing a player sees.

| Key | Default | What it does |
|---|---|---|
| `perf_log` | `0` | Every 5 seconds a `[perf]` line: frame rate, ini lookups, pad polls, and the time per view. |
| `ini_cache` | `1` | 0 reads the file on every lookup instead of keeping the values in memory. Slower; for comparing frame rates. |
| `list_scripts` | `0` | Lists the loaded script assets in the lobby, and the ones that are missing. |
| `p_input_log` | `0` | Once a second, why keyboard and mouse do or do not reach the player that owns them (`[input]`). |
| `p2_snap_diff` | `0` | N reports of the entities in player 1's and player 2's snapshot of the same server time (`[snap]`). |
| `p2_ps_diff` | `0` | Once per match, the values that differ between player 1's and player 2's state. |
| `p2_scene_log` | `0` | N lines (`[scene]`) when the entity count of the second view jumps or its camera is far from player 2. |
| `p_cam_far_log` | `0` | N lines (`[scene] ... CAMERA FAR`), each for a view of players 2 to 4 whose camera is more than 100 units from its player. |
| `p2_light_log` | `0` | N lines (`[light]`): lighting patches per drawn buffer. |
| `p2_vis_log` | `0` | N lines (`[vis]`): entities whose visibility changed from the previous frame of the same view. |
| `p2_interp_log` | `0` | N lines of the snapshot interpolation of players 2 to 4 (`[p2] interp`, `[p3] interp`, `[p4] interp`). |
| `p2_vm_log` | `0` | N viewmodel rebuilds with their inputs. |
| `p2_vm_trace` | `0` | N lines (`[vm]`) about the viewmodel per view. |
| `p2_body_trace` | `0` | Traces the drawing of player 1's body in the second view. |
| `p2_link_log` | `0` | N lines (`[link]`) for entities linked to a player, per extra view. |
| `p2_dobj_log` | `0` | N lines (`[dobj]`), one per model of a player added to a view. |
| `p2_hud_draw_trace` | `0` | N draws of a HUD root (view, root, slot). |
| `p2_hint_log` | `0` | N frames of the cursor-hint state (doors, wall buys) per view. |
| `p2_hud_trace` | `0` | N HUD event names sent from the second view. |
| `p2_hud_trace_p1` | `0` | N HUD events of player 1's view and of the time outside the views. |
| `p_hud_dump` | `0` | A change of the value logs the HUD element trees (`[luitree]`). A value above 1 is the line limit per root. |
| `p_hud_bits_log` | `0` | N lines for the changes of the HUD visibility bits of a view. |
| `p_rumble_log` | `0` | N lines (`[rumble]`) with the vibration strengths a view of a player 2 to 4 asked for. |
| `p_srvcmd_log` | `0` | N lines (`[srvcmd]`) with the server commands players 2 to 4 receive. |
| `p2_cam_kick_log` | `0` | N lines, one a second per view, with the largest camera offset in degrees. |
| `p2_recoil_log` | `0` | N lines for the view angle steps (recoil) of players 2 to 4. |
| `p2_anim_log` | `0` | N reports (`[anim]`), one a second, of the animation advances per view. |
| `p2_cf_log` | `0` | N lines for the HUD field events (power-up icons, collected parts). |
| `p_health_log` | `0` | N lines with what the low-health overlay of a shadow view reads. |
| `p2_ent_log` | `0` | N reports, one a second, of the players' and zombies' models per view: origin, visibility, cull state. |
| `p2_fx_log` | `0` | N reports, one a second, of the effect system's three cameras per view. |
| `p2_frame_log` | `0` | The first N frames with every view (rectangles, field of view, view count). |
| `p2_vp_log` | `0` | N lines about the viewport rewrites. |
| `p2_be_log` | `0` | N render-thread calls. Change the value while the game runs to start again. |
| `p2_controls_log` | `0` | N changes of the pad actions of player 2 (`[ctl] actions`). |
| `p2_hint_trace` | `0` | N key-name lookups of the second view (`[ctl] hint`). |
| `p2_jump_log` | `0` | N commands of player 2 that newly carry the jump bit, with their time step. |
