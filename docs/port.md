# The cross-platform port

A native GTA2 on SDL3 and the OpenGL renderer (`Renderers/OpenGL`), built with clang from the decomp in
`Source/`, for the plan in [issue #840](https://github.com/CriminalRETeam/gta2_re/issues/840). The game's code runs
unchanged: every Windows and DirectX API it uses is replaced underneath it, in `Port/`, and the few changes to
`Source/` are under `#if defined(GTA2_PORT)`, so the VC6 build and its matching are not affected.

The port is meant to be redone from a later, more complete decomp; "Redoing it on a later decomp" says how.

| Build | State |
|---|---|
| macOS, native arm64 (64-bit) | plays: frontend, the three cities, attract demos |
| Linux x86_64 | plays (Debian trixie; headless, and in a window with Mesa) |
| Linux i686 (x87 floats) | builds and links, not run |
| Windows, 32-bit MinGW-w64 (tested under wine) | plays; the frontend is bit-identical to `10.5.exe`'s, and the first attract demo matches it field by field (see Validation) |

Not done: movies (Bink is stubbed), the 3D sound provider (the game uses its 2D path), the mouse. The original's
DirectPlay network game is left out of every build; the port has its own, `Port/net` (docs/netcode.md).

## Building

CMake, with clang, from the repository root. One preset per target (`Port/CMakePresets.json`); the outputs go to
`build_port/<target>/` (git-ignored):

```bash
cmake --preset mac -S Port && cmake --build build_port/mac && ctest --test-dir build_port/mac
```

| Preset | Output | Needs |
|---|---|---|
| `mac` | `build_port/mac/gta2_port` | Xcode's clang, `brew install sdl3 ninja` |
| `mac-asan` | `build_port/mac_asan/gta2_port`, with AddressSanitizer | as `mac` |
| `net-tests` | the netcode's unit tests only, with UBSan, in `build_port/net_tests` | a host clang (no SDL3) |
| `linux64` | `build_port/linux64/gta2_port` (ELF x86-64) | `brew install lld`, a Debian sysroot in `build_port/linux64_sysroot` |
| `linux32` | `build_port/linux/gta2_port` (ELF i386, x87 floats) | a Debian i386 sysroot in `build_port/linux_sysroot` |
| `windows` | `build_port/windows/gta2_port.exe`, and in `gl/` the renderer DLLs and `nullvideo.dll` for `10.5.exe` | `brew install mingw-w64` (or `g++-mingw-w64-i686`), SDL3's MinGW package in `build_port/sdl3` (its `i686-w64-mingw32` folder), the install's `binkw32.dll` in `build_port/dlls` |

Other places: `-DGTA2_LINUX_SYSROOT=`, `-DGTA2_SDL3_DIR=`, `-DGTA2_DLL_DIR=`. A second macOS build with other flags:
`cmake -S Port -B build_port/mac_x4 -G Ninja -DCMAKE_CXX_FLAGS=-DGTA2_POOL_SCALE=4`.

What it builds (`Port/CMakeLists.txt`):

- **The game**: the root `CMakeLists.txt`'s `gta2_lib_src` (the VC6 build's list) without the ImGui overlay and
  DirectPlay, plus `StdAfx.cpp`, the platform layer, `Port/validation` and the network game (`Port/net`, with ENet).
  `Port/main.cpp` replaces `test.cpp`.
- **The renderer**, linked in (`Renderers/OpenGL/sources.cmake`): the game's loaders take the program itself
  instead of `d3ddll.dll` and `dmavideo.dll` (`gbh_graphics.cpp`, `dma_video.cpp`), and find the `gbh_*` and `Vid_*`
  functions in it (exported with `--kill-at` on Windows, `-rdynamic` elsewhere).
- **The tests** (ctest): `Port/tests` (the ini parser, the mixer, the POSIX file and wide string layers), the
  netcode's (`Port/net/tests`) and the renderer's (`Renderers/OpenGL/tests`). The Windows build's run under wine.

