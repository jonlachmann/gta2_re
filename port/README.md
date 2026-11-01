# GTA2 port

Native builds of the decompiled game with clang, 32-bit. Plan: `docs/port_plan.md`.

| Build | Platform layer | Toolchain file |
|---|---|---|
| Linux (i686) | SDL3, OpenGL ES 3.0, built-in renderer, sound and input | `toolchain-linux32-clang.cmake` |
| Windows (MinGW, i686) | the original DirectX, Miles and Bink DLLs | `toolchain-mingw32-clang.cmake` |

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

## Layout

- `compat/`: the Win32 subset the game calls, on SDL3 and POSIX (window, registry, files,
  DirectInput, Miles), with Wine's headers for the declarations.
- `renderer/`: the OpenGL ES renderer behind the game's `gbh_*` and `Vid_*` tables.
- `include/`: copies of headers whose originals don't build outside MSVC.

## Unattended runs

| Variable | Effect |
|---|---|
| `GTA2_INPUT=<frame>:<key>[:<frames held>],...` | key presses at given frames (SDL key names: `Return`, `Up`, `Left Ctrl`, `Alt+Return`), or a virtual gamepad (`pad.a`, `pad.dpup`, `pad.lefty-`) |
| `GTA2_EXIT_AT=<frame>` | quit at that frame |
| `GTA2GL_CAPTURE=<dir>,<frame>,<a>-<b>,...` | write those frames as BMP |
| `GTA2_NO_SOUND=1` | no audio device |
| `GTA2_DEBUG_OUTPUT=1` | the game's debug strings on stderr |

Frames count presented frames. A run nobody sees, with the sound written to a file:

```bash
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/audio.raw \
GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=cap,600 \
gamescope --backend headless -W 640 -H 480 -- ./build_linux/gta2
```
