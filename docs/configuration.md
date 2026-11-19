---
title: Configuration
kind: reference
description: Every setting of bo2ss.ini with its meaning, values and default, grouped as in the file, the settings window that edits the file while the game runs, and where the switches that are not in the file are documented.
status: active
updated: 2026-11-14
---

# Configuration

The settings are in `bo2ss.ini` in the game folder, next to `XINPUT1_3.dll`, in the section `[bo2ss]`. Each setting is a line `key=value`; a line that starts with a semicolon is a comment. The file you get with the mod has every setting listed below, each with a comment. A setting that is left out or deleted has its default.

The mod reads most settings every frame, so a change in the file or in the settings window shows while the game runs. Exceptions are marked in the tables: the map, start location and game type are read when the lobby opens, and the player count and the layout with three players take effect at the start of the next match.

## Start

| Key | Meaning | Values | Default |
|---|---|---|---|
| `map` | Map of the match. Read when the lobby opens. | `zm_transit` (TranZit) or the name of a DLC map you own and have installed, see below | `zm_transit` |
| `startlocation` | Start location on that map. It has to belong to the map. | see below | `transit` |
| `gametype` | Game type. | `zclassic` (story mode of the map) or `zstandard` (Survival) | `zclassic` |
| `auto_menu_after_ms` | The split-screen lobby opens by itself this many milliseconds after the game has started. | milliseconds; 0 = it does not, click "Online" in the main menu | `1000` |
| `auto_start_after_ms` | The match starts by itself this many milliseconds after the lobby opened. | milliseconds; 0 = the lobby stays open and you press "Start Match" | `0` |
| `lobby_join` | Players join in the lobby. | 1 = A on a free gamepad joins the next player (2 to 4), B on a joined pad leaves; 0 = the player count and pads of the next group are used | `1` |

The map choice in the lobby shows only the base game's map. To play a DLC map that you own and have installed (its file is in the game's `zone\all` folder), name it with the three settings. Combinations that have been played:

| `map` | `startlocation` | `gametype` | What it is |
|---|---|---|---|
| `zm_transit` | `transit` | `zclassic` | TranZit, story mode |
| `zm_transit` | `town` | `zstandard` | TranZit, Survival on Town |
| `zm_transit` | `farm` | `zstandard` | TranZit, Survival on Farm |
| `zm_transit` | `diner` | `zstandard` | TranZit, Survival on Diner |
| `zm_transit` | `busdepot` | `zstandard` | TranZit, Survival on Bus Depot |
| `zm_tomb` | `tomb` | `zclassic` | Origins |

Other maps and start locations are not listed because they have not been checked.

## Players and devices

| Key | Meaning | Values | Default |
|---|---|---|---|
| `p1_pad` | Input device of player 1. Always used. Player 1 on a pad plays with the game's own gamepad bindings; keyboard and mouse then do nothing in the match and still work in the menus. | -3 = automatic (the first connected pad that no other player has, keyboard and mouse without one), -2 = keyboard and mouse, -1 = nothing, 0 to 3 = that XInput pad | `-3` |
| `p2_pad` | Input device of player 2. Only used when players do not join in the lobby (`lobby_join=0`). | -2, -1, 0 to 3 as `p1_pad` | `1` |
| `p3_pad` | Input device of player 3. Only used with `lobby_join=0`. | -2, -1, 0 to 3 as `p1_pad` | `2` |
| `p4_pad` | Input device of player 4. Only used with `lobby_join=0`. | -2, -1, 0 to 3 as `p1_pad` | `3` |
| `extra_players` | Players beyond the second. Only used with `lobby_join=0`. Read when the match starts. | 0 = two players, 1 = three, 2 = four | `0` |

A device belongs to one player: when two players name the same device, the lower player gets it and the other one has no input. Only one player at a time can have the keyboard and mouse. Players 2 to 4 on a pad use the button layout below; on the keyboard and mouse they use a fixed layout, which the README lists.

## Screen

