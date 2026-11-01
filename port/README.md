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

Needs clang, lld, ninja, cmake, Wine's headers (`/usr/include/wine/windows`, declarations only) and
32-bit SDL3 (`lib32-sdl3` on Arch).

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
Movies are skipped. Renderer options are in `gta2gl.ini` (`renderer/gta2gl.ini.example`);
Scroll Lock toggles them.

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
  DirectInput, Miles and its 3D provider on OpenAL Soft), with Wine's headers for the
  declarations on Linux. On Windows, `win32_redirect.hpp` sends the game's calls to the same code.
- `renderer/`: the OpenGL ES renderer behind the game's `gbh_*` and `Vid_*` tables.
- `include/`: copies of headers whose originals don't build outside MSVC.

## Unattended runs

| Variable | Effect |
|---|---|
| `GTA2_INPUT=<frame>:<key>[:<frames held>],...` | key presses at given frames (SDL key names: `Return`, `Up`, `Left Ctrl`, `Alt+Return`), or a virtual gamepad (`pad.a`, `pad.dpup`, `pad.lefty-`) |
| `GTA2_EXIT_AT=<frame>` | quit at that frame |
| `GTA2GL_CAPTURE=<dir>,<frame>,<a>-<b>,...` | write those frames as BMP |
| `GTA2_NO_SOUND=1` | no audio device |
| `GTA2_NO_OPENAL=1`, `GTA2_HRTF=1` | no 3D sound; binaural 3D sound |
| `GTA2_DEBUG_OUTPUT=1` | the game's debug strings on stderr |

Frames count presented frames. A run nobody sees, with the sound written to a file:

```bash
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/audio.raw \
GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=cap,600 \
gamescope --backend headless -W 640 -H 480 -- ./build_linux/gta2
```
