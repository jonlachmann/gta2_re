# Cross-platform port: progress and findings

Notes for the plan in [issue #840](https://github.com/CriminalRETeam/gta2_re/issues/840) (a native
GTA2 on SDL3 + the OpenGL renderer, built from the frozen decomp in a fork). The issue holds the plan;
this file records what has been tried, what it found, and how to repeat it. The port is meant to be
redone from a later, more complete decomp, so the **Repeating it** section matters more than the
status snapshot.

## Status (2026-10-22)

| Milestone (issue #840) | State |
|---|---|
| M0. Compiles with clang | Partly done. The changes that keep VC6's output identical are done: **104 of 109** game sources compile. The rest needs the fork (see Remaining). |
| M1. Plays on Linux, Windows | Not started. The renderer's SDL3 platform layer exists (see Renderer). |
| M2-M6 | Not started. |

Branches (local, not pushed yet):

- `m0-clang` (from `master`): the M0 fixes below. Every commit keeps `[3423/3423] funcs OK`, and
  `compare_builds.py` reports no changed function. None of them needs to wait for the freeze; they can go
  to `master` as they are.
- `sdl3-mac` (from PR #837, `parigi-n/claude/opengl-renderer`): the renderer's SDL3 port and a macOS
  build, see Renderer.
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

## Remaining for M0 (fork only, breaks matching)

The 5 files that still fail, all reasons the issue already plans for:

- `NetPlay.cpp`, `Network_20324.cpp`, `Player.cpp`, `winmain.cpp`: `NetPlay.hpp` includes
  `<VC98/Include/DPLAY.H>`. The plan removes DirectPlay and the lobby. Being stuck on that include, these
  files may hide more errors (most likely in `Player.cpp` and `winmain.cpp`, which aren't network code).
- `input.cpp`: the non-VC6 branch of `DirectInputCreate_4986D0` uses an undeclared
  `Fn_DirectInputCreateA`. The plan rewrites keyboard and gamepad input on SDL3 in M1, so it isn't worth
  fixing here.
- Removing the matching tools (`build.py`, `Scripts/bin_comp`, reccmp, permuter, HookLoader, the
  `MATCH_FUNC` markers' naked asm).
- Then a real link, which hasn't been tried.

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
