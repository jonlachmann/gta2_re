# A 64-bit build of the port

Status: the Linux x86_64 build compiles, runs and plays as the i686 one does. The same sources
build both; nothing changes for 32-bit targets (the i686 port's objects and the VC6 matching
build's functions are unchanged).

```bash
cmake -S port -B build_linux64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-linux64-clang.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build_linux64
```

It takes the host's 64-bit SDL3, libarchive and unshield (no `GTA2_SETUP_DEPS_DIR`). The packages
(Windows x64, Linux x86_64 tarball, AppImage and Flatpak, macOS universal) are described in the port's README,
Packages; the Windows x64 build runs under wine as the Linux one does.

## Why

The i686 Linux build takes glibc, OpenGL, X11, Wayland and sound from the host's 32-bit libraries,
which most distributions do not install by default (and NVIDIA's 32-bit driver libraries, else Mesa
draws on the CPU). macOS runs no 32-bit code, and Android would be plain arm64.

## How

`GTA2_PORT_64BIT` (`Source/Function.hpp`) is defined in port builds with 8-byte pointers. Every change
in `Source/` is under it, or is a typedef or macro that is the original type on 32-bit targets
(`gmp_column_word`, `collided_char_id`, `SCR_SLOT`).

- Win32 types (`port/compat/include`): `DWORD`, `LONG` and `ULONG` stay 32 bits, as on 64-bit
  Windows; the `*_PTR` types follow the pointer size.
- Size asserts: `GTA2_ASSERT_SIZEOF_ALWAYS` gives the 32-bit sizes and is skipped on 64-bit targets.
  `port/src/layout_checks.cpp` checks, on every target, the structures that the game reads from its
  files or writes to saves, player data and replays: they hold no pointers and keep their size.
- Game files whose data holds 4-byte pointers or pointer strides:
  - `.sty`: the SPRX entries are read into wider entries; the delta buffer and container are sized
    for the target's pointers.
  - `.gxt`: the TKEY records are read into wider entries.
  - `.scr`: the declaration commands keep their object in the 4 bytes at 0x8. On 64-bit targets
    these slots are `PtrSlot32` (`port/src/ptr_slot.hpp`), a handle into a table of pointers, so
    the script keeps its layout.
  - `.gmp`: the column data are 4-byte words (`gmp_column_word`), not pointers.
- Pointers the game keeps in 32-bit integers are pointer-sized on 64-bit targets: the car thread
  triggers' script command, NetPlay's buffers, the collided ped, the door's model id (shared with
  its car pointer), the sprite shapes' collision id. Allocations and clears written for 4-byte
  pointers use `sizeof`: the 64 KB aligned allocation, the light grid, the zone list, the police
  crews.
- The three inline asm blocks have C versions off x86 (shared with the 3DS build).

Saves, `PlyDat`, `gta2_registry.ini` and replays have the same format on both builds.

## Checked

Headless runs of the same scenarios on the i686 build before these changes, the i686 build after
and the x86_64 build: every live sprite's position and rotation hashed after each logic step, and
frames captured along the way.

- Three attract replays (Downtown, Industrial), 2000 to 3000 steps each.
- Each city (Downtown, Residential, Industrial): rocket launcher, a tank, driving and firing,
  1900 steps, then a save.
- The frontend into a game; a replay recorded by the i686 build; the saves made by the i686 build
  loaded in both builds and played 650 steps.

All runs give the same state at every step and pixel identical frames. The saves match but for the
unused padding of the map change records, uninitialised memory that differs between two i686
builds too. An AddressSanitizer x86_64 build of the same scenarios reports only two reads past a
global that happen on every target (`KeyboardLayout::GetLayout`, the renderer's `NearLights`).

## Left

- Network play is not implemented in the port: `NetPlay` and `NetUi` compile but keep pointers in
  `s32` in many places, and would need the same treatment.
- Some fatal error messages (`error.cpp`) print arguments through `s32` casts of pointers.
- A few sound details use pointer bits as noise (`sound_obj.cpp`): on any build they depend on
  where the heap puts objects.
- arm64: the macOS app holds arm64 code, built in CI on an Intel runner, which has never run; Linux
  and Windows arm64 are not built.
