@echo off
rem Opens the settings window (panel.ps1 next to this file) without a console window staying open.
rem Works from tools\ and from the game folder when both files are copied there. The execution policy
rem is set for this one call only; arguments (-Tab pads, -GameDir "path") are passed on.
start "" powershell -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0panel.ps1" %*
