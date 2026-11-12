# GTA2 port

Native builds of the decompiled game with clang, 64-bit and 32-bit. Plan: `docs/port_plan.md`.

| Build | Platform layer | Toolchain file |
|---|---|---|
| Linux (x86_64) | SDL3, OpenGL ES 3.0 (or OpenGL 3.3 core), built-in renderer, sound and input ([docs/64bit.md](docs/64bit.md)) | `toolchain-linux64-clang.cmake` |
| Linux (i686) | the same, with the host's 32-bit libraries | `toolchain-linux32-clang.cmake` |
| Windows (MinGW, x86_64) | the same as Linux | `toolchain-mingw64-clang.cmake` |
| Windows (MinGW, i686) | the same as Linux; `-DGTA2_PORT_DIRECTX=ON` for DMA's DirectX, Miles and Bink DLLs | `toolchain-mingw32-clang.cmake` |
| macOS (arm64, x86_64) | the same as Linux, OpenGL 3.3 core; makes `reG2.app` (untested) | none (Xcode's clang) |

## Windows

Needs SDL3 for MinGW (`SDL3-devel-<version>-mingw` from the SDL releases), and libarchive and
unshield (see [Setup libraries](#setup-libraries)). `SDL3.dll` is copied next to `reg2.exe`; the exe
needs no DirectX, Miles or Bink DLL.

```bash
cmake -S port -B build_port -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-mingw32-clang.cmake \
      -DSDL3_DIR=<SDL3-devel>/i686-w64-mingw32/lib/cmake/SDL3 -DGTA2_SETUP_DEPS_DIR=<prefix> \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build_port
```

For x64, `toolchain-mingw64-clang.cmake` and the SDK's `x86_64-w64-mingw32` folder.

## macOS

Not tested yet. `packaging/deps.sh macos` builds the libraries (SDL3, OpenAL Soft, libarchive, unshield)
for the machine's architecture, `packaging/build.sh macos` the app, `packaging/macos_app.sh` the .dmg (the
`macos` job of `.github/workflows/port.yml`). The OpenGL declarations come from `renderer/khronos`, the
Khronos headers, which macOS lacks.

## Linux

Needs clang, lld, ninja, cmake, 32-bit SDL3 (`lib32-sdl3` on Arch), libarchive and unshield (see
[Setup libraries](#setup-libraries); `-DGTA2_SETUP_DEPS_DIR=<prefix>` where they are not on the system).

```bash
cmake -S port -B build_linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-linux32-clang.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build_linux
```

On its first start the port asks for the game's files (see [Game data](#game-data-and-first-start)).
A developer can also run it from a copy of the game folder, or point `GTA2_DIR` to one:

```bash
GTA2_DIR=~/games/gta2 ./build_linux/reg2
```

Settings live in `gta2_registry.ini` (the registry keys of the Windows game). Alt+Enter switches
between window and full screen. Every gamepad drives the player (only those whose name contains
`GTA2_GAMEPAD` if set): a button counts on any of them, the sticks are those of the pad used last.
Bind them in the game's control options (stick and d-pad are the two axes).
In a vehicle, with FORWARD and BACKWARD on the pad's up and down (the default), RT accelerates, LT brakes
or reverses, both as far as they are pressed, the left stick only steers, more gently when pushed part
way, and the d-pad works as on foot; SPECIAL 2 (the camera pan, LT
by default) is then on L3, unless a control uses it.
The menus also take the mouse: pointing selects, a click enters or changes a value, the right
button goes back, the wheel changes a value; the cursor shows while the mouse is in use over a menu.
Each control has a key, a second key (ALTERNATE: a key, a mouse button or a wheel notch, by default
WASD by their position (ZQSD on AZERTY) to move, F to enter or exit, the left button to attack and the
wheel to change weapons; Delete clears it) and a gamepad input.
CONTROLS on the controls page chooses how the player walks on foot: CLASSIC, the original's tank
controls, or TWIN STICK, which moves and aims apart, both relative to the screen.
The left stick, the d-pad or the movement keys walk; the right stick aims (its reticle
further out the more it is pushed, up to the screen's edge, and held a moment once released), or the mouse does (confined to the window,
a reticle in place of the cursor). RT attacks (held from driving or getting in, once pressed again), the other controls stay as bound. Without an aim the player faces the
way he walks; with one he turns to it and walks any way at the same speed (backward with the run
animation, as the original's back key; his shots and flames take on his speed along the way he walks, so backing
off shortens their reach as the original's backward walk does). Walking off the aim, he is drawn as Hotline Miami's peds
are: from 45 to 135 degrees off, the walk frame's legs hidden, behind and in front, his feet, rounded boxes the colour of his shoes, step out from under his torso along the move, one ahead while the other is behind, the rest of him at the aim; further back his legs come back, the trailing one along the move, and his walk plays backward. In a jump the move steers him toward its way, as fast as the
original's left and right turn him in the air. The machine guns fire straight along the aim: the original bends
their shots toward a ped up to 16 degrees off the line of fire. The electro weapons keep their lock on a target.
AIM CAMERA (on by default) moves the view toward the aim on foot, about a third of the way to the
cursor or the pad's reticle; held, left Shift, LT (instead of SPECIAL 2, while that keeps its default LT)
or R3 (unless a control is bound to it) looks further, as far as keeps the player on screen.
In vehicles the controls stay classic, but the fire truck's, the tank's and the gun jeep's roof gun turns
toward the mouse or the right stick's reticle at the original's turret speed; ATTACK fires it as before, RB
too (instead of NEXT WEAPON, while that keeps its default RB), and SPECIAL with left or right still turns it
while held. The stick and mouse are not recorded in replays.
Movies are skipped. Renderer options are in `gta2gl.ini` (`renderer/gta2gl.ini.example`);
Scroll Lock toggles them.

## Game data and first start

The port does not include GTA2's files. It runs in the first of these folders that holds the game's
data (`data/fstyle.sty`), and writes its settings and saves there:

1. `GTA2_DIR`, if set;
2. the current folder, then the executable's folder (a game folder the port was copied into);
3. the per-user folder, which the setup fills. Packages (the Linux ones, a macOS bundle, an
   installed Windows build) always end up here: nothing is written next to the executable.

| Platform | Per-user folder (`SDL_GetPrefPath`) |
|---|---|
| Linux | `$XDG_DATA_HOME/gta2_re/`, by default `~/.local/share/gta2_re/` (Flatpak: `~/.var/app/io.github.parigi_n.reG2/data/gta2_re/`) |
| Windows | `%APPDATA%\gta2_re\` |
| macOS | `~/Library/Application Support/gta2_re/` |

It holds `data/` (the game's data, the music in `data/GTAudio/`), `gta2_registry.ini` (settings),
`gta2gl.ini` (renderer options), `player/` (saves and high scores) and `test/` (replays, logs).

Without the data there, a message box offers three choices:

- **Use an existing install:** a GTA2 folder (the one holding `data`, or `data` itself). Its
  `data` folder is copied, with `gta2_registry.ini`, `gta2gl.ini` and `player/` if the port ran there.
- **Install from a file:** the game's installer or a disc image, read by the port (nothing is run).
- **I don't have the game:** a short text (`kNoGameText` in `setup/setup.cpp`) and a link to the
  Internet Archive item [rockstar-classics](https://archive.org/details/rockstar-classics), which
  holds the 2004 Rockstar Classics `GTA2.exe`.

The files are copied into a temporary folder, checked (the essential files are listed in
`setup/install.cpp`), then moved into place. Without the music (the 22 tracks of `GTAudio` on the
original CD) the game runs, and the setup offers to add it from a folder (the CD, its `GTAudio`), a
disc image or the installer. `reg2 --setup` shows the setup again, to replace the data or add the music.

| Source | Read with |
|---|---|
| A game folder, or its `data` or `GTAudio` folder | |
| InstallShield setup `.exe` (the 2004 Rockstar Classics `GTA2.exe`): its files follow the PE image | `setup/install.cpp`, then unshield |
| InstallShield cabinets (`data1.hdr`, `data*.cab`: the original CD), in a folder or a disc image | unshield |
| Disc images (`.iso`, Joliet names), `.zip`, `.7z` and the other formats of libarchive | libarchive |

Other installers (NSIS, Inno Setup) are not read: install the game with them, then use its folder.

Message boxes are SDL's: task dialogs on Windows, `NSAlert` on macOS. On Linux, zenity if installed
(GTK dialogs), else SDL's own X11 box; when neither is available (Wayland without XWayland) the port
draws the question in a window of its own. The folder and file pickers are SDL's (the
xdg-desktop-portal on Linux, else zenity). The progress is a small SDL
window; closing it cancels the copy.

## Frame rate

The game logic always runs in the original's steps of 33 ms (11 ms in fast forward), 30.3 per
second, on a monotonic clock; the original's `max_frame_rate` (off: the game as fast as the
machine) is not read. The SCREEN settings page chooses how often frames are drawn:

- FRAME LIMIT (`port_max_fps`): 30 ORIGINAL draws once after each step, as the original; 60, 120,
  144, 240 or UNLIMITED draw in between, with every sprite and the camera interpolated between the
  last two steps (`src/interpolation.cpp`). That shows the game one step (33 ms) late.
- VSYNC (`port_vsync`): adaptive where the driver has it. With it, above 30 ORIGINAL, the screen's
  refresh paces the frames: the port presents on every refresh, a new frame every whole number of
  refreshes nearest the frame limit (60 on a 144 Hz screen: every 2, 72 per second) and the last one
  again in between, so each frame stays on the screen equally long. Without it, or when the screen
  does not give its refresh rate, a timer paces them.
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

On Linux without `GTA2_OPENAL_DIR`, the build uses the system's OpenAL Soft headers when it finds
them (`AL/efx-presets.h`, for example from the distribution's `openal` package) and the game loads
the system's `libopenal.so.1`, which must match the build's architecture.

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

## Third-party code

| Code | Version | License | Use |
|---|---|---|---|
| [SDL 3](https://libsdl.org) | 3.4 | zlib | platform layer (linked) |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 1.25.2 | LGPL | 3D sound (loaded at run time, optional) |
| [libarchive](https://github.com/libarchive/libarchive/releases/tag/v3.8.11) | 3.8.11 | BSD | setup: disc images and archives (linked) |
| [unshield](https://github.com/twogood/unshield/releases/tag/1.6.2) | 1.6.2 | MIT | setup: InstallShield cabinets (linked) |
| [zlib](https://github.com/madler/zlib/releases/tag/v1.3.2), [xz](https://github.com/tukaani-project/xz/releases/tag/v5.8.4) | 1.3.2, 5.8.4 | zlib, 0BSD | their compression, where the system has none (MinGW) |

### Setup libraries

The setup links libarchive and unshield. Distributions and Homebrew have libarchive; unshield is
packaged by most distributions and Homebrew.
CMake finds them on the system, or in `GTA2_SETUP_DEPS_DIR`, an install prefix with both built
static (and, for MinGW, zlib as `libzs.a` and liblzma). Arch has no 32-bit libarchive or unshield;
build them from their releases with the port's toolchain files, for example:

```bash
P=$PWD/setup_deps    # then -DGTA2_SETUP_DEPS_DIR=$P
TC="-G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/toolchain-linux32-clang.cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_C_FLAGS=-m32"
cmake -S unshield-1.6.2 -B b_unshield $TC -DBUILD_STATIC=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
ninja -C b_unshield install
# The static library leaves out unshield's MD5 objects.
llvm-ar rs $P/lib/libunshield.a b_unshield/lib/md5/CMakeFiles/md5.dir/md5c.c.o
cmake -S libarchive-3.8.11 -B b_archive $TC -DBUILD_SHARED_LIBS=OFF -DPOSIX_REGEX_LIB=NONE -DENABLE_TEST=OFF \
      -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF -DENABLE_OPENSSL=OFF \
      -DENABLE_CNG=OFF -DENABLE_LIBXML2=OFF -DENABLE_EXPAT=OFF -DENABLE_ZSTD=OFF -DENABLE_LZ4=OFF \
      -DENABLE_BZip2=OFF -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_ICONV=OFF -DENABLE_LIBB2=OFF
ninja -C b_archive install
```

For MinGW, first build zlib (`-DZLIB_BUILD_SHARED=OFF`) and xz (`-DBUILD_SHARED_LIBS=OFF -DXZ_ASM_I386=OFF`)
into the same prefix, then pass `-DZLIB_LIBRARY=$P/lib/libzs.a -DZLIB_INCLUDE_DIR=$P/include
-DLIBLZMA_LIBRARY=$P/lib/liblzma.a -DLIBLZMA_INCLUDE_DIR=$P/include -DCMAKE_C_FLAGS=-DLZMA_API_STATIC`
to unshield and libarchive.

## Packages

`.github/workflows/port.yml` builds the packages on pushes to `port/**` branches, on `v*` tags (a draft
release of the 64-bit packages) and on demand. On Linux each is `packaging/ci.sh` in a Debian 13 container,
which installs the tools, downloads the sources pinned in `packaging/versions.sh`, builds the libraries
(`packaging/deps.sh`), the port (`packaging/build.sh`), then the packages. The same runs locally, from the
repository's root:

```bash
docker run --rm -v $PWD:/src:ro -v $PWD/../work:/work -w /src debian:trixie sh port/packaging/ci.sh linux64 /work
docker run --rm -v $PWD:/src:ro -v $PWD/../work:/work -w /src debian:trixie sh port/packaging/ci.sh mingw64 /work
docker run --rm -v $PWD:/src:ro -v $PWD/../work:/work -w /src i386/debian:trixie linux32 sh port/packaging/ci.sh linux32 /work
docker run --rm -v $PWD:/src:ro -v $PWD/../work:/work -w /src debian:trixie sh port/packaging/ci.sh mingw32 /work
# A Linux package in a clean container; a GTA2 folder as a second argument also installs and plays the game.
docker run --rm -v $PWD/../work/out:/out:ro -v $PWD/port/packaging:/p:ro ubuntu:24.04 sh /p/smoke_test.sh /out/reG2-linux-x86_64.tar.gz
# The Flatpak bundle, and its test on a clean Debian 13 (bwrap needs these options in a container).
BWRAP="--security-opt seccomp=unconfined --security-opt apparmor=unconfined --security-opt systempaths=unconfined"
docker run --rm $BWRAP -v $PWD:/src:ro -v $PWD/../fpwork:/work -w /src \
    ghcr.io/flathub-infra/flatpak-github-actions:freedesktop-26.08 sh port/packaging/flatpak.sh /work
docker run --rm $BWRAP -v $PWD/../fpwork/out:/out:ro -v $PWD/port/packaging:/p:ro debian:trixie \
    sh /p/flatpak_smoke_test.sh /out/reG2-x86_64.flatpak
```

The releases are 64-bit:

- Windows: `reG2-windows-x64.zip`, with `reg2.exe`, `SDL3.dll`, `OpenAL32.dll`, this README and
  the libraries' licences. Built with llvm-mingw on UCRT, part of Windows 10 and later.
- Linux, portable: `reG2-linux-x86_64.tar.gz`. Extract it anywhere and start `./reg2`. It brings
  SDL 3 and OpenAL Soft, and takes glibc (2.29 or later), OpenGL, X11, Wayland and sound from the system,
  so it uses the system's GPU driver. `packaging/linux/README-x86_64.md` is its README; `./reg2` says which
  libraries are missing.
- Linux, AppImage: `reG2-x86_64.AppImage`, the same build and libraries in an AppImage
  (`packaging/appimage.sh`, the type2 runtime), with its desktop file, icon and AppStream metainfo.
- Linux, Flatpak: `reG2-x86_64.flatpak`, a bundle of `packaging/flatpak/io.github.parigi_n.reG2.yml`
  on the `org.freedesktop.Platform` 26.08 runtime, whose SDL 3, OpenAL Soft and libarchive it uses
  (`flatpak install --user reG2-x86_64.flatpak` fetches the runtime from Flathub). It needs no
  access to the files: the setup's choosers go through the desktop portal, and the game's data goes to
  `~/.var/app/io.github.parigi_n.reG2/data/gta2_re/`.
- macOS: `reG2-macos-arm64.dmg` and `reG2-macos-x86_64.dmg`, with `reG2.app` (SDL 3 and OpenAL Soft in
  `Contents/Frameworks`) for macOS 11 and later, built on GitHub's macOS runners (`packaging/macos_app.sh`).
  The app is signed ad hoc, not notarized: the first start needs a right click, Open (or
  `xattr -dr com.apple.quarantine reG2.app`). Not tested on a Mac yet.

The file names carry no version, so `releases/latest/download/<name>` always gives the newest. The version
is the release's tag, and inside the packages the start menu's text (`kVersionText`, `src/options_menu.hpp`),
the Windows file version (`resources.rc`) and the AppStream release (`packaging/linux/reG2.metainfo.xml`).
The executable is `reg2` (`reg2.exe`), so that it never replaces a GTA2 folder's `gta2.exe`.

The i686 packages (`reG2-windows-i686.zip`, `reG2-linux-i686.tar.gz`) still build and
are tested in CI, but are not released. The i686 Linux build takes everything but SDL 3 and OpenAL Soft from
the system's 32-bit libraries; `packaging/linux/README.md` lists them per distribution. With NVIDIA's
proprietary driver it needs its 32-bit libraries too, else Mesa draws on the CPU.

The Linux builds compile with Debian 13's clang (OpenAL Soft with its GCC) against a system root of Debian 11
(`packaging/sysroot.sh`, glibc 2.31; `packaging/linux32-sysroot.cmake`, `linux64-sysroot.cmake`), so they
start on distributions as old as that. The C++ runtime is linked statically; OpenAL Soft's needs a few glibc
2.32+ functions, which `packaging/glibc_compat.c` provides. `packaging/smoke_test.sh` installs a tarball or
the AppImage in a clean Debian, Ubuntu or Arch Linux container with the libraries it needs.

## Layout

- `compat/`: the Win32 subset the game calls, on SDL3 and POSIX (window, registry, files,
  DirectInput, Miles and its 3D provider on OpenAL Soft). Outside Windows, `compat/include` declares
  the Win32 and DirectX subset the game uses (Windows' 32-bit layouts and values); on Windows,
  MinGW's headers declare it and `win32_redirect.hpp` sends the game's calls to the same code.
- `renderer/`: the OpenGL ES renderer behind the game's `gbh_*` and `Vid_*` tables.
- `include/`: copies of headers whose originals don't build outside MSVC.
- `setup/`: the game folder and the first-start setup (copy, message boxes).

## Unattended runs

| Variable | Effect |
|---|---|
| `GTA2_INPUT=<frame>:<key>[:<frames held>],...` | key presses at given frames (SDL key names: `Return`, `Up`, `Left Ctrl`, `Alt+Return`), a virtual gamepad (`pad.a`, `pad.dpup`, `pad.lefty-`, `pad.rightx=0.5` for part of an axis; `pad2.` a second one, the first then idle), or the mouse (`mouse.<x>x<y>` moves it to that window position, `mouse.left`, `mouse.right`, `mouse.wheelup`, `mouse.wheeldown`) |
| `GTA2_EXIT_AT=<frame>` | quit at that frame |
| `GTA2_CURSOR_BMP=<file>` | the mouse cursor's image written to that BMP |
| `GTA2GL_CAPTURE=<dir>,<frame>,<a>-<b>,...` | write those frames as BMP (the first frame drawn at each); `GTA2GL_CAPTURE_FLIPS=1` counts presented frames instead |
| `GTA2_LOOP_STATS=1` | logic steps and frames per second on stderr, every 5 seconds |
| `GTA2_LOOP_TRACE=<file>` | the player's and the camera's position after each step (`S`) and as drawn (`D`) |
| `GTA2_PRESENT_LOG=<file>` | every presented frame with the time it reached the screen (Wayland's presentation-time protocol; elsewhere when the swap returned), and every 10 seconds and at exit a summary: frame to frame times, how many refreshes each frame was shown, the shown time against the game time it depicts, on Wayland how many frames the compositor showed on vblank and scanned out directly, and the GPU time of each frame and how long before its presentation the GPU finished it |
| `GTA2_NO_SOUND=1` | no audio device |
| `GTA2_NO_OPENAL=1`, `GTA2_HRTF=1` | no 3D sound; binaural 3D sound |
| `GTA2_LOG_FILE=<file>` | SDL's log, debug strings and message boxes written to that file |
| `GTA2_NO_MESSAGEBOX=1` | message boxes only logged, never shown |
| `GTA2_AUDIO_DRIVER`, `GTA2_AUDIO_FILE` | SDL's audio driver and disk output file (wine hides `SDL_*` variables from Windows programs) |
| `GTA2_DEBUG_OUTPUT=1` | the game's debug strings on stderr |
| `GTA2_SETUP_PATH=<folder or file>` | the setup installs from it, without the first question and the dialog (messages shown after it still wait, unless `GTA2_NO_MESSAGEBOX=1`) |
| `GTA2_SETUP_DRAWN=1` | the setup's questions in its own window, not in message boxes |
| `GTA2_SETUP_CAPTURE=<dir>` | the setup's windows written there as BMP |

Frames count presented frames, except that while the game runs they count its logic steps, so a
script does the same at any frame rate. A run nobody sees, with the sound written to a file:

```bash
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/tmp/audio.raw \
GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=cap,600 \
gamescope --backend headless -W 640 -H 480 -- ./build_linux/reg2
```