Toolchains are in `Port/cmake/`: `mingw-clang.cmake` (clang compiles against the MinGW-w64 headers, the MinGW g++
links), `linux.cmake` (clang and lld against a sysroot), and `mingw-gcc.cmake` (the renderer DLLs on their own,
with g++; CI uses it).

### Linux sysroots

The x86_64 sysroot is a Debian trixie amd64 container's files (`docker build` with `libc6-dev libstdc++-14-dev
libgcc-14-dev libsdl3-dev libgl-dev libegl-dev`, then `docker export`): `usr/include`, `usr/lib/x86_64-linux-gnu`,
`usr/lib/gcc`, `usr/lib/linux` (the kernel headers' `asm` links point there), `usr/lib64`, `lib`, `lib64`. The
i386 one: Debian trixie's i386 packages unpacked without running anything (`libc6`, `libc6-dev`, `linux-libc-dev`,
`libgcc-14-dev`, `libgcc-s1`, `libstdc++-14-dev`, `libstdc++6`, `libcrypt-dev`, `libcrypt1`, `libsdl3-0`,
`libsdl3-dev`, `libgl-dev`, `libegl-dev`, `libglx-dev`; URLs from `dists/trixie/main/binary-i386/Packages.xz`,
`tar xf x.deb && tar xf data.tar.* -C sysroot`), plus a `lib -> usr/lib` symlink (merged /usr: Debian's
`libc.so` linker script names `/lib/...`).

The Linux program needs only glibc and `libSDL3.so.0`, which it looks for next to itself first (`$ORIGIN`); the
C++ runtime is linked in. A game folder needs `data/` and an existing `player/` (the game writes its save slots
there but doesn't make it).

### Compiler flags

The game's code is VC6-era C++ that matches the original, undefined behaviour included, so the flags make clang
behave like VC6 rather than the code being changed:

| Flag | Why |
|---|---|
| `-fshort-wchar` | the game's strings and `L""` literals are 2-byte |
| `-fwrapv` | signed overflow wraps, as the fixed-point and angle code expects |
| `-ftrivial-auto-var-init=zero` | uninitialised locals the decomp keeps to match (clang otherwise deleted a null check that depended on one) |
| `-fno-delete-null-pointer-checks`, `-fno-strict-aliasing` | as VC6 |
| `-fno-strict-return` | a function without a `return` on some path (C4716 is off for the match) returns garbage instead of running into the next function |
| `-mno-sse -mfpmath=387` (32-bit x86) | x87 floats under the control word the game sets (single precision, round down), as `10.5.exe` |
| `-fno-optimize-sibling-calls` (Windows) | no tail calls, so the state check's call trace sees every return |
| `-Wno-address-of-temporary` | `f(&temp())` is valid (the temporary lives until the call returns); clang refuses it by default |
| `-DNDEBUG` | the original is a release build; one `assert` reads a variable before it is set |
| `-D_USE_32BIT_TIME_T` | VC6's `time_t`; MinGW's `_findfirst` otherwise takes a struct of another layout |
| `-fsjlj-exceptions` (Windows) | Homebrew's i686 MinGW uses SJLJ exceptions |
| `-fms-extensions` (not Windows) | `__declspec`, `__forceinline`, `__int64` |
| `-include Port/compat/posix_prefix.h` (not Windows) | the Windows names, before anything else |

## Running

```bash
GTA2_DIR=~/Games/GTA2 Scripts/port/run/run_mac.sh [map]       # build_port/mac/gta2_port in build_port/mac/game
GTA2_DIR=~/Games/GTA2 Scripts/port/run/run_wine.sh [exe] [map] # build_port/windows/gta2_port.exe in build_port/game
```

Both copy the install to their game folder once, write its `gta2.ini` (the install's settings from
`gta2_install.reg`, via `reg2ini.py`, then a 640x480 window, no movies, every area playable) and run there. `map`
(for example `wil`) boots straight into `data/wil.gmp`. `run_wine.sh` runs in its own wine prefix
(`build_port/wineprefix`); with another exe (`10.5.exe`) it sets the registry instead and takes the renderer DLLs
from `RENDERER=<folder>` (`build_port/windows/gl`).

The game keeps its settings in `gta2.ini` in its folder (`Port/registry_ini.cpp`): the registry's keys as sections
(`[screen]`, `[control]`, ...) with `dword:`, `str:` and `hex:` values. Debug settings (`[debug]`) are on when the
value exists, whatever it holds (`Get_Debug_Setting_586E20`). Without settings the game starts in full screen.

To look at a running game: `touch capture_now` in its folder writes the next frame to `capture.bmp` (the renderer's
`dev/capture.cpp`). Fatal errors go to `test/error.log` in the game folder.

## Code layout

`Port/` (the runtime, linked into the game):

| File | What it replaces |
|---|---|
| `main.cpp` | `test.cpp`: SDL's `main`, straight into `WinMain_5E53F0` |
| `sdl_win32.{hpp,cpp}` | the dozen user32 calls of `winmain.cpp` on one SDL window; SDL events become the window messages `WindowProc_5E4EE0` handles (close, focus, minimise, Alt+Enter). `GTA2_HEADLESS`: a hidden window, no sound |
| `sdl_video.cpp` | `glvideo.dll`'s `Vid_*` functions, with an SDL GL context (`Renderers/OpenGL/platform/sdl_context.cpp`) |
| `sdl_dinput.cpp` | DirectInput: an `IDirectInputA` whose keyboard gives SDL key events as DIK events, buffered (`GetDeviceData`, the game) and as a state array (`GetDeviceState`, the frontend); the first SDL gamepad as the joystick |
| `sdl_miles.cpp` | Miles (`mss32.dll`): the 52 `AIL_*` functions `cSampleManager.cpp` calls, as a software mixer in an SDL3 audio callback, WAV streams via `SDL_LoadWAV` |
| `registry_ini.{hpp,cpp}` | the registry, on `gta2.ini` |
| `rand/` | `rand`/`srand`: VC6's generator, so replays play the same everywhere (`rand.cpp`); the network game's owned draws and the sound's and particles' own streams (`owner.hpp`, `sound.hpp`, `particle.cpp`) |
| `port_globals.{hpp,cpp}` | the registry of the game's globals with their `10.5.exe` addresses (`DEFINE_GLOBAL` registers each), for the state log |
| `ptr_table.cpp` | 64-bit builds: the 32-bit handles of `PTR32` (`Source/ptr32.hpp`) |
| `stubs.cpp` | game functions the port leaves out: the ImGui overlay; on non-Windows builds the DirectX and version checks of `winmain.cpp` |
| `port_hooks.{hpp,cpp}` | what `Source/` calls or reads in the port (see "`Source/` changes") |
| `posix_*.cpp`, `compat/` | non-Windows builds, see below |
| `net/` | DirectPlay (`NetPlay.cpp`, `NetUi.cpp`): the port's network game, docs/netcode.md. Without one, `net/engine/netplay.cpp` answers the rest of the game as a single player |

`Port/compat/` has headers under the Windows names (`windows.h`, `objbase.h`, `mmsystem.h`, `dinput.h`, `ddraw.h`,
`dplay.h`, `io.h`, ...) with types, constants and declarations only; COM interfaces are C++ classes with virtual
methods, the same layout as COM on 32-bit x86, and `DWORD` is `unsigned int`. `compat/posix_prefix.h` is
force-included first in every game file. The POSIX layer comes in pairs:

| Header | Implementation | |
|---|---|---|
| `compat/posix_files.h` | `posix_files.cpp` | `fopen`, `_findfirst` and friends, `_stat`, `_getcwd`: backslashes become slashes, and every path component is matched case-insensitively (the game asks for `data\audio\vocals\...`, the folder is `Vocals`) |
| `compat/posix_wchar.h` | `posix_wchar.cpp` | the 9 wide string functions the game calls, for 2-byte `wchar_t` and with Microsoft's semantics (`swprintf(buf, fmt, ...)` without a size, `%s` wide) |
| `compat/posix_win32.h` | `posix_win32.cpp` | the other Win32 calls: `LoadLibrary`/`GetProcAddress` as `dlopen(NULL)`/`dlsym`, time (`GTA2_SPEED`), mutexes (no single-instance check), the CD drive type (fixed), the computer name, the keyboard layout (US) |

`Port/validation/` (linked in, off unless their environment variables are set; the platform layer calls them
through `validation.hpp` only):

| File | |
|---|---|
| `state_log.cpp` | the per-frame state log (`GTA2_STATELOG*`), see Validation |
| `fast_clock.cpp` | `GTA2_SPEED` on Windows, through the program's import table (also inside `10.5.exe`, in `nullvideo.dll`) |
| `nullvideo.cpp` | `nullvideo.dll`: a video DLL for `10.5.exe` that draws nothing and runs the state log |
| `scripted_input.cpp` | `GTA2_PORT_KEYS`, `GTA2_PORT_PAD`: input for tests without a person |

`Port/tests/`: the tests (ctest). `Port/cmake/`: the toolchains. `Port/net/`: the network game (docs/netcode.md,
"Code layout"). `Scripts/port/`: `run/` (the run scripts), `statecheck/` (the state check and its tools), `net/` (the
network check and its tools), `bots/` (the bots' trainer), `clang_check.sh` (the compile check of a decomp snapshot). The
renderer has its own layout (`Renderers/OpenGL/README.md`): `core/` the renderer, `platform/` the GL contexts,
`dll/` the Windows DLLs, `dev/` its development aids, `tools/` the reference tools, `tests/`.

### `Source/` changes

All under `GTA2_PORT`, VC6-neutral:

| Where | What |
|---|---|
| `Function.hpp` | `DEFINE_GLOBAL` registers the global (`Port/port_globals.hpp`) |
| `gbh_graphics.cpp`, `dma_video.cpp` | the renderer's functions from the program itself (`GetModuleHandle(NULL)`) |
| `input.cpp`, `InputRecorder_67F8B0.cpp` | `Port_DirectInputCreateA` (`Port/port_hooks.hpp`) |
| `winmain.cpp` | the window on SDL3 (`Port/sdl_win32.hpp`); its Windows-only functions are `Port/stubs.cpp`'s elsewhere |
| `registry.cpp`, `Frontend.cpp`, `NetUi.cpp`, `cSampleManager.cpp`, `winmain.cpp` | the registry on `gta2.ini` (`Port/registry_ini.hpp`) |
| `Frontend.cpp` | the attract idle time from `gPortAttractIdleMs` (`GTA2_PORT_ATTRACT_MS`) |
| `rng.cpp`, `sound_obj.cpp` | VC6's `rand` (`Port/rand/rand.hpp`) |
| `3rdParty/mss.h` | Miles' declarations for the port's mixer |
| `error.cpp` | the logs through C stdio (they only wrote through VC6's iostreams), and `gFile_67C530` is `gErrorLog_67C530`, as in `10.5.exe` |
| `crt_stubs.cpp` | `malloc` zeroes, like `10.5.exe`'s fresh heap pages, so the state log can compare pools |
| `char.cpp` | `CanReachTile_550090`'s `bRes_1`, read before it is set: VC6 keeps it where `bRes_2` was just set true |
| `fix16.cpp` | tan(90°) and tan(270°) take `10.5.exe`'s values (out of `s32` range, where VC6 keeps the low bits of an x87 value) |
| `types.hpp`, `ptr32.hpp`, `file.hpp`, `Light.cpp`, `TextTable.cpp`, `gtx_0x106C.*`, `map_0x370.*`, `Function.hpp` | 64-bit builds, see below |
| `Hud.cpp` | the chat line types the character the system's keyboard layout gives (`Port_TypedChar`) |
| `Game.cpp`, `winmain.cpp`, `Frontend.cpp`, `Car.cpp`, `Char_Pool.*`, `Ped.cpp`, `map_0x370.cpp`, `Pool.hpp`, `sound_obj.*` | the network game's hooks, docs/netcode.md "The game's code" |

## 64-bit builds

The macOS and Linux x86_64 builds are 64-bit. Issue #840 planned ARM64 with 32-bit pointers (M4), but macOS has no
such ABI: the kernel refuses a `__PAGEZERO` below 4 GB on arm64, and `mmap` won't hand out low addresses. Changes,
all VC6-neutral (32-bit builds keep their types and code):

| Problem | Fix |
|---|---|
| Structure size asserts (pointers grow) | off in 64-bit port builds, also GTA2Hax's `STATIC_ASSERT`. `-DGTA2_SIZE_SCAN` turns them on for a scan: 112 structures change, nearly all runtime objects; `SaveData` doesn't |
| Integers that hold pointers | `uptr`/`sptr` (`types.hpp`): u32/s32 on 32-bit, pointer-sized on 64-bit. Clang lists them: `-Wpointer-to-int-cast -Wint-to-pointer-cast -Wvoid-pointer-to-int-cast` |
| Script data keeps object pointers in 4-byte slots | `PTR32(T)` (`ptr32.hpp`): a 32-bit handle into a table (`Port/ptr_table.cpp`) on 64-bit, `T*` elsewhere |
| File records with 4-byte offsets that become pointers | text keys and the sprite index are copied into larger entries on load |
| Tables sized as `4 * n` | the map's zone list, the GTX delta container and deltas (`GTX_DELTAS_SIZE`), the light grid |
| `u16** field_40008_pColumn` holds 32-bit column words | `MapColumnWord`, a **macro** on 32-bit: as a typedef, the new name moved one instruction in `Particle_4C::UpdateCollisionBurst_state_31_34_53BAC0` |
| `size_t` is no longer `u32` | overloads for the `File`/`Map` calls that pass the other one |
| C fallback of an inline asm block stepped by a `u32` | a signed step |
| Static initialisation order | the settings file's sections are built on first use |

Floats are IEEE single precision there instead of x87, so replays don't match `10.5.exe` bit for bit (playing is
fine). Save games are interchangeable with the 32-bit builds only for the structures without pointers.

## Validation: the state check

GTA2 records replays of its inputs and random seeds; attract mode plays `data\attract\attr1.rep` and the others.
Played by the port and by `10.5.exe`, they must give the same game state. `Scripts/port/statecheck/state_check.py`
runs the attract replay headless in both and compares the whole game state after every frame, in about a minute:

```bash
cmake --preset windows -S Port && cmake --build build_port/windows
GTA2_DIR=~/Games/GTA2 python3 Scripts/port/statecheck/state_check.py 1100   # up to game frame 1100
```

```
compared game frames 1-174 (174 frames, 52679 chunks, 508 noisy)
game frame 174: 1 chunks became different: gNumberWalkingCopsSpawned_6787CD+0x0
game frame 186 *gChar_B4_Pool_6FDB44: field_0_pool.field_4_pool[13].field_98_velocity_vector.x.mValue (+0x9c0 10.5 000001ec port 00000400); ...
```

How it works:

- **The state**: every global the decomp defines, registered with its `10.5.exe` address and size
  (`Port/port_globals.hpp`), and for a pointer to a complete type what it points to (so a pool global covers the
  pool): about 1,800 entries and 3.3 MB, cut into 64-byte chunks.
- **The log** (`Port/validation/state_log.cpp`), after every flip: `F <flip> <game frame>`, then a hash of each
  chunk that changed. The port writes its list of entries (`.globals`); `nullvideo.dll`, loaded by `10.5.exe` as
  its video DLL, reads the list and hashes the same entries at the original addresses.
- **Headless and fast**: `GTA2_HEADLESS` (no GL context, SDL's dummy drivers) and `GTA2_SPEED`. A patched copy of
  `10.5.exe` (`patch_attract.py`, 2 bytes in `Frontend::Run_4AEDB0`) and `GTA2_PORT_ATTRACT_MS` shorten the wait
  for attract mode in both.
- **Lockstep frames**: `RunGameFrame_4DA850` skips draws when it runs late, and drawing fills caches, so two runs
  of `10.5.exe` differed. `GTA2_LOCKSTEP` (`max_frame_rate = 0`, `min_frame_rate = 0`) alternates one logic frame
  and one draw whatever the clock says; then `10.5.exe` matches itself in everything but sound.
- **The same renderer code**: `10.5.exe` uses `build_port/windows/gl`'s DLLs, built with the port's clang (MinGW's
  gcc rounds some vertices differently).
- **No junk bytes**: the port's allocations are zeroed like `10.5.exe`'s fresh heap, and padding is left out of
  the hashes (clang writes junk into padding). `layout.py` reads the padding and the field names from the port's
  DWARF (`llvm-dwarfdump`). Pointer fields are hashed as what they point into (entry, offset).
- **Onset, not difference**: `state_diff.py` reports the first chunk that was identical and became different,
  leaving out chunks that never matched and chunks that differ between two `10.5.exe` runs (the noise run).
  Sound and render scratch are ignored by default (`IGNORE=<regex>`).
- **Fields**: for the onset chunks both run again with `GTA2_STATELOG_DUMP` and each differing field is printed
  by name. Other entries: `DUMP='170-175:*gPedPool_6787B8,gGame_0x40_67E008' NO_FIELDS=1 state_check.py 176`.
- **Calls**: `TRACE=<a>-<b>:<function>,..` traces calls instead: an int3 at each function's entry and return
  address (`10.5.exe`'s from `og_function_data_v105.csv`, the port's from its symbols) logs `this`, the arguments
  and the return value, and the first call or return that differs is printed with context.
  `TRACE_THIS=<n>` adds the first n bytes of `*this`. `Class::*` traces a whole class.

**Result**: the port matches `10.5.exe` for the whole first attract demo (4,100 game frames) and the second
(4,711), with these left out:

```bash
IGNORE='gTileVerts_|gSound_obj_66F680|gSampManager_6FFF00|gParticle_4C_Pool|gSpritePool_703818|gSpriteShapePool|gGridSpriteLink_Pool|gGridCell_Pool|gSpriteTreeNodePool|gSpriteTreeStack|gParticlePos|gSpriteGrid_|gpGBH_Globals|gLight_1D4CC|gCarInfo_48_6FE258|gCarInfo_2C_6FE0E4|gMap_0x370_6F6268\+0x340' \
  GTA2_DIR=~/Games/GTA2 python3 Scripts/port/statecheck/state_check.py 4100
