# SDL3 DOS Demo Suite

[English](README.md) · [Español](README.es.md)

**by @mmoroca & [Arena.AI](http://arena.ai/) 2026**

A set of small C programs for MS-DOS, built with DJGPP and the SDL3 DOS backend. SDL3 is referenced as a Git submodule pinned to an upstream commit; its source remains in the SDL project and is not copied into this repository.

## Included programs

| Executable | Source | Description and controls |
|---|---|---|
| `HELLO.EXE` | `main.c` | Minimal SDL surface example at 320×200. Draws a colored rectangle and exits on **Esc** or window close. Writes `HELLO.LOG` in the current directory when writable. |
| `MISTIFY.EXE` | `mistify.c` | Animated quadrilaterals with a 256-color palette and fading trails. Defaults: 48 trail frames, 2 polygons, normal speed. Any key or mouse movement exits. `/L` enables `MISTIFY.LOG`. |
| `SBTEST.EXE` | `soundtest.c` | Plays an original generated melody as 11025 Hz, unsigned 8-bit mono PCM and shows audio/device diagnostics. **Esc** exits; `/L` enables `SBTEST.LOG`. |
| `MODPLAY.EXE` | `modplay.c` | Plays bounded ProTracker modules (31 samples, 4 channels). Default output: 22050 Hz, 8-bit, mono. **Space** pauses/resumes and **Esc** exits. |
| `SHOWCASE.EXE` | `showcase.c` | Function-oriented SDL3 starter: 320×200 indexed display, palette fade, text/keyboard input, optional eight-way joystick, audio diagnostics and PCM effects, saved settings/high score, and a four-panel mini-shooter. **Arrows/WASD** move, **Space** fires, **T** edits the call sign, **F1** fades, **M** toggles music, **F2/F3** save/load settings, **-/+** adjusts volume, **Esc** exits. |
| `SHOOTER.EXE` | `shooter.c` | Standalone full-screen 320×200 arcade shooter built on `showcase.c`. Shows `SHOWCASE/UFO.BMP` for 5 seconds at startup, then starts runs with 1 descending UFO on one of 4 color-coded patterns; each hit spawns 2 new UFOs (up to 256 active) until an enemy collides with the ship. **Arrows/WASD** or joystick move, **Space**/button 1 launches/fires, **P** pauses, **T** edits pilot name, **M**/**-/+** control audio, **F2/F3** save/load, **Esc** fades out to DOS text mode and prints `"Gracias por jugar!"`. |
| `DINO.EXE` | `dinorun.c` | The runner attempts to dodge automatically; random agility can make its timing imprecise. Place a cactus with **Q** or a bird with **P**. Sprites default to half size (`/s 2`); `/h` shows help. **Space** starts/pauses and **Esc** exits. The Top 10 ranks shorter times first. |

## Screenshots

| Demo | Screenshot |
|---|---|
| `HELLO.EXE` | ![HELLO.EXE running in DOSBox-X](docs/hello.png) |
| `MISTIFY.EXE` | ![MISTIFY.EXE running in DOSBox-X](docs/mistify.png) |
| `SBTEST.EXE` | ![SBTEST.EXE diagnostics in DOSBox-X](docs/sbtest.png) |
| `MODPLAY.EXE` | ![MODPLAY.EXE playing a module in DOSBox-X](docs/modplay.png) |
| `DINO.EXE` | ![DINO.EXE running in DOSBox-X](docs/dino.png) |

Screenshots for `SHOWCASE.EXE` and `SHOOTER.EXE` are intentionally omitted until real DOS/DOSBox captures are uploaded.

The graphics examples request 320×200 indexed-color surfaces where supported. Mistify renders with a 256-color palette; Dino Fall uses a shared grayscale palette and transparent key; Showcase and Shooter use the same RGB 3:3:2 palette as the converted sprite BMPs. All seven programs use SDL software surfaces; the DOS backend uses VGA/VESA and does not require OpenGL or GPU acceleration. In-program text and command-line help are in English.

## Source organization

Each C file is organized into visible sections for configuration, state, initialization, input, the main loop, and cleanup. `dinorun.c` documents its state, resource ownership, timing/physics, randomized maneuver logic, collision-mask math, rendering, and event flow. `showcase.c` is the function-oriented starter template: display/palette, text drawing, input, joystick, audio, files, shooter update/render, main loop, and cleanup are separate functions. `shooter.c` turns that template into a standalone full-screen arcade game using pure integer/fixed-point motion and pixel-accurate sprite collision. See [`SHOWCASE_GUIDE.md`](SHOWCASE_GUIDE.md) for the bilingual function map and extension notes. `tools/convert_assets.py` remains project-neutral; `trimexe.sh` creates stripped executable copies while leaving the originals intact. Resources are released safely after partial initialization failures; there are no `goto` statements.

## Requirements

- CMake 3.16 or newer.
- Git.
- A DJGPP cross-compiler targeting `i386-pc-msdosdjgpp`, including its binutils.
- The SDL3 Git submodule at `SDL/`, pinned to an upstream commit containing the DOS backend and `build-scripts/i586-pc-msdosdjgpp.cmake`. SDL3 is **not vendored** in the project; the repository records only the submodule pointer. Initialize the submodule when cloning so the exact recorded SDL revision is checked out.
- For DOS runtime tests, a DPMI host such as `CWSDPMI.EXE`. Audio examples also require a Sound Blaster-compatible device or emulator configuration.

CMake looks for SDL3 in `SDL/` by default. To use a checkout elsewhere, pass `-DSDL3_DOS_SOURCE_DIR=/path/to/SDL` when configuring.

## Set up a DJGPP toolchain

Any compatible DJGPP toolchain can be used. The following is an optional macOS/Homebrew recipe using [build-gcc](https://github.com/jwt27/build-gcc); on other host operating systems, use the equivalent prerequisites documented by that project or an already installed DJGPP toolchain.

On macOS, install the command-line tools and the required utilities:

```sh
xcode-select --install   # only if the tools are not already installed
brew install bash gnu-sed texinfo nasm dos2unix cmake git
```

Set a configurable installation root, clone `build-gcc` there once, and build its minimal DJGPP toolchain. The default below is under the current user's home directory; set `BUILD_ROOT` to another path if desired.

```sh
export BUILD_ROOT="${BUILD_ROOT:-$HOME/build-gcc}"

if [ ! -d "$BUILD_ROOT/.git" ]; then
  git clone https://github.com/jwt27/build-gcc.git "$BUILD_ROOT"
fi

export DJGPP_PREFIX="${DJGPP_PREFIX:-$BUILD_ROOT/opt/djgpp}"
export PATH="$(brew --prefix bash)/bin:$(brew --prefix nasm)/bin:$(brew --prefix dos2unix)/bin:$(brew --prefix gnu-sed)/libexec/gnubin:$(brew --prefix texinfo)/bin:$PATH"
cd "$BUILD_ROOT"
"$(brew --prefix bash)/bin/bash" ./build-djgpp.sh --prefix="$DJGPP_PREFIX" minimal
```

Use the `minimal` target for this project; the broader `all` target may attempt to build the legacy `djgpp/default` aggregate. The prefix must be an absolute path. On Linux or another host, install the corresponding build dependencies and follow the host-specific instructions in the `build-gcc` documentation, or point `DJGPP_PREFIX` at an existing toolchain.

## Configure a terminal and get the sources

Define the project and toolchain locations in each terminal used for building. These defaults are examples and can be overridden; `DEMO_DIR` is explicitly defined so the build commands do not depend on the current working directory.

```sh
export BUILD_ROOT="${BUILD_ROOT:-$HOME/build-gcc}"
export DEMO_DIR="${DEMO_DIR:-$HOME/sdl3-dos-demo}"
export DJGPP_PREFIX="${DJGPP_PREFIX:-$BUILD_ROOT/opt/djgpp}"
export SDL3_DOS_SOURCE_DIR="${SDL3_DOS_SOURCE_DIR:-$DEMO_DIR/SDL}"

export DJDIR="$DJGPP_PREFIX/i386-pc-msdosdjgpp"
export GCC_EXEC_PREFIX="$DJGPP_PREFIX/lib/gcc/"
export PATH="$DJGPP_PREFIX/i386-pc-msdosdjgpp/local/bin:$DJGPP_PREFIX/i386-pc-msdosdjgpp/bin:$DJGPP_PREFIX/bin:$PATH"
export PKG_CONFIG_LIBDIR="$DJGPP_PREFIX/i386-pc-msdosdjgpp/local/lib/pkgconfig:$DJGPP_PREFIX/i386-pc-msdosdjgpp/local/share/pkgconfig:$DJGPP_PREFIX/i386-pc-msdosdjgpp/lib/pkgconfig:$DJGPP_PREFIX/i386-pc-msdosdjgpp/share/pkgconfig"
unset PKG_CONFIG_PATH
```

Check that the compiler targets DJGPP:

```sh
i386-pc-msdosdjgpp-gcc -v 2>&1 | grep Target
```

The output should contain `Target: i386-pc-msdosdjgpp`. Clone the project and initialize its pinned SDL3 submodule, or initialize the submodule in an existing checkout:

```sh
# Replace the project URL if the GitHub owner or repository name differs.
if [ ! -d "$DEMO_DIR/.git" ]; then
  git clone --recurse-submodules https://github.com/mmoroca/sdl3-dos-demo.git "$DEMO_DIR"
else
  git -C "$DEMO_DIR" submodule update --init --recursive
fi

export SDL3_DOS_SOURCE_DIR="${SDL3_DOS_SOURCE_DIR:-$DEMO_DIR/SDL}"
```

The submodule checkout is pinned by the parent repository, so `git submodule update --init --recursive` restores the exact recorded SDL revision. If you use a separate SDL checkout, set `SDL3_DOS_SOURCE_DIR` to that path instead.

## Build profiles

The SDL checkout supplies the DOS CMake toolchain file. The compiler is set explicitly to the DJGPP C compiler; this project is C-only.

### Normal Release build

```sh
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build-dos" \
  -DSDL3_DOS_SOURCE_DIR="$SDL3_DOS_SOURCE_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDL3_DOS_SOURCE_DIR/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$DJGPP_PREFIX/bin/i386-pc-msdosdjgpp-gcc"

cmake --build "$DEMO_DIR/build-dos" --parallel
```

The toolchain file is named `i586-pc-msdosdjgpp.cmake`; the compiler is explicitly set to `i386-pc-msdosdjgpp-gcc`. CMake creates `hello.exe`, `mistify.exe`, `sbtest.exe`, `modplay.exe`, `showcase.exe`, `shooter.exe`, and `dino.exe` in the selected build directory. The `SHOWCASE/` runtime assets are copied beside the executables for both `showcase.exe` and `shooter.exe`. If changing compiler or toolchain after configuration, use a fresh build directory so CMake does not reuse the old cache.

### Release build optimized for size

Use a separate build directory to keep the normal build intact:

```sh
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build-dos-small" \
  -DSDL3_DOS_SOURCE_DIR="$SDL3_DOS_SOURCE_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDL3_DOS_SOURCE_DIR/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$DJGPP_PREFIX/bin/i386-pc-msdosdjgpp-gcc" \
  -DCMAKE_C_FLAGS_RELEASE="-Os -DNDEBUG -ffunction-sections -fdata-sections" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections"

cmake --build "$DEMO_DIR/build-dos-small" --parallel
```

`-Os` favors smaller code; function/data sections and `--gc-sections` allow unused sections to be discarded. If the toolchain linker does not support `--gc-sections`, remove that linker option and the section flags; `-Os -DNDEBUG` can remain. This profile does not request 486-only instructions.

#### Create stripped copies

After building, `trimexe.sh` can make stripped copies of the executables in a build directory without changing the originals:

```sh
bash "$DEMO_DIR/trimexe.sh" "$DEMO_DIR/build-dos-small"
```

It scans only the specified directory (not its subdirectories), accepts `.exe` and `.EXE`, and creates a sibling copy with `str.exe` appended to the base name (for example, `dino.exe` becomes `dinostr.exe`). It uses `i386-pc-msdosdjgpp-strip --strip-all`, so symbols including debug symbols are removed from the copies. Existing files ending in `str.exe` are skipped, which makes repeat runs safe. The DJGPP strip utility must be on `PATH`; set `STRIP_TOOL=/path/to/i386-pc-msdosdjgpp-strip` to override it. Run `bash "$DEMO_DIR/trimexe.sh" --help` for usage and options.

### Release build for 486-class performance

This separate profile favors speed over size and targets 486 or newer processors; it is not suitable for a 386.

```sh
cmake -S "$DEMO_DIR" -B "$DEMO_DIR/build-dos-fast" \
  -DSDL3_DOS_SOURCE_DIR="$SDL3_DOS_SOURCE_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDL3_DOS_SOURCE_DIR/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$DJGPP_PREFIX/bin/i386-pc-msdosdjgpp-gcc" \
  -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -march=i486 -mtune=i486 -ffunction-sections -fdata-sections" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections"

cmake --build "$DEMO_DIR/build-dos-fast" --parallel
```

To compare `-O3`, use another build directory (for example, `build-dos-fast3`) and replace `-O2` with `-O3`; do not overwrite a build configured with different flags.

A reported 486 DX2/50 test ran `MODPLAY.EXE` comfortably at 22050 Hz, 16-bit stereo. At 44100 Hz, 16-bit stereo, the reported `-O2` run generated about 44,150 frames/s with a 61 ms maximum audio-fill time; `-O3` generated about 45,090 frames/s with a 57 ms maximum. The target queue is about 65 ms, so the higher rate still has little headroom. These are hardware-specific measurements, not performance guarantees.

## Running in DOS or DOSBox

DJGPP creates 32-bit DPMI executables. Provide a compatible DPMI host, such as `CWSDPMI.EXE`, alongside the programs unless the runtime environment supplies one. Mount the selected build directory as drive `C:` in DOSBox or DOSBox-X.

```text
C:
HELLO.EXE
```

### Systems Showcase template

```text
SHOWCASE.EXE
```

`SHOWCASE.EXE` requests the SDL3 DOS backend's 320×200 indexed display (Mode-13h-style; it does not issue BIOS interrupts), then ramps the RGB 3:3:2 palette from black to full color. Four panels show general state and a rainbow-text palette, audio/`BLASTER` diagnostics, keyboard/text/joystick input, and a black-background starfield shooter. The three Kenney sprites are indexed BMPs drawn at 1:4 size (28×19 ship, 23×23 UFO, 4×10 laser). Source OGG music/effects are supplied as 11025-Hz unsigned 8-bit mono WAVs; the DOS target uses SDL3 core audio and does not link SDL3_mixer. See [`SHOWCASE_GUIDE.md`](SHOWCASE_GUIDE.md) for why and for the supported SDL3_mixer alternative.

Controls: **arrows/WASD** move; **Space** or joystick button 1 fires; **T** starts call-sign text entry, **Backspace** edits, and **Enter** saves it; **F1** toggles the palette fade; **M** toggles music; **F2/F3** save/load `SHOWCASE.CFG`; **-/+** adjust master volume; **Esc** exits with a palette fade-out. The engine clip is mixed at exactly half the master volume. A new best shooter score is written immediately to `SHOWCASE.SCO`. Both files are plain-text runtime data in the current directory and are ignored by Git. A joystick is optional; without one, the display reports its absence and the keyboard remains usable. See [`SHOWCASE_GUIDE.md`](SHOWCASE_GUIDE.md) for the function-by-function overview and asset conversion details.

### Arcade Shooter

```text
SHOOTER.EXE
SHOOTER.EXE /H
```

`SHOOTER.EXE` adapts the `showcase.c` foundation into a full-screen 320×200 arcade game. At startup it displays the full-screen 320×200 indexed presentation screen `SHOWCASE/UFO.BMP` (`showcase-src/UFO.png`) for 5 seconds before transitioning to the title menu, and pre-bakes the six indexed gameplay sprites (`PLAYER~1.BMP`, `UFOBLUE.BMP`, `UFOYEL~1.BMP`, `UFORED.BMP`, `UFOGREEN.BMP`, and `LASERR~1.BMP`) at 1:4 size into compact contiguous buffers so the scanline blitter stays friendly to a 486 L1 cache even with hundreds of enemies on screen. Audio mixes the 11025-Hz mono PCM WAV effects using pure integer and 24.8 fixed-point math (no FPU required), the mouse cursor is hidden while running, and exiting via **Esc** fades the palette out, restores MS-DOS text mode (`SDL_Quit`), and prints `"Gracias por jugar!"` right before returning to the DOS prompt (`F1` manual fade is removed; `F2`/`F3` save/load `SHOOTER.CFG`).

Each run starts with **1 UFO** descending from the top on one of four color-identified behaviors: **blue** = sinusoidal wave, **yellow** = timed diagonal zigzag, **red** = alternating fast swoop dives, and **green** = diagonal drift with course shifts. Destroying an enemy increments the score and immediately spawns **2 new descending UFOs** (`ENEMIES_PER_KILL = 2`, raising the simultaneous active pool up to `MAX_ENEMIES = 256`, plus an overflow queue). Enemies reaching the bottom wrap back to the top with a new random X position and behavior/color. Collisions check non-transparent pixels after fast vertical/bounding-box rejection; the run ends immediately when an enemy sprite impacts the player ship. Settings, records, and startup/summary logs are stored separately in `SHOOTER.CFG`, `SHOOTER.SCO`, and `SHOOTER.LOG` (ignored by Git, with zero per-frame disk I/O during gameplay).

### Dino Fall

```text
DINO.EXE
DINO.EXE /s 1
DINO.EXE /s 2
DINO.EXE /s 3
DINO.EXE /h
```

The dinosaur attempts to jump over cacti and duck under birds. Its agility is randomized for each obstacle: an offset from 100 ms early to 260 ms late shifts cactus takeoff away from the calculated safe point, while for birds it shifts duck onset later and releases the duck early (or, for negative rolls, starts sooner). A poor roll can make either maneuver imprecise, and direct collisions are checked against the sprite masks. The title screen reads “MAKE THE DINO FALL AS FAST AS YOU CAN!” Place obstacles in its path: **Q** places a cactus and **P** places a bird. **Space** starts a run, pauses/resumes it, or starts another run after a collision; **Esc** exits. A three-note high melody plays on a jump and a four-note low melody on a loss when audio is available.

Sprite size 2 (half-size) is the default. `/s 1` displays source sprites at full 1:1 size, `/s 2` draws them at half size, and `/s 3` selects the previous smallest custom-scaled size; the display remains 320×200. `/h` shows help. For compatibility, `--native-sprites`, `--half-sprites`, and `--scaled-sprites` remain aliases for sizes 1, 2, and 3; `--help` also works. Game Over and Reset follow the selected size: at size 2, the 386×40 title is 193×20 and the 75×101 reset icon rounds up to 38×51 pixels. Odd half-size dimensions round up to a whole pixel.

Each run ends when the dinosaur collides. The run duration is recorded in milliseconds; **shorter times rank higher**. The player starts with 2 charges and a capacity of 6; one charge is replenished every 5 seconds initially, and the capacity grows by 1 every 30 seconds. Refill time decreases by 0.5 seconds every 10 seconds, down to 2 seconds. The placement cooldown starts at 0.4 seconds and drops by 0.05 seconds every 15 seconds, down to 0.15 seconds. Each obstacle gets an independent random agility error from 100 ms early to 260 ms late. For cacti it shifts the jump takeoff away from the calculated safe window; for birds it shifts duck onset later and releases the duck earlier, so a poor roll can cause a direct collision. Negative values favor earlier movement. The jump arc still adapts to cactus size and speed. The runner starts at 104 px/s, ramps gradually, and caps at 190 px/s. Placement spacing enforces a 350 ms travel gap plus an 8-pixel margin and the runner width. The gameplay HUD shows time, supply, and speed; the old `CD` cooldown meter, temporary placement/wait messages, and bottom control footer have been removed. The controls are listed above.

The local Top 10 is stored as plain text in `DINO.SCO` in the current working directory. If a run qualifies, enter a name of up to 8 letters, digits, hyphens, or underscores. `DINO.SCO` is runtime data and is ignored by Git. The game loads the runner, birds, six cactus variants, clouds, track, game-over title, and reset icon from the local `Images/` folder. The preferred layout keeps its DOS-friendly subfolders and filenames (for example, `Images/DINO/RUN1.BMP`); the loader also tries common flattened BMP layouts beside the executable. The PNGs are not loaded directly. It searches beside `DINO.EXE` first, then in the current working directory. CMake copies `Images/` beside the executable; if you move the executable, copy that whole folder alongside it. The BMPs are 8-bit grayscale/indexed with a transparent palette key, so no SDL_image library is required.

The checked-in runtime BMP set in `Images/` uses the game's shared grayscale palette and transparent index 5. `tools/convert_assets.py` is a separate, general-purpose PNG-to-BMP utility: run `python3 tools/convert_assets.py PNG_SOURCE_DIRECTORY BMP_OUTPUT_DIRECTORY`. It recursively converts non-interlaced PNGs, preserves their dimensions (no fixed 320×200 limit; bounded by the BMP format and available memory), and writes 8-bit BMPs that all share a 256-entry RGB 3:3:2 palette. Long names receive deterministic uppercase DOS 8.3 aliases. Transparent pixels are composited over black by default; choose another matte with `--matte RRGGBB`. `--clean` removes stale BMPs after successful conversion, while preserving the newly generated BMPs and non-BMP files. The utility uses only Python's standard library. Its shared RGB 3:3:2 palette is not the game's grayscale/transparency-key palette, so don't use it to overwrite the current `Images/` runtime assets without deliberately changing the game's palette handling.

### Mistify

```text
MISTIFY.EXE /H
MISTIFY.EXE /T 64 /P 3 /S 1
MISTIFY.EXE /T 64 /P 3 /S 1 /L
```

`/T` sets trail length in frames (1–100), `/P` sets the number of quadrilaterals (1–8), and `/S` sets speed (`1` fast, `2` normal, `3` slow). `/L` enables `MISTIFY.LOG`; logging is off by default and does not write a line per frame. Any key or mouse movement exits. The pointer is hidden while the demo runs. Mistify requests an `INDEX8` surface and configures a 256-color palette; if the DOSBox-X window uses true color, SDL converts the indexed surface to the display format.

### Sound test

```text
SBTEST.EXE /H
SBTEST.EXE
SBTEST.EXE /L
```

The test generates its own 11025 Hz, unsigned 8-bit mono melody in 256-sample blocks and maintains a short SDL audio queue. The screen reports the selected device/driver and formats, queue depth, low/empty-queue events, generated samples, loop rate, and a waveform. `/L` enables `SBTEST.LOG`, which records setup and periodic diagnostics rather than every sample. Press **Esc** to stop playback and exit.

The DOS SDL backend uses the `BLASTER` environment variable. In DOSBox-X, configure the emulated card and make sure `BLASTER` agrees with its actual resources. For example, `A220 I5 D1 H5 T6` was used in one tested Sound Blaster-compatible setup; values vary by hardware and should not be copied blindly. If initializing a PnP card with UNISOUND, set the correct `BLASTER` values before running UNISOUND.

### Four-channel MOD player

`MODPLAY.EXE` accepts one `.MOD` file (case-insensitive extension). The current parser is limited to ProTracker-style modules with 31 samples and four channels, recognized by `M.K.`, `M!K!`, `FLT4`, `4CHN`, or `N.T.` signatures.

```text
MODPLAY.EXE /H
MODPLAY.EXE MUSIC\SONG.MOD
MODPLAY.EXE /R 11025 /B 8 /C MONO MUSIC\SONG.MOD
MODPLAY.EXE /R 44100 /B 16 /C STEREO /L MUSIC\SONG.MOD
```

Defaults are **22050 Hz, 8-bit, mono**. `/R` accepts `11025`, `22050`, or `44100`; `/B` accepts `8` or `16`; `/C` accepts `MONO`/`STEREO` or `1`/`2`. These select the PCM mix format; SDL may convert it to the device's supported format. **Space** pauses/resumes and **Esc** exits.

The player mixes four PCM voices and supports common ProTracker effects, including arpeggio, slides, portamento, vibrato, sample offset, volume slide, pattern jump/break, speed/BPM, and several `E` sub-effects. Eight-bit playback uses nearest-neighbor sample reads; 16-bit playback uses linear interpolation. The target audio queue is about 65 ms. The UI refreshes at 10 Hz, drawing a full frame initially and then updating changed regions with SDL dirty rectangles. Transfer savings depend on the video mode; a linear framebuffer path may still copy the full screen. This is not a complete tracker implementation, so uncommon effects may be ignored and playback can differ from a full-featured MOD player.

`/L` enables `MODPLAY.LOG`, which records module/audio setup, playback position, and periodic audio-fill and drawing-time statistics. Logging is off by default. Audio-queue-empty events are useful warning signals but do not necessarily mean the hardware buffer has already run out of sound.

## License and third-party software

The project-authored code and documentation are licensed under MIT; see [`LICENSE`](LICENSE). That grant does not cover third-party assets. The existing Dino Fall artwork has its own provenance note in [`Images-src/README.md`](Images-src/README.md). The new Showcase sprites and source OGG tracks are from Kenney's *Space Shooter Remastered (1.0)* and are CC0 1.0; converted BMP/WAV runtime files are derivatives of those assets. See [`showcase-src/README.md`](showcase-src/README.md) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). SDL3 is an external Git submodule and remains under its upstream zlib license; its source is not vendored, and its `LICENSE.txt` remains authoritative.

## References

- [SDL3 DOS README](https://wiki.libsdl.org/SDL3/README-dos)
- [SDL3 CMake README](https://wiki.libsdl.org/SDL3/README-cmake)
- [SDL licensing](https://www.libsdl.org/license.php)
- [DOSBox-X capture menu](https://dosbox-x.com/wiki/DOSBox%E2%80%90X%E2%80%99s-Drop%E2%80%90Down-Menus)
- [build-gcc DJGPP cross-compiler](https://github.com/jwt27/build-gcc)
