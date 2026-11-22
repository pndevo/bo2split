# Changelog

All notable changes to bo2split are listed here, newest first. Each version has a release with the built DLL.

To release: rename the section "Unreleased" to the new version (`## v1.2.3`), add an empty "Unreleased" above it, set the same number in `src/core/version.h`, and merge `develop` into `main`. The workflow then tags the merge and makes the release with this section as its text.

## Unreleased


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
