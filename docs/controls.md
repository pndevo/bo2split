---
title: Controls
kind: reference
description: What each button and key does for players 2 to 4 on a gamepad and on keyboard and mouse, how the player's own controller options are opened and used, and which device player 1 plays on.
status: active
updated: 2026-11-15
---

# Controls

Player 1 plays through the game's own bindings, on keyboard and mouse or on a gamepad. Players 2 to 4 use the layouts below. Their pad layout is shared by the three players and can be changed with the `btn_*` settings (see [configuration](configuration.md#buttons)).

## Gamepad, players 2 to 4

| Input | Action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| Right trigger | Fire |
| Left trigger | Aim down sights |
| Right bumper | Lethal grenade |
| Left bumper | Tactical grenade |
| X | Use / reload |
| A | Jump (stands up first when crouched or prone) |
| B | Crouch (tap), prone (hold) |
| Left stick click | Sprint |
| Right stick click | Melee |
| Y | Next weapon |
| D-pad up, down, left, right | Action slots 1 to 4 |
| Start | The player's own controller options |
| Back | Scoreboard in the player's own view, while held |

Sprinting and then holding the crouch button dives to prone. A press of the sprint button while crouched or prone stands the player up.

Start opens the game's controller options for that player, inside the player's own view: look inversion, stick layout, button layout and look sensitivity. D-pad or left stick moves, A selects, B goes back, Start closes. The match is paused while the menu is open, for everyone, and goes on when it is closed. The menu itself shows in that player's view only. The choices are kept per player in `bo2ss_players.ini` in the game folder. A button layout chosen there (tactical, lefty, N0M4D, charlie) changes which button does what on top of the table above. Player 1 uses the game's own pause and options menu.

## Keyboard and mouse, players 2 to 4

This layout is fixed. Only one player at a time can have the keyboard and mouse; while a player 2 to 4 has them, they no longer reach player 1.

| Input | Action |
|---|---|
| W A S D | Move |
| Mouse | Look |
| Left mouse button | Fire |
| Right mouse button | Aim down sights |
| G | Lethal grenade |
| Q | Tactical grenade |
| F or R | Use / reload |
| Space | Jump |
| C or left Ctrl | Crouch (tap), prone (hold) |
| Left Shift | Sprint |
| V or E | Melee |
| 1 or middle mouse button | Next weapon |
| 2 | Previous weapon |
| 3, 4, 5, 6 | Action slots 1 to 4 |
| Escape | Pause menu of player 1 |
