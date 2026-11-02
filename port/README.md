# GTA2 port

Native builds of the decompiled game with clang, 32-bit. Plan: `docs/port_plan.md`.

| Build | Platform layer | Toolchain file |
|---|---|---|
| Linux (i686) | SDL3, OpenGL ES 3.0, built-in renderer, sound and input | `toolchain-linux32-clang.cmake` |
| Windows (MinGW, i686) | the same as Linux; `-DGTA2_PORT_DIRECTX=ON` for DMA's DirectX, Miles and Bink DLLs | `toolchain-mingw32-clang.cmake` |

## Windows

Needs SDL3 for MinGW (`SDL3-devel-<version>-mingw` from the SDL releases). `SDL3.dll` is copied
next to `gta2.exe`; the exe needs no DirectX, Miles or Bink DLL.

```bash
cmake -S port -B build_port -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-mingw32-clang.cmake \
      -DSDL3_DIR=<SDL3-devel>/i686-w64-mingw32/lib/cmake/SDL3 -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build_port
```

## Linux

Needs clang, lld, ninja, cmake and 32-bit SDL3 (`lib32-sdl3` on Arch).

```bash
cmake -S port -B build_linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-linux32-clang.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build_linux
```

Run it from a copy of the game folder (it writes `gta2_registry.ini` and the saves there), or set
`GTA2_DIR` to that folder:

```bash
GTA2_DIR=~/games/gta2 ./build_linux/gta2
```

Settings live in `gta2_registry.ini` (the registry keys of the Windows game). Alt+Enter switches
between window and full screen. The first gamepad is used, or the first whose name contains
`GTA2_GAMEPAD`; bind it in the game's control options (stick and d-pad are the two axes).
The menus also take the mouse: pointing selects, a click enters or changes a value, the right
button goes back, the wheel changes a value; the cursor shows while the mouse is in use over a menu.
Movies are skipped. Renderer options are in `gta2gl.ini` (`renderer/gta2gl.ini.example`);
Scroll Lock toggles them.

## Frame rate

The game logic always runs in the original's steps of 33 ms (11 ms in fast forward), 30.3 per
second, on a monotonic clock; the original's `max_frame_rate` (off: the game as fast as the
machine) is not read. The SCREEN settings page chooses how often frames are drawn:

- FRAME LIMIT (`port_max_fps`): 30 ORIGINAL draws once after each step, as the original; 60, 120,
  144, 240 or UNLIMITED draw in between, with every sprite and the camera interpolated between the
  last two steps (`src/interpolation.cpp`). That shows the game one step (33 ms) late.
- VSYNC (`port_vsync`): adaptive where the driver has it.
- DISPLAY MODE (`port_fullscreen_width`, `_height`, `_centihz`): full screen at the desktop's
  mode (borderless) or one of the screen's own modes (resolution and refresh rate).

After a stall the game catches up at most 4 steps before the next frame, then drops the rest.

## 3D sound (OpenAL Soft)

The game's 3D sound option (`do_3d_sound` under `Sound` in `gta2_registry.ini`) uses
[OpenAL Soft](https://github.com/kcat/openal-soft) (LGPL), loaded at run time from next to the exe
(`libopenal.so.1`, `OpenAL32.dll`): positional sound with DirectSound3D's distance model and the
EAX environment the game picks, as EFX reverb. OpenAL renders into the SDL mix through its loopback
device. Without the library the game keeps its 2D sound. `GTA2_HRTF=1` renders binaurally for
headphones; `GTA2_NO_OPENAL=1` ignores the library.

Build it once per target (no audio backend needed), then pass its install prefix to the port:

```bash
OPTS="-DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_ENABLE_MODULES=OFF \
      -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
      -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_ALSA=OFF \
      -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_SDL3=OFF \
      -DALSOFT_BACKEND_WASAPI=OFF -DALSOFT_BACKEND_DSOUND=OFF -DALSOFT_BACKEND_WINMM=OFF -DALSOFT_BACKEND_WAVE=OFF \
      -DHAVE_WFUNCTION_EFFECTS=OFF -DCMAKE_BUILD_TYPE=Release"
# Linux
cmake -S openal-soft-1.25.2 -B al_linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-linux32-clang.cmake \
      -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_INSTALL_PREFIX=$PWD/openal_linux32 $OPTS
ninja -C al_linux install
# Windows: also -DALSOFT_EAX=OFF and a self-contained DLL
cmake -S openal-soft-1.25.2 -B al_mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-mingw32-clang.cmake \
      -DALSOFT_EAX=OFF "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -static -static-libgcc -static-libstdc++ -lpthread" \
      -DCMAKE_INSTALL_PREFIX=$PWD/openal_mingw32 $OPTS
ninja -C al_mingw install
```

Then add `-DGTA2_OPENAL_DIR=$PWD/openal_linux32` (or `openal_mingw32`) to the port's cmake line.
`-DHAVE_WFUNCTION_EFFECTS=OFF` works around recent clang rejecting OpenAL Soft 1.25.2's
`-Werror=function-effects`.

## Layout

- `compat/`: the Win32 subset the game calls, on SDL3 and POSIX (window, registry, files,
  DirectInput, Miles and its 3D provider on OpenAL Soft). Outside Windows, `compat/include` declares
  the Win32 and DirectX subset the game uses (Windows' 32-bit layouts and values); on Windows,
  MinGW's headers declare it and `win32_redirect.hpp` sends the game's calls to the same code.
- `renderer/`: the OpenGL ES renderer behind the game's `gbh_*` and `Vid_*` tables.
- `include/`: copies of headers whose originals don't build outside MSVC.

## Unattended runs

| Variable | Effect |
|---|---|
| `GTA2_INPUT=<frame>:<key>[:<frames held>],...` | key presses at given frames (SDL key names: `Return`, `Up`, `Left Ctrl`, `Alt+Return`), a virtual gamepad (`pad.a`, `pad.dpup`, `pad.lefty-`), or the mouse (`mouse.<x>x<y>` moves it to that window position, `mouse.left`, `mouse.right`, `mouse.wheelup`, `mouse.wheeldown`) |
| `GTA2_EXIT_AT=<frame>` | quit at that frame |
| `GTA2GL_CAPTURE=<dir>,<frame>,<a>-<b>,...` | write those frames as BMP (the first frame drawn at each); `GTA2GL_CAPTURE_FLIPS=1` counts presented frames instead |
| `GTA2_LOOP_STATS=1` | logic steps and frames per second on stderr, every 5 seconds |
| `GTA2_LOOP_TRACE=<file>` | the player's and the camera's position after each step (`S`) and as drawn (`D`) |
| `GTA2_NO_SOUND=1` | no audio device |
| `GTA2_NO_OPENAL=1`, `GTA2_HRTF=1` | no 3D sound; binaural 3D sound |
| `GTA2_LOG_FILE=<file>` | SDL's log, debug strings and message boxes written to that file |
| `GTA2_NO_MESSAGEBOX=1` | message boxes only logged, never shown |
| `GTA2_AUDIO_DRIVER`, `GTA2_AUDIO_FILE` | SDL's audio driver and disk output file (wine hides `SDL_*` variables from Windows programs) |
| `GTA2_DEBUG_OUTPUT=1` | the game's debug strings on stderr |

Frames count presented frames, except that while the game runs they count its logic steps, so a
script does the same at any frame rate. A run nobody sees, with the sound written to a file:

```bash
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/audio.raw \
GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=cap,600 \
gamescope --backend headless -W 640 -H 480 -- ./build_linux/gta2
```
