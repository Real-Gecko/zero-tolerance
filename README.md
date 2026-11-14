# Zero Tolerance for Windows and Linux

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

Everything in `data/` except `gm.sf2` was extracted from that ROM: levels, graphics, palettes,
text, music and sound effects. Because the game is freeware, the release ships the complete
game and you don't need to supply anything.

`data/gm.sf2` is a General MIDI soundfont used to play the music
([musical-artifacts.com #713](https://musical-artifacts.com/artifacts/713)). It is a
conversion of the Microsoft GS Wavetable set, and its license is unclear. To use another
soundfont, pass `--soundfont <file.sf2>`.

## What the port does

- Runs the original game logic, so levels, enemies and saves behave as on the console.
- Draws the 3D view with its own renderer, at the console's 320x224.
- Plays the music as MIDI through FluidSynth. Sound effects come from the original data.
- Adds mouse look and gamepad support.
- Leaves out the two-console link cable mode.

## Download

Get the zip for your system from [Releases](../../releases):

- **Windows:** `zt-<version>-Windows-x64.zip`. Unpack it and run `zt.exe`.
- **Linux:** `zt-<version>-Linux-x64.tar.gz`. Install SDL3 and FluidSynth from your
  distribution, unpack, and run `./zt`.

## Controls

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Move, turn | Arrows, or W/S with the mouse | D-pad or left stick |
| Strafe | A / D | |
| Fire (B) | X or left click | South (A on Xbox) |
| Switch item (C) | C or right click | East |
| A button | Z | West |
| Start / pause | Enter or Tab | Start |
| Fullscreen | Alt+Enter | |

Escape releases the mouse, a click captures it again.

## Command line

| Option | Meaning |
| --- | --- |
| `--data <dir>` | Game data folder (default `data`) |
| `--soundfont <file>` | Soundfont for the music (default `data/gm.sf2`) |
| `--scale <n>` | Window size as a multiple of 320x224 (default 3) |

Saves go to `%APPDATA%\ZeroTolerance\zt` on Windows and `~/.local/share/ZeroTolerance/zt`
on Linux.

## Building

Needs CMake 3.20+, a C++20 compiler, SDL3 and FluidSynth.

```sh
make        # Linux: ./zt
make win    # Windows cross-build with mingw-w64: zt.exe and its DLLs
```

`make win` downloads the official SDL3 and FluidSynth Windows releases into `build/win-deps`.

Pushing a tag runs `.github/workflows/release.yml`, which builds both versions and attaches
the archives to a GitHub release.

## License

The project has parts with different owners:

- **Port code:** the files in `src/` outside `src/game/`, the build files and the release
  workflow are under the [MIT License](https://opensource.org/license/mit).
- **Game code:** `src/game/` is translated from the original Zero Tolerance and stays
  © Randel B. Reiss. It is distributed under his royalty-free permission quoted above.
- **Game data:** everything in `data/` except `gm.sf2` was extracted from the ROM and is
  © Randel B. Reiss, with portions © Sega, under the same permission.
- **Soundfont:** `data/gm.sf2` keeps its own license, which is unclear (see above).
- **Libraries:** the Windows release includes SDL3 (zlib license), FluidSynth and libsndfile
  (both LGPL-2.1).
