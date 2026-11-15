# Contributing

bo2split is a 32-bit DLL written in C++ that hooks the game's engine. This page says how to build it, where things are, the rules for hooks, and how to report a bug.

## Build

You need Windows and the Visual Studio Build Tools with the C++ workload. Run:

```
build.bat
```

It compiles the `.cpp` files of the folders under `src\` and writes the DLL to `build\XINPUT1_3.dll`. The build must be free of warnings (`/W4`). Copy the DLL and `bo2ss.ini` into the game folder next to `t6zm.exe` to try it, with Steam in offline mode.

## Layout of the code

One source file per concern, grouped in folders under `src/` (`core`, `engine`, `menu`, `input`, `shadow`, `view`, `hud`, `effects`); the table is in [docs/how-it-works.md](docs/how-it-works.md#source-files). `src/core/common.h` holds the shared helpers (`Log`, ini access, memory helpers) and the `Detour` and `BytePatch` structs, `src/shadow/shadow.h` the constants and the globals shared by the player modules. The design is explained in [docs/how-it-works.md](docs/how-it-works.md); read it before changing the view or client code.

## Rules for hooks

The game is a closed executable. These rules keep the code checkable and the repository free of the game's own code.

- **Address and role in a comment.** Every address the code touches has a comment that says what the function or field is. An address without a role is a number nobody can check.
- **Expected bytes next to the hook.** Each hook has an array with the bytes the function must start with (the `kPro...` arrays). `InstallDetour` compares them and refuses to install the hook on a mismatch, logging `unexpected bytes`. Verify the prologue before you write the hook, and copy the bytes into the array.
- **Prose, not decompiler output.** Do not paste decompiled code or copy function bodies from the game into the repository. Describe in words what a function does, which arguments it takes and which data it touches.
- **Name only what you verified.** A function gets a name in a comment only when its role is clear from its callers, the data it touches or a string it uses. Otherwise write its address.
- **Hooks are 5-byte jump detours with a trampoline** (`InstallDetour`). Use naked assembly only where a function takes an argument in a register.
- **One game build.** The code supports one executable. Check the prologue, not the version string.
- **A new setting is documented in `bo2ss.ini`** with a comment, and defaults to the behaviour that is known to work. Diagnostic settings default to 0.
- **No game content.** No executables, fastfiles, script dumps, textures or models, in any form, ever.
- **No cheats and no online features.** The mod is for local co-op in Steam offline mode.

## Branches and pull requests

```
fix/round-counter ──┐
feat/pause-menu ────┼──> develop ──> main ──> release
docs/controls ──────┘
```

- **Work happens on a branch of its own**, made from `develop` and named after the kind of change: `feat/...`, `fix/...`, `perf/...`, `refactor/...`, `docs/...`, `build/...`, `ci/...` or `chore/...`, with a short name in lower case after the slash (`fix/round-counter`).
- **`develop` only changes by a pull request** from such a branch. Nothing is pushed to it directly.
- **`main` only changes by a pull request from `develop`.** It holds the released state. A merge makes a release when `CHANGELOG.md` names a version that has no tag yet (its first `## v1.2.3` heading); the workflow creates the tag.
- After a release `main` may be merged back into `develop`, so that the two stay level.
- The workflow checks all of this on every pull request (the job `branch-policy`) and builds every push and every pull request (the job `build`). A pull request is merged when both pass.
- The title of a pull request has the form of a commit message (see below); a pull request from `develop` to `main` may also be titled `release: v1.2.3`.
- Add a line to the "Unreleased" section of [CHANGELOG.md](CHANGELOG.md) for a change a player would notice.

## Commit messages

Commits follow [Conventional Commits](https://www.conventionalcommits.org/): `type(scope): summary`.

```
fix(hud): round counter of players 2 to 4 follows the round
feat(input): run-up of the turn rate can be switched off
docs(readme): shorter, controls moved to docs/controls.md
```

| Type | For |
|---|---|
| `feat` | something a player can do or set that was not there before |
| `fix` | a bug that is gone |
| `perf` | the same behaviour, faster |
| `refactor` | code moved or renamed, the compiled behaviour unchanged |
| `docs` | README, the documents under `docs/`, comments |
| `build` | `build.bat`, compiler options |
| `ci` | the workflow and the issue forms |
| `chore` | everything else that touches no behaviour (formatting, file moves) |

- **Scope** is the folder under `src/` the change is mostly in: `core`, `engine`, `menu`, `input`, `shadow`, `view`, `hud`, `effects`. Other scopes: `panel`, `ini`, `readme`, `changelog`. Leave the scope out when a change has no single place.
- **Summary**: what is different afterwards, in the present tense, lower case, no full stop, at most about 72 characters. Say the effect ("counter follows the round"), not the work ("change swap logic").
- **Body** (optional, after an empty line): why, and the engine addresses involved. Say how the change was checked, and what was not checked.
- **A change that breaks a setting or a default**: `feat(ini)!: ...` and a line `BREAKING CHANGE: ...` in the body.
- One change per commit. A fix and an unrelated cleanup are two commits.
- The title of a pull request follows the same form.

## Report a bug

Open an issue with the bug report form. It asks for:

1. `bo2ss.log` from the game folder, from the run in which the problem happened. Start the game fresh, reproduce the problem, close the game, then take the file.
2. Your `bo2ss.ini`.
3. What you did, what you expected, what happened. Say how many players there were, which devices they used (keyboard and mouse, which pads), which map, and whether it happens every time.
4. For a crash, the last lines of the log; the mod logs exceptions with their address.

The first lines of the log show which game build was found and whether Steam was in offline mode. If the log says `unknown game image`, the executable is not the supported one and the mod did nothing.
