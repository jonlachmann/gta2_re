# A 64-bit build of the port

Status: not started. This note records why it is worth doing, what it involves and what was
measured, so the work can start from here.

## Why

The port is i686 only. That costs:

- **32-bit libraries on Linux.** The portable build and the .deb take glibc, OpenGL, X11, Wayland
  and sound from the host's 32-bit libraries, which most distributions do not install by default
  (Steam brings them). With NVIDIA's proprietary driver, its 32-bit libraries are needed too, else
  Mesa draws with llvmpipe (on the CPU). A 64-bit build would use the libraries already there.
- **macOS.** No 32-bit code since 10.15; Rosetta 2 translates x86_64 only.
- **Android** would be plain arm64 instead of OpenCE's ILP32 guest (see below).

## The goal: pointer-size independent, not 64-bit only

The same sources must build as i686 (Windows zip, a future 3DS port, the matching build) and
as x86_64/arm64. The work removes the places that assume 4-byte pointers; the compiler then
picks the pointer size per target. i686 stays a first-class target: built and tested at every
step, the CI keeps its Windows i686 job.

Typical changes:

- A pointer kept in a `u32`/`s32` becomes `uintptr_t`/`intptr_t`.
- A file structure loaded as is from disk that contains a pointer field is described with
  fixed-size integers, and converted at load time into an in-memory structure with real
  pointers.

Every change in `Source/` goes under `#if defined(GTA2_PORT)` and is checked with
`Scripts/bin_comp/compare_builds.py`, so no matched function changes.

## What was measured (x86_64 trial compile, asserts disabled, nothing committed)

- Size asserts: 115 of 199 asserted structs change size; 113 of them hold pointers (319 fields,
  about 3770 slots counting array elements). Under x86_64 these asserts must be skipped or given
  64-bit sizes.
- Casts: 142 pointer to integer and 144 integer to pointer conversions.
- With the game's asserts neutralised, 126 of 149 TUs fail (875 errors), almost all from the
  `static_assert`s in `port/` because the Win32 `DWORD` is a `long`, 8 bytes on LP64 Linux. With
  those disabled too, 5 TUs fail (24 errors: 6 `size_t*`/`u32*` mismatches, 3 inline asm blocks).
- Warnings: 170 `-Wshorten-64-to-32`, 101 `-Wpointer-to-int-cast`.

## The hard part: pointers inside the game's data files

These are loaded straight into memory and hold 4-byte pointers or pointer-sized strides, so
they would load wrong without any compiler error:

- `.sty`: the SPRX and DELX chunks.
- `.gxt`: the TKEY chunk.
- `.scr`: `SCR_POINTER` values in the script bytecode.
- `.gmp`: the `mpColumn` stride.

Saves, `PlyDat` and replays hold no pointers.

## Estimate

About a day to compile, then a few days to two weeks until it runs correctly (the data formats
above, then whatever the replay tests show).

## How others handle it

- **OpenCE** (Halo CE decompilation, https://github.com/OpenCommunityEdition/OpenCE) has the same
  problem, since its data holds 32-bit pointers. Its Windows and Linux builds stay i686: the Linux
  "portable" build links against glibc 2.31, bundles only SDL3 and needs the host's 32-bit
  libraries (glibc, Mesa, PipeWire or PulseAudio, X11, Wayland), as on SteamOS. Android runs the
  game as an ILP32 AArch64 guest (`arm64_32`, 32-bit pointers in 64-bit code) with its own musl;
  the web build is wasm32. macOS is in progress.
- **Julius/Augustus** (Caesar III) are rewritten in portable C and parse the game files field by
  field, so they are 64-bit natively. That route is not open to a matching decompilation.