```

What is left out, and why it can't match:

- Debris particles from frame 308: `Car::HandleCarHitByObject_43F130` passes an `impact_vec` it never sets; in
  `10.5.exe` that stack slot holds a heap address. The particles, the sprite grids and the render counters differ
  from then on.
- `gCarInfo_48_6FE258` (the car being simulated) and the map's zone-search cursor: scratch values that end
  elsewhere with the same calls and results.
- Demo 3, from frame 968, the HUD arrows: `10.5.exe` copies an arrow target from an uninitialised stack temporary.

Open: across runs the later demos aren't exactly reproducible on `10.5.exe`'s side (the port matches itself
through demos 2 and 3).

The decomp bugs the check found are fixed on `master` or the `decomp-fixes` branch: false matches the verifier
can't see (wrong case labels in jump tables, swapped constants; `Scripts/bin_comp/check_match_refs.py` now checks
the globals, callees and switch targets of every `MATCH_FUNC` against `10.5.exe`). The others are port fixes:
`CanReachTile_550090`'s unset `bRes_1`, the x87 and `rand` differences, the missing returns and asserts above.

winedbg: attaching to a running 32-bit process faults under macOS wine's wow64 mode, so start the exe
under the debugger (`WINE_LAUNCHER=winedbg Scripts/port/run/run_wine.sh`); break on `RunGameFrame_4DA850` with a
condition on the frame counter (`*(int*)(*(int*)0x67ab34)`) rather than `cont N`.

## Environment variables

The game's (all builds):

| Variable | Effect |
|---|---|
| `GTA2_HEADLESS=1` | no window content, no GL context, no sound; the game and the gbh layer still run |
| `GTA2_SPEED=<k>` | the game's clock k times faster |
| `GTA2_STATELOG=<file>` and `GTA2_STATELOG_*` | the state log and its options (`Port/validation/state_log.cpp` lists them) |
| `GTA2_PORT_KEYS=<ms>:<dik>:<1\|0>,...` | key events at those times after the start |
| `GTA2_PORT_PAD=<ms>:<offset>:<value>,...` | pad events (and the game sees a pad) |
| `GTA2_PORT_ATTRACT_MS=<ms>` | the idle time before attract mode (60000) |
| `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=<file>` | the mix written to a file (float stereo, 44.1 kHz) |
| `GTA2GL_*` | the renderer's: capture, command log, statistics, enhancements (`Renderers/OpenGL/README.md`) |

The run scripts': `GTA2_DIR` (the install), `GTA2_DEBUG="skip_frontend log_random ..."` (debug settings),
`GTA2_LOCKSTEP=1`, `GTA2_INI_EXTRA='[control]\n0=dword:32994'` (more settings), `GTA2_BIN` (`run_mac.sh`: another
build), `RENDERER`, `GTA2_VIDEO_DLL`, `WINE_LAUNCHER` (`run_wine.sh`).

## Redoing it on a later decomp

1. Branch from the current `master`.
2. `Scripts/port/clang_check.sh` compiles every game source with clang for `i686-w64-mingw32`, syntax only,
   against the MinGW-w64 headers (about 4 s), and lists the failing files and every distinct error (logs in
   `build_clang/`). Fix in the order below, since earlier kinds hide later ones (clang stops a file at its first
   missing include, so the error count can go **up** after a fix).
3. After every few fixes, check the VC6 build as `CLAUDE.md` describes (`build.py`, `compare_builds.py` against a
   baseline saved on `master`); expect no changed function. Commit per kind of fix.
4. Then the port: `cmake --preset windows -S Port`, `run_wine.sh` (once with no map for the frontend, once with
   `wil`), and compare frontend captures with `10.5.exe`'s; they should be identical. Then the state check. A new
   crash: `i686-w64-mingw32-addr2line -f -C -e build_port/windows/gta2_port.exe <addr>`, and suspect undefined
   behaviour VC6 tolerated before suspecting the decomp.

The kinds of error the first snapshot had, and their fixes (all on `master` now):

1. **Include paths**: backslashes (`"..\Common\Types.hpp"` in GTA2Hax) and letter case (`"Fix16.hpp"` for
   `fix16.hpp`). macOS doesn't notice case; find them with
   `ls Source > /tmp/names; grep -ho '#include "[^"/]*"' Source/*.?pp | sed 's/#include "//; s/"$//' | sort -u | while read i; do [ -e "Source/$i" ] && ! grep -qx "$i" /tmp/names && echo "$i"; done`.
2. **`#if defined(__clang__) || (_MSC_VER <= 1200)`** selects the VC6 path on any clang (an undefined `_MSC_VER` is
   0): `#if defined(_MSC_VER) && _MSC_VER <= 1200`.
3. **`Function.hpp`'s non-Microsoft branch**: a missing `DEFINE_GLOBAL_ARRAY_INIT`, and `#define __stdcall` (clang
   knows `__stdcall`).
4. **The placeholder `std::vector`/`std::set`** in `gbh_graphics.hpp` (a matching device: GTA2Hax's real ones
   change VC6's EH frames) collide with a real STL: VC6 only.
5. **Temporaries bound to non-const references** (by far the most errors, all `Fix16`, `Ang16` and `Fix16_Point`):
   make the input parameters `const&`, and then the read-only members they call `const`. This changed no VC6 code,
   except where a member gained a free twin of the same signature (`Ang16::operator-`): VC6 then rejects the calls
   as ambiguous (C2593), so it stays non-const. clang's `-fsyntax-only` misses some uses VC6 rejects (C2678):
   always run the VC6 build too.
6. **Smaller ones**: extra qualification on member declarations; `case 0x8xxxxxxx` narrowed to `HRESULT` (cast the
   case value, not the switch, which would change VC6's compare); `(char)(size_t)` for a `%c` from a pointer;
   a `static inline` definition after an `extern inline` declaration; `throw()` on a declaration but not the
   definition; inline asm (plain C++ under `#else`, the asm kept for MSVC); `throw(std::bad_alloc)` (VC6 only); a
   dangling `&pCar->GetCarLinearSpeed_43A240()` kept after the statement to match VC6 (other compilers copy the
   value).
7. **DirectPlay** (`<VC98/Include/DPLAY.H>`): MinGW-w64 ships `dplay.h`, `dplobby.h` and `dinput.h`.

When the decomp has grown, expect new instances of 5: every new function taking `Fix16&` and every new caller
passing `a - b` to one.

## Platform notes

**Wine on macOS** (Homebrew `wine-stable` 9, wow64 mode; `10.5.exe` behaves the same):

- A 32-bit program reads the 32-bit registry view: `wine reg add ... /reg:32`.
- DMA's own video DLL can't run: windowed mode needs a 16-bit desktop and full screen a 16-bit display mode, and
  wine's mac driver offers neither (`Fatal Error 3003 in video.cpp(1359)` in `test/error.log`). After that failure
  the game saves `start_mode = 1` (full screen) and fails on every later run; `run_wine.sh` resets it.
- The renderer's WGL context needs `WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB` there (the same rule as SDL3 on macOS).
- The `gta2.exe` in a typical install isn't the 10.5 exe the decomp targets: compare against
  `Scripts/bin_comp/10.5.exe`.
- Message box texts: `WINEDEBUG=+relay` with `HKCU\Software\Wine\Debug\RelayInclude` = `user32.MessageBoxA`; CRT
  calls the same way with `ucrtbase.fopen` (the MinGW build uses the UCRT).

**The VC6 build on macOS** (`wine-stable`, through Rosetta):

- `vc6_setup.py` handles only Windows and Linux; the build works without it.
- wineserver can crash during full rebuilds with all cores: `python3 build.py --cores 4`. After a crash the link
  can leave a truncated `decomp_main.exe` (512 KiB) with an older `output.map`, and the verify prints `[0/3423]`:
  delete both and rebuild. `compare_builds.py` happily compares a stale exe, so check `build.py`'s exit code first.
- A full rebuild after a change to `fix16.hpp`, `ang16.hpp` or `Fix16_Point.hpp` takes over 10 minutes; the clang
  check takes 4 seconds. Iterate with clang, verify with VC6.

**Calling conventions**: clang honours `__stdcall` on Linux i386 (the callee pops its arguments), so everything the
game calls through a pointer has to use the same convention on both sides: the renderer's `gbh_*` are `__stdcall`
on every 32-bit x86 build; Miles, Bink and DirectInput share one header with the game.
