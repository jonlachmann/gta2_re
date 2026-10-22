# Cross-platform port: progress and findings

Notes for the plan in [issue #840](https://github.com/CriminalRETeam/gta2_re/issues/840) (a native
GTA2 on SDL3 + the OpenGL renderer, built from the frozen decomp in a fork). The issue holds the plan;
this file records what has been tried, what it found, and how to repeat it. The port is meant to be
redone from a later, more complete decomp, so the **Repeating it** section matters more than the
status snapshot.

## Status (2026-10-23)

| Milestone (issue #840) | State |
|---|---|
| M0. Compiles with clang | **All 109** game sources compile, and the whole game **links and runs**: a clang-built Windows exe (MinGW-w64), under wine on macOS with the OpenGL renderer. The frontend is bit-identical to `10.5.exe`'s; the first city plays at 30 fps. Network and tool removal, planned for M0, are deferred to M1 (see Remaining). |
| M1. Plays on Linux, Windows | In progress. Windows (tested under wine): renderer linked in, SDL3 window, GL context, event loop, keyboard, settings file and sound; frontend still bit-identical to `10.5.exe`. **Linux (32-bit x86): compiles and links, not run yet**; network removed there, 2-byte wide strings and Windows paths handled. Gamepad and trig tables done. Left: running on Linux. See "M1 so far" and "The Linux build". |
| M2. Replay test | Started: `Scripts/port/replay_check.py` compares the attract replay frame by frame with `10.5.exe` under wine. The port matches for the first 187 game frames of `attr1.rep` (of about 4300), then diverges; the cause is traced to a walking-cop spawn flag but not yet found. See "M2: the replay test". |
| M3-M6 | Not started. |

Branches (local, not pushed yet):

- `m0-clang` (from `master`): the M0 fixes below. Every commit keeps `[3423/3423] funcs OK`, and
  `compare_builds.py` reports no changed function. None of them needs to wait for the freeze; they can go
  to `master` as they are.
- `port` (from `m0-clang`): what can't go to `master`. So far only additions: `Scripts/port/build.sh`,
  `Scripts/port/run_wine.sh`, `Port/stubs.cpp`. See "The clang-built game under wine".
- `sdl3-mac` (from PR #837, `parigi-n/claude/opengl-renderer`): the renderer's SDL3 port, a macOS
  build, and the WGL fix wine on macOS needs. See Renderer.
- Submodule `3rdParty/GTA2Hax`, branch `portable-includes`: forward slashes in two `#include`s. It has to
  be merged in GTA2Hax before `m0-clang` can be pushed, since the submodule pointer refers to it.

## How the M0 state is measured

```bash
Scripts/port/clang_check.sh            # all of gta2_lib_src (CMakeLists.txt), about 4 s
Scripts/port/clang_check.sh Source/Ped.cpp
```

It runs `clang++ --target=i686-w64-mingw32 -fshort-wchar -std=c++17 -fsyntax-only` on every game
source, using the headers of the installed MinGW-w64 g++ (`brew install mingw-w64`, or
`g++-mingw-w64-i686` on Debian/Ubuntu). It prints the failing files and every distinct error; logs go to
`build_clang/` (git-ignored). Exit code 0 means M0 is done in terms of compiling.

Notes on the check:

- MinGW headers stand in for `<windows.h>` until the fork has its own compatibility header (issue
  #840: "Small compatibility headers"). Compiling against MinGW checks the C++, not the Win32 use.
- Syntax only: nothing is linked yet, so missing definitions and link-time duplicates are still unknown.
- clang stops a file at its first missing include. A file that fails on an include can hide any number
  of errors behind it, so the count of errors can go **up** after a fix. That happened twice here:
  9301 errors right after the include fixes, down to 177 after the STL fix.
- The tools (`cl_proxy`, `ExePatcher`, `HookLoader`, `WinMainProxy`) are not in `gta2_lib_src` and
  are not checked; the plan removes them.
- `-Wno-address-of-temporary` is on purpose, see below.

Every change to `Source/` was then checked with the VC6 build, as `CLAUDE.md` describes:
`compare_builds.py --save` on `master` first, then `build.py` + `compare_builds.py` after each step.

## Findings, by error kind

Error counts are from the first full run (916 errors, 102 of 113 files failing, before any fix) and
the runs in between; they show what to expect from a later snapshot of the decomp.

### 1. Include paths (fatal, hide everything else)

- **Backslashes**: `3rdParty/GTA2Hax/DmaVideo/DmaVideo.{hpp,cpp}` included `"..\Common\Types.hpp"`.
  Only Windows resolves that. Forward slashes work on VC6 too. (67 files failed on this alone, through
  `gbh_graphics.hpp`.)
- **Letter case**: `#include "Fix16.hpp"` (file is `fix16.hpp`, in `Car_BC.hpp`, `CarAI_78.hpp`,
  `ExplodingScore_100.hpp`) and `"miss2_0x11c.hpp"` (file is `miss2_0x11C.hpp`). macOS and Windows
  don't notice; Linux will. The check runs on macOS, so it does **not** catch this. Find them with:
  ```bash
  ls Source > /tmp/names; grep -ho '#include "[^"/]*"' Source/*.?pp | sed 's/#include "//; s/"$//' | sort -u |
    while read i; do [ -e "Source/$i" ] && ! grep -qx "$i" /tmp/names && echo "$i"; done
  ```
- `<FSTREAM.H>` and `<VC98/Include/DPLAY.H>` are VC6 SDK headers, see the next item and Remaining.

### 2. `#if defined(__clang__) || (_MSC_VER <= 1200)` picks the 1998 path

These guards were written so that clang-cl (or an IDE's clangd) sees the VC6 code. For a non-Microsoft
clang they select `<FSTREAM.H>`, the old iostreams, and the hand-rolled DirectInput path. Outside
`#if defined(_MSC_VER)` blocks they became `#if defined(_MSC_VER) && _MSC_VER <= 1200` (`error.hpp`,
`error.cpp`, `input.cpp`, `InputRecorder_67F8B0.cpp`). Note that an undefined `_MSC_VER` is 0 in `#if`, so
`(_MSC_VER <= 1200)` alone is true on every non-Microsoft compiler. The guards inside
`Function.hpp`'s `#if defined(_MSC_VER)` block only matter to clang-cl and were left alone.

### 3. `Function.hpp`'s non-Microsoft branch

It lacked `DEFINE_GLOBAL_ARRAY_INIT` (24 uses) and had `#define __stdcall` (empty), which turns every
`WINAPI` declaration in the SDK headers into a syntax error. clang knows `__stdcall` on every target and
ignores it where it has no meaning, so the define can just go.

### 4. The fake `std::vector`/`std::set` (`gbh_graphics.hpp`)

A matching device: GTA2Hax's `DmaVideo.hpp` includes `<set>`/`<vector>`, which pulls `<new>` into every TU
and changes VC6's EH frames (see commit 4876ffb1 and `docs/matching_quirks.md`). The header pre-sets the
include guards and declares placeholder templates. With a real STL these collide with libstdc++
(5520 "too many template arguments" errors). Now VC6 only.

### 5. Temporaries bound to non-const references (by far the biggest)

1250 errors, all of `Fix16`, `Ang16` and `Fix16_Point`, from **48 declarations** (about 30 functions).
MSVC allows binding a temporary to `T&`; standard C++ doesn't. Fix: make the **input** parameters
`const&`. `Scripts/port/constref.py` does the declarations and definitions in one go:

```bash
Scripts/port/constref.py Max_41E130,SquareRoot_436A70,PolarToCartesian_451730 ret1,ret2
```

The second argument lists output parameters to leave alone. In this pass those were `ret1`, `ret2`
(PolarToCartesian), `outX`, `outY` (ProjectOntoAxis), `pRotTransX`, `pRotTransY`
(RotateAndTranslatePoint), and `angle`, `angle_speed` (SmoothApproachAngle_405CE0).

The knock-on: a `const` parameter can only call `const` members. Three rounds of clang errors
("'this' argument ... is not marked const", "invalid operands ... 'const Fix16'") led to:

- `const` on the read-only members: `Ang16` comparisons, `operator*`, `operator+`, `Add_ool`, unary
  `operator-`; `Fix16::operator+`, both `Fix16::operator/`; `Fix16_Point` `operator+`, `operator*`,
  `operator/`, `Multiply_438FE0`, `Divide_442CB0`, `Sub_40AC80`, `MultBy_442C80`, `NormalizeSafe_442AD0`,
  `GetLength_41E260`, `GetLength_inline_442AD0`, `atan2_40F790`.
- `const` on the parameters these pass things to: `Fix16_Point_POD(const Fix16&, const Fix16&)`,
  `Ang16(const Fix16&, u8)`, `Fix16_Point::operator+=/-=`, `SetMovementVector_5224E0`,
  `NewObj3C_528130`, `ApplyForceAtPoint_55F800`/`ApplyForceAndIntegrate_55F7A0`/
  `ApplyForceWithTrailerRedirect_55F740` (`const Fix16_Point*`), `Abs_436A50`, `DotProductOOL_49E500`.

What we learned about VC6 here:

- **Adding `const` to parameters and to member functions changed no generated code**, including the
  functions whose comments say they depend on the inline budget (`SquareRoot`, `Max_41E130`, the
  `GetLength_*` family). The budget counts `const` *locals* (see the comment on `Fix16::SquareRoot`), but
  `const` on a parameter or on `this` cost nothing in this pass.
- **Exception: overload resolution.** Making the member `Ang16::operator-(const Ang16&)` const made it
  tie with the free `operator-(const Ang16&, const Ang16&)` (`ang16.hpp`), and VC6 rejected the calls
  as ambiguous (C2593). It stays non-const. Watch for any member that has a free twin.
- clang's `-fsyntax-only` and VC6 disagree in places: clang did not report the non-const operator uses
  in `ang16.cpp`/`fix16.cpp` that VC6 rejected (C2678) after the first round. Always run the VC6 build too.
- A `static inline` definition (`Camera.cpp`'s `Max_41E130`) after an `extern inline` declaration of it
  (`Camera.hpp`) is an error in clang; the unused header declaration was dropped.
- `throw()` on a declaration but not on the definition is an error in C++17 (`SquareRoot_436A70`). Adding
  `throw()` to the definition didn't change VC6's code.

When the decomp has grown, expect new instances: every new function taking `Fix16&` and every new caller
passing `a - b` to one. Fix them the same way: run the check, collect the "passing argument to parameter
here" notes, run `constref.py`, repeat until clean.

### 6. Smaller items

| Error | Where | Fix |
|---|---|---|
| extra qualification `Class::f` on a member declaration | `ang16.hpp`, `CarPhysics_B0.hpp` (2), `Frontend.hpp` | drop the `Class::` |
| case value `0x8xxxxxxx` cannot be narrowed to `HRESULT` | `FatalDXError_4A3CF0` in `error.cpp` | `case (HRESULT)0x8...:` — not `switch ((u32)hr)`, which would change the compare signedness and the switch layout VC6 needs (see the comment there) |
| cast from pointer to smaller type `char` | `error.cpp` (`%c` from a `va_arg` pointer) | `(char)(size_t)va_1` |
| taking the address of a temporary | 10 calls in `Car_BC.cpp`, `CarPhysics_B0.cpp`, `Explosion_30.cpp` | none: `f(&temp())` is valid (the temporary lives until the call returns), only clang refuses it by default. The port build needs `-Wno-address-of-temporary`. |
| taking the address of a temporary, **kept after the statement** | `sound_obj.cpp`, `pSpeed = &pCar->GetCarLinearSpeed_43A240()` | a real dangling read, done on purpose to match VC6. Now VC6 only; other compilers copy the value. |
| inline asm | `sprite_delta::Delta_5ABA00`, `Delta_5ABA40` (`gtx_0x106C.cpp`), `get_rdtsc_5BEE90` (`SpriteRenderer_1C.cpp`) | plain C++ under `#else`, asm kept for MSVC. The delta loops are line-by-line translations but **untested** (32-bit x86 can't run on the Apple Silicon machine used). The M2 replay test, or a quick test under wine against the asm, should check them. `get_rdtsc` uses `__builtin_readcyclecounter` (clang only; the profiler display is its only user). |
| dynamic exception specification `throw(std::bad_alloc)` | `crt_stubs.cpp` (`operator new`, `new[]`) | VC6 only, through a `THROW_BAD_ALLOC` macro |

### 7. The last five files

`NetPlay.cpp`, `Network_20324.cpp`, `Player.cpp`, `winmain.cpp` stopped at `NetPlay.hpp`'s
`<VC98/Include/DPLAY.H>`, `input.cpp` at an undeclared `Fn_DirectInputCreateA` (its `#if _MSC_VER > 1200`
excluded clang). MinGW-w64 still ships `dplay.h`, `dplobby.h` and `dinput.h` with import libraries, so
non-MSVC compilers now take those, and the declaration's guard became `!defined(_MSC_VER) || _MSC_VER >
1200`. Nothing was hiding behind them: all 109 files compile.

## Remaining for M0 (fork only, breaks matching)

The plan removes these in M0. Since the clang build is a Windows exe for now, they still work there, and
removing them fits better with M1, when the platform code moves to SDL3:

- DirectPlay and the lobby (`NetPlay.*`, `Network_20324.*`, the network paths in `winmain.cpp`). Single
  player still asks `gNetPlay_7071E8` for the player index, the player count and names, so removing it
  means a small single-player replacement for those few methods, not just deleting files.
- The matching tools (`build.py`, `Scripts/bin_comp`, reccmp, permuter, HookLoader, the `MATCH_FUNC`
  markers' naked asm). The port build simply doesn't compile them.

## The clang-built game under wine (branch `port`)

```bash
Scripts/port/build.sh          # -> build_port/gta2_port.exe; incremental, FORCE=1 rebuilds everything
GTA2_DIR=~/Documents/GTA2 RENDERER=<renderer>/Renderers/OpenGL/build \
  GTA2GL_CAPTURE=cap,100,300 Scripts/port/run_wine.sh "" wil    # "" = gta2_port.exe; wil = boot into city 1
```

`build.sh` compiles the same sources as `decomp_main` (`gta2_lib_src`, `test.cpp`, `StdAfx.cpp`,
`resources.rc`) plus `Port/*.cpp` with clang for `i686-w64-mingw32`, and links with the MinGW-w64 g++
(Homebrew's LLVM has no `lld`). It needs the GTA2 install's `mss32.dll` and `binkw32.dll` in
`build_port/dlls/` (or `GTA2_DLL_DIR`). `run_wine.sh` copies the install to `build_port/game/` and runs
there, in its own wine prefix (`build_port/wineprefix`), so neither the install nor the VC6 prefix changes.

What it took, in the order the problems appeared:

| Problem | Fix |
|---|---|
| GTA2Hax's ImGui copy defines its own `vsnprintf` under `#if _MSC_VER <= 1200` (true without MSVC). ImGui is a submodule *inside* the GTA2Hax submodule. | The port build leaves ImGui and `ImGuiDebug.cpp` out (the overlay draws through GTA2Hax's D3D9 renderer, which the port doesn't use); `Port/stubs.cpp` has an empty `ImGuiDebugDraw`. |
| `test.cpp`, `decomp_main`'s entry point, calls `EnableBoot2MapDebugOptions()` (in `ImGuiDebug.cpp`), which **turns off traffic lights and trains** for the standalone decomp build. | Empty in `Port/stubs.cpp`: the port plays the full game. Worth knowing when testing `decomp_main` itself. |
| GNU `windres`: syntax error at `VALUE "Translation", 0x0000 0x04B0`. | A comma, on `m0-clang` (VC6 output unchanged). |
| `_Unwind_Resume`, `__gxx_personality_v0` undefined. | Homebrew's i686 MinGW uses SJLJ exceptions (`__USING_SJLJ_EXCEPTIONS__`); clang needs `-fsjlj-exceptions`. |
| `_imp__AIL_startup@0` undefined. The real `mss32.dll`/`binkw32.dll` export decorated names (`_AIL_startup@0`). | `build.sh` generates a `.def` from each DLL's exports with `AIL_startup@0 == _AIL_startup@0` lines and builds import libraries with `dlltool`. The repo's `Source/3rdParty/mss.cpp`/`bink.cpp` stubs aren't used. |
| `GetFileVersionInfoA` undefined. | `-lversion`. |
| "Couldn't open .seq file in the data directory": `fopen("data\\")`, the name from `_findfirst` was empty. MinGW maps `_findfirst` to `_findfirst64` (64-bit `time_t`), whose struct layout differs from the `_finddata_t` the code declares. | `-D_USE_32BIT_TIME_T`, which also matches VC6's 32-bit `time_t` everywhere else. |
| Crash entering a city: read of address 0x14 in `Hud_Brief_704::FormatCurrentBrief_5D3470`. Its `num_chars` is returned uninitialised when there's no brief (VC6 returned garbage, and the decomp keeps it to match), so clang treated the null case as impossible and **deleted the `if (field_6F8_curr_briefs)` check**. | `-ftrivial-auto-var-init=zero`, plus `-fwrapv` (signed overflow wraps as on VC6; the fixed-point and angle code relies on it) and `-fno-delete-null-pointer-checks`. Fix the build flags, not the functions: the decomp is full of VC6-era undefined behaviour that matches the original. |

Results on macOS 26, Apple Silicon, wine 9.0 (Homebrew `wine-stable`):

- **Frontend: bit-identical** to `10.5.exe` with the same renderer, at frames 30, 100, 200 and 400
  (`cmp` of the captured BMPs).
- **City 1 (`wil`)**: plays at 30 fps, renders correctly. Not comparable frame by frame: two runs of
  `10.5.exe` itself differ there (time-seeded randomness, timing). That needs M2's replays.
- Untested so far: input (nobody pressed a key), sound output, cities 2 and 3, saving, the network game.

### Wine on macOS: running GTA2 at all

These are about the environment, not the port; `10.5.exe` behaves the same.

- Wine 9 on macOS runs 32-bit programs in "experimental wow64 mode", in a 64-bit prefix. **A 32-bit
  program reads the 32-bit registry view**: set GTA2's keys with `wine reg add ... /reg:32`, or the game
  doesn't see them.
- The game reads `HKLM\SOFTWARE\DMA Design Ltd\GTA2` (`registry.cpp`). The install's
  `gta2_install.reg` writes `HKEY_CURRENT_USER`; `run_wine.sh` imports nothing from it, it sets the few
  values that matter.
- **DMA's own video DLL can't run here**: windowed mode needs a 16-bit desktop and full screen a 16-bit
  display mode, and wine's mac driver offers neither. `test/error.log` then says `Fatal Error 3003 in
  video.cpp(1359)` (`VideomodeNotAvailable`). Use the OpenGL renderer (or capvideo + d3ddll, which fails
  the same way).
- After that failure the game saves `start_mode` = 1 (full screen) and fails the same way on every later
  run. `run_wine.sh` resets it.
- PR #837's `glvideo.dll` needed `WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB`: the mac driver only creates
  3.2+ contexts that are forward compatible (the same rule as SDL3 on macOS). Fixed on `sdl3-mac`.
- The `gta2.exe` in a typical install isn't the 10.5 exe the decomp targets (different hash, other
  registry handling). Compare against `Scripts/bin_comp/10.5.exe`.
- Finding out why it stopped without seeing the screen: the game's fatal errors go to `test/error.log` in
  the game folder; message box texts show up with `WINEDEBUG=+relay` and
  `HKCU\Software\Wine\Debug\RelayInclude` = `user32.MessageBoxA`; CRT calls the same way with
  `ucrtbase.fopen` and so on (the MinGW build uses the UCRT, not `msvcrt`); crash addresses resolve with
  `i686-w64-mingw32-addr2line -f -C -e build_port/gta2_port.exe <addr>`. To look at the game, use
  `GTA2GL_CAPTURE` rather than screen captures.

## M1 so far (branch `port`)

One pattern throughout: **keep the game's code, replace the library under it.** Each Windows API the
game uses is redirected, in the files that use it and only under `GTA2_PORT`, to a small
implementation on SDL3 in `Port/`. The game's functions run unchanged, which keeps the port's
behaviour tied to the decomp (and the replays of M2 meaningful). Each step was checked under wine with
`Scripts/port/run_wine.sh`: the frontend frames stay bit-identical to `10.5.exe`'s.

| Step | Files | How |
|---|---|---|
| Renderer linked in | `Source/gbh_graphics.cpp`, `Source/dma_video.cpp` (one guarded line each) | The renderer is compiled into the exe with its exports kept (`-Wl,--kill-at`); the loaders take `GetModuleHandle(NULL)` instead of `LoadLibrary`, so `GetProcAddress` finds the `gbh_*`/`Vid_*` functions in the exe. On Linux the same is `dlsym` on the program. |
| Entry point | `Port/main.cpp` | SDL's `main`, straight into `WinMain_5E53F0`. `test.cpp` isn't used. |
| Window, GL, event loop | `Port/sdl_win32.{hpp,cpp}`, included by `winmain.cpp` | The dozen user32 calls of `winmain.cpp` (`CreateWindowExA`, `PeekMessageA`, `DispatchMessageA`, `SetWindowPos`, `ShowCursor`, ...) act on one SDL window. SDL events become the window messages `WindowProc_5E4EE0` handles: close, focus, minimise, Alt+Enter. `GetDeviceCaps(BITSPIXEL)` says 16, so windowed mode is offered on any desktop. |
| `Vid_*` | `Port/sdl_video.cpp`, `Renderers/OpenGL/platform/sdl_context.cpp` | `glvideo.cpp`'s functions, with an SDL GL context on that window (`CreateGLContextForSDLWindow`) instead of WGL. |
| Keyboard | `Port/sdl_dinput.cpp`, `Source/input.cpp`, `Source/InputRecorder_67F8B0.cpp` | An `IDirectInputA` whose keyboard returns SDL key events as DIK events: buffered (`GetDeviceData`, the game) and as a state array (`GetDeviceState`, the frontend). No gamepad or mouse data yet. |
| Settings | `Port/registry_ini.{hpp,cpp}`, included by the 5 files that use the registry | `Reg*` on `gta2.ini`: `[screen]`, `[control]`, ... with `dword:`/`str:`/`hex` values. |
| Sound | `Port/miles_sdl.cpp`, `Source/3rdParty/mss.h` | The 52 `AIL_*` functions `cSampleManager.cpp` calls, as a software mixer in an SDL3 audio callback, plus WAV streams via `SDL_LoadWAV`. No 3D provider, so the game uses its 2D path. |

Tests: `Port/tests/*_test.cpp` (the ini parser, the mixer), built by `build.sh` into `build_port/*.exe`
and run under wine. Scripted input for checks without a person: `GTA2_PORT_KEYS=<ms>:<dik>:<1|0>,...`.
Sound without speakers: `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=sdlaudio.raw` writes the mix
(float stereo, 44.1 kHz) to the game folder.

Checks so far: the frontend is bit-identical at frames 30/100/200/400 after every step; Return on the
main menu opens the play menu; holding Up in city 1 walks the player into the church (its "Get lost, ya
bum!" brief appears); 20 s of city 1 give sound without clipping (not listened to yet).

Findings:

- **The issue's caution about faking Windows messages** ("would cost more") holds for the whole message
  API, but the game's own window needs about ten messages; translating SDL events into those was less
  code than rewriting `WinMain_5E53F0` and `WindowProc_5E4EE0`, and leaves them as decompiled.
- `GetDeviceCaps(BITSPIXEL)` decides whether the game offers windowed mode at all (16-bit desktops
  only, a DirectDraw limit).
- GTA2's debug settings are on when the value **exists**, whatever its value (`Get_Debug_Setting_586E20`):
  to turn `skip_frontend` off, delete it.
- Without settings, the game defaults to **full screen** (`start_mode` 1).
- The frontend reads the keyboard with `GetDeviceState` (immediate), the game with `GetDeviceData`
  (buffered, one event per frame; the PR's README explains why that lagged under wine with DMA's DLLs).
- Miles plays centred 2D sounds at full volume in both channels: an equal-power pan (+3 dB at the
  centre) clipped constantly.
- Apple clang's `cpuid.h` clashes with MinGW's `intrin.h` (`__cpuidex`), which SDL's headers include:
  `build.sh` presets `__CPUID_H`. And `-static` links `libwinpthread` statically, but then SDL's import
  library must be given by path (`libSDL3.dll.a`), as `-static` ignores `-lSDL3`.

### Trig tables and gamepad

- **Trig tables**: the sine, cosine and tangent tables (1440 entries each) are computed at start-up from
  the C library. Dumped from a running `10.5.exe` (`Scripts/port/trig_check.py`, winedbg): every entry is at
  least 1e-6 from a truncation boundary, far more than any difference between correct `sin`/`cos`/`tan`,
  except tan(90°) and tan(270°). Those are out of `s32` range, where VC6's conversion keeps the low 32
  bits of an x87-precision value; in C++ the conversion is undefined (and traps on some targets). Under
  `GTA2_PORT` those two entries take `10.5.exe`'s values. The port's tables are identical in all 4320.
- **Gamepad**: the first SDL gamepad, behind the same DirectInput replacement as the keyboard, in
  DirectInput's joystick layout (X/Y at offsets 0/4 in -1000..1000 with a 25% dead zone, button n at
  48 + n). Pad bindings come from the game's Controls menu (bit 15 of a `[control]` value), as in the
  original. `GTA2_PORT_PAD=<ms>:<offset>:<value>,...` scripts pad input for tests.

## M2: the replay test (branch `port`)

GTA2 records replays of its inputs and random seeds, and attract mode plays `data\attract\attr1.rep` and
the others after 60 s idle on the start menu. Played by two exes, they must give the same frames.

```bash
Scripts/port/build_gl_dlls_clang.sh                          # 10.5.exe renders with the port's renderer code
python3 Scripts/port/patch_attract.py Scripts/bin_comp/10.5.exe build_port/game/gta2_105_attract.exe 3000
GTA2_PORT_ATTRACT_MS=3000 FIRST=60 LAST=360 GTA2_DIR=~/Documents/GTA2 RENDERER=$PWD/build_port/gl_clang \
  python3 Scripts/port/replay_check.py $PWD/build_port/game/gta2_105_attract.exe $PWD/build_port/gta2_port.exe
```

What it takes to compare fairly:

- **The same settings.** The first comparisons failed on configuration, not code: the port rendered at
  960x720 (frames can't match the original's 640x480), and lacked the install's `do_blood` and
  `exploding_on = 0`, so kills showed score pop-ups instead of blood. `run_wine.sh` now gives both exes
  the install's settings (`reg2ini.py` turns `gta2_install.reg` into `gta2.ini`) and a 640x480 window.
- **The same renderer code.** PR #837's DLLs built with MinGW's gcc round some vertices differently from
  the port's clang build: single-pixel differences. `build_gl_dlls_clang.sh` builds them with clang.
- **A short wait.** Attract mode starts after 60 s of wall-clock idle; `GTA2_PORT_ATTRACT_MS` and a
  patched copy of `10.5.exe` (`patch_attract.py`, 2 bytes in `Frontend::Run_4AEDB0`) shorten it for both.
- `10.5.exe` against itself gives identical frames, so the replay is deterministic.

Fixes it found in the port:

- **x87, not SSE.** clang compiled the game's float code with SSE2, which ignores the x87 control word
  the game runs with (single precision, round down; `gbh_InitDLL`). `-mno-sse -mfpmath=387` for the game
  code; the frames then match for 87 more frames.
- **VC6's `rand`.** glibc's `rand` is another generator (`Port/port_rand.h`; Windows' UCRT happens to use
  the same LCG).
- **Logs.** `ErrorLog` only wrote through VC6's iostreams, so the port wrote no `test\error.log` or
  `warning.log`; and the decomp's `gFile_67C530` was a separate, never-opened object, while in `10.5.exe`
  it is `gErrorLog_67C530` (0x67C530). Both fixed under `GTA2_PORT`, so `log_random` works.

**The open divergence.** With `GTA2_DEBUG=log_random` both exes log every random draw: the first 10,106
are identical, the first difference is at game frame 187. Traced back with `snapshot.py` and winedbg
breakpoints (comparing memory at the same moment):

1. Frame 187: pedestrian 38 makes an extra draw in the port. Its position and offscreen counter are
   identical; the draws are in a different order because...
2. ...the original updates one more pedestrian (pool slot 13), spawned at the end of frame 186, whose
   AI runs first. In the port that pedestrian is freed right after spawning (`dead_9`, inactive), through
   `SpawnPedestrianAt_46E380`'s `delloc_ret_2` exit for walking cops, because...
3. ...`gNumberWalkingCopsSpawned_6787CD` is 1 in the port and 0 in `10.5.exe` at frame 186. Both
   deallocated the same walking cop at frame 173 (`Deallocate_45EB60` only decrements for `guard`, 28, not
   `walking_guard_29`, so not there), and the flag is otherwise only reset when `PedManager` is
   constructed. Next step: a write watchpoint on 0x6787CD in both (`watch *0x6787cd` in winedbg) to find
   what clears it in the original.

Not ruled out yet: decomp code whose meaning differs between VC6 and clang (a matching function can
still rely on VC6 behaviour, such as an evaluation order or undefined behaviour), and uninitialised heap
memory (the pools are allocated with `new`; tried and ruled out: uninitialised stack variables, with
`-ftrivial-auto-var-init=pattern` the divergence stays at the same draw).

Tools for this, all in `Scripts/port/`: `replay_check.py` (frames), `snapshot.py` (memory at a frame),
`trig_check.py` (the tables). winedbg notes: attaching to a running 32-bit process faults under macOS
wine's wow64 mode, so start the exe under the debugger (`WINE_LAUNCHER=winedbg Scripts/port/run_wine.sh`);
`cont N` is slow per hit, so break on `RunGameFrame_4DA850` with a condition on the frame counter
(`*(int*)(*(int*)0x67ab34)`) first; casts make dereferences work (`x /4x *(unsigned int*)0x676978`).

## The Linux build (branch `port`)

Built on the Mac, without running it (a Linux VM wasn't wanted yet): the non-Windows code is the same for
Linux and macOS, and the Windows build under wine keeps testing the shared SDL3 code at runtime.

```bash
brew install lld
Scripts/port/build_linux.sh      # -> build_port/linux/gta2_port, ELF i386, needs libSDL3.so.0
```

The target files (sysroot) are Debian trixie's i386 packages, unpacked without running anything: `libc6`,
`libc6-dev`, `linux-libc-dev`, `libgcc-14-dev`, `libgcc-s1`, `libstdc++-14-dev`, `libstdc++6`,
`libcrypt-dev`, `libcrypt1`, `libsdl3-0`, `libsdl3-dev`, `libgl-dev`, `libegl-dev`, `libglx-dev`
(URLs from `dists/trixie/main/binary-i386/Packages.xz`; `tar xf x.deb && tar xf data.tar.* -C sysroot`), plus
a `lib -> usr/lib` symlink (merged /usr).

How the game compiles without Windows: `Port/compat/` has headers under the Windows names
(`windows.h`, `objbase.h`, `mmsystem.h`, `dinput.h`, `ddraw.h`, `dplay.h`, `io.h`, ...), with types,
constants and declarations only, and `port_linux.h` is force-included first in every game file:

| Difference | Where | How |
|---|---|---|
| Windows types, COM | `compat/windows.h`, `objbase.h` | 32-bit definitions (`DWORD` is `unsigned int`, not `unsigned long`); interfaces are C++ classes with virtual methods, the same layout as COM on 32-bit x86. |
| Microsoft keywords | build flags | `-fms-extensions` (`__declspec`, `__forceinline`, `__int64`); `-Wl,--no-dependent-libraries` ignores `#pragma comment(lib, ...)`. |
| 2-byte `wchar_t` | `port_wchar.cpp` | `-fshort-wchar` keeps the game's strings and `L""` literals 2-byte, but glibc's wide functions take 4-byte ones: `swprintf` and the 8 other wide functions the game calls are our own, with Microsoft's semantics (`swprintf(buf, fmt, ...)` without a size; `%s` wide). |
| Windows paths | `posix_files.cpp` | `fopen`, `_findfirst` and friends, `_stat`, `_getcwd`: backslashes become slashes, and every path component is matched case-insensitively (the game asks for `data\audio\vocals\...`, the folder is `Vocals`). |
| Window, input, settings, sound | `sdl_win32.cpp`, `sdl_dinput.cpp`, `registry_ini.cpp`, `miles_sdl.cpp` | the same as on Windows. |
| The renderer's "DLLs" | `posix_win32.cpp` | `LoadLibrary`/`GetProcAddress` are `dlopen(NULL)`/`dlsym`; the program is linked with `-rdynamic`. |
| Network | `netplay_offline.cpp`, `compat/dplay.h` | `NetPlay.cpp` and `Network_20324.cpp` aren't built; a single-player `NetPlay` answers the rest of the game (no players, no messages). The network code only runs once a network game has started, which can't happen. |
| Windows-only checks | `port_winmain.cpp` | DirectX and DirectMusic versions, exe version info, the GTA2 Manager launch. |
| Movies | `Source/3rdParty/bink.cpp` | its stubs (no movies until M5). |
| Other Win32 calls | `posix_win32.cpp` | time, mutexes (no single-instance check), CD drive type (always fixed), computer name, keyboard layout (US). |

Findings:

- libstdc++'s `<cwchar>` and `<cstdio>` `#undef` the C names, so redirect macros defined before them are
  lost: `port_linux.h` includes them first and redirects after.
- `struct _stat` can't have an `st_mtime` member: it's a macro in both glibc and macOS.
- Debian's `libc.so` linker script names `/lib/...`; with merged /usr the sysroot needs `lib -> usr/lib`.
- **Calling conventions**: clang honours `__stdcall` on Linux i386 (the callee pops its arguments). The
  renderer declared its `gbh_*` functions `__stdcall` only on `_WIN32`, so the Linux build would have
  corrupted the stack on the first renderer call. Everything the game calls through a pointer has to use
  the same convention on both sides; Miles, Bink and DirectInput share one header with the game, so they do.
- The game compiles with all its `GTA2_ASSERT_SIZEOF` checks passing on Linux i386, as the issue predicted.

Not verified until it runs on Linux: anything at runtime. Expected trouble spots: paths the game builds
in ways `posix_files.cpp` doesn't cover, uninitialised-memory differences, and the frame pacing on the
first frames.

## Renderer on SDL3 and macOS (branch `sdl3-mac`)

PR #837's renderer has a portable `core/` and an SDL2 platform file meant for the native port. Moving
it to SDL3 and building it natively on macOS (Apple Silicon, SDL 3.4 from Homebrew):

- SDL2 → SDL3 changes that mattered: `SDL_CreateWindowFrom` is gone (a foreign HWND now goes through
  `SDL_CreateWindowWithProperties` and `SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER`),
  `SDL_GL_GetDrawableSize` → `SDL_GetWindowSizeInPixels`, `SDL_GL_DeleteContext` →
  `SDL_GL_DestroyContext`, init functions and event filters return `bool`,
  `SDL_HINT_VIDEO_FOREIGN_WINDOW_OPENGL` and `SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING` no longer exist.
- macOS only gives a core 3.2+ context with `SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG`.
- macOS has no `GL/glcorearb.h`. `<OpenGL/gl3.h>` declares the same 3.3 core API as prototypes, so
  `gl_funcs.hpp` takes the function pointer types from them with `decltype(&::glFoo)`.
- The SDL platform file installed an event filter that drops all events, which is right for the DLL (the
  game's own message loop owns the window) but makes an SDL-owned window impossible to close. The filter
  now applies only to the adopted native window.
- `make demo` builds `tools/sdl_demo.cpp`, which draws through `IRenderer` in an SDL3 window: about 52
  fps (paced by a 16 ms sleep), frame capture correct. The shaders are `#version 330 core` and work
  unchanged on macOS's GL 4.1.
- Ubuntu 24.04 (current `ubuntu-latest`) has no `libsdl3-dev`, so `make test` skips the SDL compile
  check when SDL3 is missing.
- Apple Silicon can't run 32-bit code at all, and the game is 32-bit only (issue #840, "Why 32-bit"). A
  native macOS game needs M4 (ARM64 with 32-bit pointers). Until then, on a Mac the game only runs under
  wine with the renderer DLLs.

## Working on macOS (VC6 under wine)

The VC6 build works on Apple Silicon with Homebrew's `wine-stable` (9.0, through Rosetta). Things that
went wrong:

- `vc6_setup.py` only handles Windows and Linux, and crashes on Darwin (`result` unbound in
  `import_reg_file`). The build worked without it here.
- **wineserver crashed** twice during full rebuilds with all cores. `python3 build.py --cores 4` held up.
- After a crash the link can leave a **truncated `decomp_main.exe`** (exactly 512 KiB instead of about 1.6
  MB) with an older `output.map`. `msvc_dump_new_data.py` then fails with `IndexError`, and the verify
  prints `[0/3423]`. Delete both and rebuild.
- `compare_builds.py` happily compares a **stale** exe when the build failed. Check `build.py`'s exit code
  (or the absence of `error C`) before trusting it.
- A full VC6 rebuild after a change to `fix16.hpp`/`ang16.hpp`/`Fix16_Point.hpp` takes over 10 minutes
  under wine on this machine; the clang check takes 4 seconds. Iterate with clang, verify with VC6.

## Repeating it

1. Branch from the current `master` (and the current renderer branch).
2. `Scripts/port/clang_check.sh`. Expect the same kinds of error as above; each section says how it was
   fixed.
3. Fix in this order, since earlier ones hide later ones: includes (1), `__clang__` guards (2),
   `Function.hpp` (3), fake STL (4), then `const&` (5) round by round, then the small items (6).
4. Every few steps: `python3 build.py --cores 4` and `compare_builds.py` against a baseline saved on
   `master`. Expect "changed: 0"; a change means the edit was not neutral (so far only overload
   resolution did that).
5. Commit per kind of fix, so a regression can be bisected.
6. On a branch for the fork: `Scripts/port/build.sh`, then `Scripts/port/run_wine.sh` (once with no map
   for the frontend, once with `wil`). Compare frontend captures with `10.5.exe`'s; they should be
   identical. Any new crash: `addr2line`, and suspect undefined behaviour that VC6 tolerated before
   suspecting the decomp.