| Key | Meaning | Values | Default |
|---|---|---|---|
| `split_vertical` | Split direction with two players. Three and four players use quarters. | 1 = left and right, 0 = top and bottom | `1` |
| `layout_three_players` | Layout with three players. Choose it before the match starts. | 1 = player 1 across the top, players 2 and 3 in the bottom quarters; 0 = three quarters, the fourth one black | `1` |
| `fov_scale` | Field of view of the split views as a percentage of the game's own. Applies to halves and to the wide view of three players, not to quarters. | 100 = as the game sets it, 50 = the vertical field of view of the full screen; smaller = wider | `70` |

## Look and aim

| Key | Meaning | Values | Default |
|---|---|---|---|
| `mouse_sens` | Mouse speed of a player 2 to 4 on keyboard and mouse. | tenths of the game's sensitivity setting; 50 = 5.0 | `50` |
| `look_native` | Right stick of players 2 to 4 turns at the speeds the game itself uses for a gamepad, as player 1's does: fast from the hip, slower while aiming, slower still with a zoomed weapon, with a short run-up. | 0 or 1; 0 = the fixed speed `look_speed` | `1` |
| `look_accel` | Run-up of the turn on a gamepad, for all players including player 1: the game lets the turn take about a third of a second to reach full speed. | 0 or 1; 0 = full speed at once | `1` |
| `look_speed` | Turn speed of the right stick of players 2 to 4 at full travel, with `look_native=0`. | degrees per second | `180` |
| `look_deadzone` | Part of the stick travel that does nothing. | percent | `20` |
| `look_curve` | How stick travel becomes turn speed. | percent; 100 = linear, 150 = finer around the centre, 200 = finer still | `150` |
| `look_ads_scale` | Turn speed while aiming down the sights, for the mouse, and for the right stick with `look_native=0`. | percent of the normal speed | `65` |

## Buttons

One key per pad input. The value is the number of the action the input does. All pads of players 2 to 4 share the layout. Player 1 on a pad is not affected: it uses the game's own bindings.

| Number | Action |
|---|---|
| 0 | none |
| 1 | fire |
| 2 | aim down sights |
| 3 | lethal grenade |
| 4 | tactical grenade |
| 5 | use / reload |
| 6 | jump (stands up first when crouched or prone) |
| 7 | crouch (tap) / prone (hold) |
| 8 | sprint |
| 9 | melee |
| 10 | next weapon |
| 11 to 14 | action slot 1 to 4 |
| 15 | scoreboard in the player's own view, while held (`player_scores`) |
| 16 | the player's own controller options (`player_menu`) |
| 17 | previous weapon |

| Key | Meaning | Values | Default |
|---|---|---|---|
| `btn_rt` | Right trigger | action number | `1` (fire) |
| `btn_lt` | Left trigger | action number | `2` (aim) |
| `btn_rb` | Right bumper | action number | `3` (lethal grenade) |
| `btn_lb` | Left bumper | action number | `4` (tactical grenade) |
| `btn_x` | X button | action number | `5` (use / reload) |
| `btn_a` | A button | action number | `6` (jump) |
| `btn_b` | B button | action number | `7` (crouch / prone) |
| `btn_ls` | Left stick click | action number | `8` (sprint) |
| `btn_rs` | Right stick click | action number | `9` (melee) |
| `btn_y` | Y button | action number | `10` (next weapon) |
| `btn_dpad_up` | D-pad up | action number | `11` (action slot 1) |
| `btn_dpad_down` | D-pad down | action number | `12` (action slot 2) |
| `btn_dpad_left` | D-pad left | action number | `13` (action slot 3) |
| `btn_dpad_right` | D-pad right | action number | `14` (action slot 4) |
| `btn_back` | Back button | action number | `15` (scoreboard) |
| `btn_start` | Start button | action number | `16` (controller options) |
| `stance_hold_ms` | How long the crouch button is held to go prone, or to stand up from prone. | milliseconds | `300` |
| `player_menu` | Start on the gamepad of a player 2 to 4 opens the game's controller options for that player, inside the player's own view. 0: Start opens player 1's pause menu instead. | 0 or 1 | `1` |
| `menu_pause` | The match is paused while the controller options of a player 2 to 4 are open, and goes on when the last open menu is closed. 0: the match goes on meanwhile. | 0 or 1 | `1` |
| `player_scores` | Holding Back on the gamepad of a player 2 to 4 shows the scoreboard inside that player's own view. | 0 or 1 | `1` |
| `player_rumble` | The gamepads of players 2 to 4 vibrate as player 1's does. Each player can turn it off for itself with "Gamepad vibration" in its controller options. | 0 or 1 | `1` |

