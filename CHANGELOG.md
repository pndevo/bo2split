# Changelog

All notable changes to bo2split are listed here, newest first. Each version has a release with the built DLL.

To release: rename the section "Unreleased" to the new version (`## v1.2.3`), add an empty "Unreleased" above it, set the same number in `src/core/version.h`, and merge `develop` into `main`. The workflow then tags the merge and makes the release with this section as its text.

## Unreleased

- Fixed: a player 2, 3 or 4 that was down had a still camera while the hand with the weapon moved. The camera now sways as player 1's does.
- Fixed: players 3 and 4 started to aim down sights about a tenth of a second later than player 2. Their zoom now starts with the trigger, as player 2's does.

## v0.3.0

- Fixed: a rare crash while the HUD of player 2, 3 or 4 was set up, seen when player 4 joined in a later match of one session. The mod used the game's menu script state while the game's own clean-up of that state ran on another thread. The mod now takes the game's lock for it.
- Fixed: the blur the game puts on a player's view (standing in the lava) lay on the views of all players when player 1 was the one hurt, and a player 2 to 4 never got its own. Every player now has its own. Health and damage were never shared. ([#19](https://github.com/pndevo/bo2split/issues/19))
- Fixed: when player 1 played on a gamepad, a button press of player 1 could act on another player: now and then the weapon of player 2, 3 or 4 changed when player 1 switched weapons, and player 1's own switch was lost. The game reads player 1's gamepad once per view; the buttons are now worked off for player 1's own view only. ([#19](https://github.com/pndevo/bo2split/issues/19))
- Fixed: a see-through effect over the whole view (a fade, the flash of a nuke) was stronger on the view of player 2 than on player 1's, stronger again for player 3 and for player 4. It was drawn once more for every player before them. It is now drawn once.
- Fixed: while player 1 was dead and watched another player, the views of the other living players were broken: the camera sat far from the player and moved, the player's own body was in the picture, the weapon was missing, and player 1's "next player" button fired the weapons in those views. Player 1's spectator bar also showed in every view.
- Fixed: a player 2, 3 or 4 that was down kept the normal picture. It now gets the grey picture with the red edge, as player 1 does, until it is revived.
- Fixed: the colours of the views of players 2 to 4 were off (washed-out dark tones, a yellow cast). They kept the colour settings of the start of the match; they now follow player 1's.

## v0.2.3

- Fixed: after "Restart level" the HUD of players 2 to 4 could miss the score line of another player until that player's score changed. The score lines are sent to them again after a restart.
- Fixed: after "Yes" in one of the mod's messages (a newer release is out, the script update is missing) Steam could refuse the next start of the game with "game is already running". The browser was started by the game, and Steam counted the game as running until that browser was closed. The page is now opened so that the browser does not belong to the game. Until you have this version: close the browser, then start the game again.

## v0.2.2

- Fixed: with three or four players a second match in the same session ended with "UI Error" right after its start. The HUD of players 3 and 4 from the match before was drawn until those players had joined again; it is now dropped when the new match starts.
- Fixed: after "Restart level" in the pause menu the players 2 to 4 stood still without a weapon and took no input. The server gives itself a new id at a restart and ignored these players from then on; they now take the new id as player 1 does.

## v0.2.1

- The mod tells when a newer release is out: once per start of the game it asks GitHub for the newest release and shows a message, once per version, with an offer to open the release page. It downloads and installs nothing. `update_check=0` in `bo2ss.ini` switches the request off.

## v0.2.0

- Fixed: no match started on an installation that never had the game's script update on disk; the start ended with `Unresolved external: 'checkscorelimit'`. The file was never part of a release and the README did not name it. The mod now saves it by itself: start the game once while Steam is online, a message tells when the file is written. The README describes this and a download as a second way. ([#7](https://github.com/pndevo/bo2split/issues/7))
- Without the script update the mod no longer opens the lobby in offline mode; a message says what to do.
- While Steam is online the mod leaves the game untouched: no patch, no hook. Before, the sign-in patches were applied in that case too, and the game stayed at "Connecting to online service".
- Start on any gamepad pauses the match for everyone. Player 1 gets the pause menu, every player 2 to 4 its controller options; Start again closes all menus and the match goes on. Before, Start of a player 2 to 4 opened only that player's options and the match went on.
- Player 1's pause menu is drawn inside player 1's view, not across the views. `pause_menu_in_view=0` in `bo2ss.ini` brings the old place back.
- Fixed: player 1's mouse cursor moved the highlight in the menus of the other players.
- Branch rules: work goes to `develop` by a pull request from a branch named after the kind of change, `main` only takes `develop`; the workflow checks it.

## v0.1.2

- Fixed: player 1's muzzle flash sat in the middle of the screen, across the border between the views, and showed in the other views as well. It sits at player 1's muzzle now, in player 1's view only.
- Fixed: player 1's shell casings flew next to the weapon of every other player. In split-screen the casings of the first-person weapon are no longer drawn.
- Players 2 to 4 see the muzzle flash of their first-person weapon. Before, they saw the flash other players see, a little beside the weapon.
- README: the list of known limits no longer names the burning effect; it shows for every player.

## v0.1.1

No change to the mod itself; the DLL is built from the same sources as v0.1.0.

- Controls moved from the README to `docs/controls.md`; the README is shorter and starts with install and the start of a match.
- Bug report form for issues.
- Branches: `develop` for the work, `main` for releases.
- Commit messages follow Conventional Commits (`CONTRIBUTING.md`).
- A merge into `main` makes the release by itself when this file names a new version. The release zip also holds the documents and pictures, and the release text lists SHA256 checksums.

## v0.1.0

First public version.

- Local split-screen for two to four players in one running game: two halves side by side or top and bottom, three players as one wide view above two quarters or as three quarters, four quarters.
- Every player has a camera, a first-person weapon, a HUD, a scoreboard and a sound listener of their own.
- Up to four XInput gamepads, or keyboard and mouse for one player and gamepads for the others.
- Players join in the game's own local lobby with A and leave with B.
- Players 2 to 4 have the game's controller options in their own view: look sensitivity, inversion, stick layout, button layout and vibration, kept per player.
- The right stick of every player uses the game's response curve and turn rates; the turn rate does not depend on the number of views. `look_accel=0` switches the run-up of the turn off.
- Each gamepad vibrates for its own player.
- Settings window `panel.bat` for `bo2ss.ini`.
- Supported: the Steam version of `t6zm.exe` (PE timestamp `0x6a8332d9`), in Steam's offline mode.
