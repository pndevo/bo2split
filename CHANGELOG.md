# Changelog

All notable changes to bo2split are listed here, newest first. Each version has a release with the built DLL.

To release: rename the section "Unreleased" to the new version (`## v1.2.3`), add an empty "Unreleased" above it, and merge `develop` into `main`. The workflow then tags the merge and makes the release with this section as its text.

## Unreleased

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