### Controller options of players 2 to 4

With `player_menu=1` the Start button of a player 2 to 4 opens the gamepad page of the game's own controls options in that player's view. D-pad or left stick moves, A selects, B goes back, Start closes. The match is paused while the menu is open (`menu_pause`); with several menus open it goes on when the last one is closed. The page offers look inversion, stick layout (default, southpaw, legacy, legacy southpaw), button layout (default, tactical, lefty, N0M4D, charlie) and look sensitivity (1 to 14). A button layout changes which physical button does the action of which `btn_*` key: tactical swaps B and the right stick click, lefty swaps the triggers, the bumpers and the stick clicks, N0M4D is tactical with the aim button as a toggle, charlie puts the lethal grenade on the left trigger and the aim toggle on the right bumper. The look sensitivity multiplies the turn speed of the right stick; 4 (medium) is 1.0.

The choices are written to `bo2ss_players.ini` in the game folder, one section per player (`[player2]` to `[player4]`), and read again at the next start:

| Key | Meaning | Values | Default |
|---|---|---|---|
| `sensitivity` | Look sensitivity as a factor of the turn speed. | percent, 40 to 400 | `100` |
| `invert` | Look inversion. | 0 or 1 | `0` |
| `buttons` | Button layout. | 0 default, 1 tactical, 2 lefty, 3 N0M4D, 4 charlie | `0` |
| `sticks` | Stick layout. | 0 default, 1 southpaw, 2 legacy, 3 legacy southpaw | `0` |
| `rumble` | Gamepad vibration of this player (with `player_rumble=1`). | 0 or 1 | `1` |

## Log

| Key | Meaning | Values | Default |
|---|---|---|---|
| `trace` | How much the mod writes to `bo2ss.log` in the game folder. | 0 = a short log; 1 = also the game's console output, menu events and the state of the menus (a large file, for a bug report that needs it) | `0` |

## The settings window

`panel.bat` opens a window that edits `bo2ss.ini`. Copy `panel.ps1` and `panel.bat` into the game folder, next to `t6zm.exe`, and double click `panel.bat`. The window changes the same file the mod reads, and the mod reads most keys every frame, so a change shows in a running game. A key that the file does not have yet is added at the end of the file the first time it is changed. The window also notices when the file is changed from outside (a text editor) and shows the new values.

It finds the game folder in this order: the folder it lies in, when `t6zm.exe` is there; the folder given with `-GameDir "<path>"` (`panel.bat` passes its arguments on); the Steam library entry of the game. If none is found, the window says so and edits nothing. Its descriptions and defaults come from the `bo2ss.ini` next to the script, and, when there is none, from the one in the game folder.

`panel.bat` starts PowerShell with the execution policy set for that one call, because Windows would otherwise refuse to run an unsigned script. The script is plain text next to it and can be read first.

What the window shows:

- the top bar: buttons that open `bo2ss.ini` and `bo2ss.log` in Notepad, and whether the game is running;
- the tab "Game": the start settings (map, start location, game type, automatic start, joining in the lobby), look and aim, the screen;
- the tab "Players and pads": the player count, the split direction, the input device of each player, chosen from a list or by pressing a button on the device, the live state of each pad (connected, buttons held), the button layout and the stance hold time;
- the bottom: the end of `bo2ss.log`, which can follow the log while the game runs and be filtered, for a bug report.

The window offers only maps and start locations that are listed under Start above.

## Keys that are not in the file

The DLL reads further switches that are not in the file: they turn single fixes on and off, write extra lines to the log, or switch an experiment on. They are for tracking down a problem and have working defaults. They are documented in [diagnostics](diagnostics.md), together with how to read `bo2ss.log` for a bug report.
