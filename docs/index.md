---
title: bo2split documentation
kind: index
description: The documents of the public repository: the controls, the settings of bo2ss.ini and the settings window, how the split-screen mod is built from the proxy DLL to the render thread, and the diagnostic switches with how to read bo2ss.log.
status: active
updated: 2026-11-15
---

# bo2split documentation

Documentation of bo2split, a split-screen mod for the Steam version of Call of Duty: Black Ops II Zombies. The README in the repository root covers install, the start of a match and the known limits.

| Document | One line |
|---|---|
| [controls](controls.md) | What each button and key does for players 2 to 4 on a gamepad and on keyboard and mouse, how the player's own controller options are opened and used, and which device player 1 plays on. |
| [configuration](configuration.md) | Every setting of bo2ss.ini with its meaning, values and default, grouped as in the file, the settings window that edits the file while the game runs, and where the switches that are not in the file are documented. |
| [how-it-works](how-it-works.md) | The design of the split-screen mod, from the proxy DLL and its per-frame driver through the shadow clients, the view passes, the per-player cgame block, HUD roots, input devices and sound listeners to the render-thread side, with a table of the source files. |
| [diagnostics](diagnostics.md) | How to read bo2ss.log for a bug report, and the switches that are not in bo2ss.ini - the fix switches that turn single parts of the mod on and off, the experimental switches and the diagnostic switches that write extra lines to the log. |
