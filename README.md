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

## Screenshots

| Demo | Screenshot |
|---|---|
| `HELLO.EXE` | ![HELLO.EXE running in DOSBox-X](docs/hello.png) |
| `MISTIFY.EXE` | ![MISTIFY.EXE running in DOSBox-X](docs/mistify.png) |
| `SBTEST.EXE` | ![SBTEST.EXE diagnostics in DOSBox-X](docs/sbtest.png) |
| `MODPLAY.EXE` | ![MODPLAY.EXE playing a module in DOSBox-X](docs/modplay.png) |

The graphics examples request 320×200 indexed-color surfaces where supported. Mistify renders with a 256-color palette. All four programs use SDL software surfaces; the DOS backend uses VGA/VESA and does not require OpenGL or GPU acceleration. In-program text and command-line help are in English.

## Source organization

Each C file is organized into visible sections for configuration, state, initialization, input, the main loop, and cleanup. Resources are released safely after partial initialization failures; there are no `goto` statements.

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

The toolchain file is named `i586-pc-msdosdjgpp.cmake`; the compiler is explicitly set to `i386-pc-msdosdjgpp-gcc`. CMake creates `hello.exe`, `mistify.exe`, `sbtest.exe`, and `modplay.exe` in the selected build directory. If changing compiler or toolchain after configuration, use a fresh build directory so CMake does not reuse the old cache.

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

The original project files are licensed under the MIT License; see [`LICENSE`](LICENSE). SDL3 is referenced through a Git submodule and remains under its upstream zlib license; the parent repository stores only the submodule pointer, not SDL's source. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and the submodule's `LICENSE.txt` for details.

## References

- [SDL3 DOS README](https://wiki.libsdl.org/SDL3/README-dos)
- [SDL3 CMake README](https://wiki.libsdl.org/SDL3/README-cmake)
- [SDL licensing](https://www.libsdl.org/license.php)
- [DOSBox-X capture menu](https://dosbox-x.com/wiki/DOSBox%E2%80%90X%E2%80%99s-Drop%E2%80%90Down-Menus)
- [build-gcc DJGPP cross-compiler](https://github.com/jwt27/build-gcc)
