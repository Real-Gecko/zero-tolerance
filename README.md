# Zero Tolerance for Windows and Linux

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A native PC port of **Zero Tolerance**, the 1994 first-person shooter by Technopop for the
Sega Mega Drive / Genesis. The game's 68000 code was translated by hand into C++ and runs on
SDL3. It is not an emulator: there is no ROM and no emulated console at run time.

## Freely available

Randel B. Reiss, the owner of the Zero Tolerance copyright and trademark, made the original
Mega Drive ROM royalty-free for distribution. From the [Technopop site](http://www.technopop.net/):

> Randel Reiss, the owner of the Zero Tolerance copyright and trademark, has made the original
> Zero Tolerance Sega Genesis ROM image royalty-free for distribution. Portions of the ROM image
> are copyright Sega and although Sega has not actively prevented royalty-free distribution,
> they have never official granted permission for distribution.

Everything in `data/` was extracted from that ROM: levels, graphics, palettes, text, music
and sound effects. Because the game is freeware, the release ships the complete game and you
don't need to supply anything.

## What the port does

- Runs the original game logic, so levels, enemies and saves behave as on the console.
- Draws the 3D view with its own renderer, at the console's 320x224.
- Plays the music on an emulated YM2612 sound chip, as the console does. Sound effects come
  from the original data.
- Adds mouse look and gamepad support.
- Leaves out the two-console link cable mode.

## Download

Get the zip for your system from [Releases](../../releases):

- **Windows:** `zt-<version>-Windows-x64.zip`. Unpack it and run `zt.exe`.
- **Linux:** `zt-<version>-Linux-x64.tar.gz`. Install SDL3 from your distribution, unpack, and run `./zt`.

## Controls

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Move, strafe | W / S, A / D | Left stick |
| Turn | Mouse | Right stick |
| D-pad | Arrows | D-pad |
| Jump | Space | Left bumper |
| Crouch (toggle) | Left Ctrl | Right bumper |
| Fire | Left click | Right trigger |
| Switch item | Right click | Left trigger |
| Map | Tab | Select |
| Pause menu | Escape | Start (in gameplay) |
| Pad A / B / C | Z / X / C | South / West / East |
| Pad Start | Enter | Start (outside gameplay) |
| Fullscreen | Alt+Enter | |

The original pad is the arrows (or the gamepad's D-pad), Z/X/C (South/West/East) and
Enter. Everything else is a separate PC control that works only in gameplay.

Escape opens the pause menu and releases the mouse, a click captures it again.

## Command line

| Option | Meaning |
| --- | --- |
| `--data <dir>` | Game data folder (default `data`) |
| `--scale <n>` | Window size as a multiple of 320x224 (default 3) |

Saves go to `%APPDATA%\ZeroTolerance\zt` on Windows and `~/.local/share/ZeroTolerance/zt`
on Linux.

## Building

Needs CMake 3.20+, a C++20 compiler and SDL3.

```sh
make        # Linux: ./zt
make win    # Windows cross-build with mingw-w64: zt.exe and its DLLs
```

`make win` downloads the official SDL3 Windows release into `build/win-deps`.

Pushing a tag runs `.github/workflows/release.yml`, which builds both versions and attaches
the archives to a GitHub release.

## License

The project has parts with different owners:

- **Port code:** the files in `src/` outside `src/game/` and `src/ymfm/`, the build files and the release
  workflow are under the [MIT License](https://opensource.org/license/mit).
- **Game code:** `src/game/` is translated from the original Zero Tolerance and stays
  © Randel B. Reiss. It is distributed under his royalty-free permission quoted above.
- **Game data:** everything in `data/` was extracted from the ROM and is © Randel B. Reiss,
  with portions © Sega, under the same permission.
- **Libraries:** `src/ymfm/` is [ymfm](https://github.com/aaronsgiles/ymfm) by Aaron Giles
  (BSD-3-Clause). The Windows release includes SDL3 (zlib license).
