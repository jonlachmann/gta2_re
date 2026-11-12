# reG2, portable Linux build (i686)

Start the game with `./reg2`. GTA2's files are not included: on the first start, choose your GTA2
folder, its installer or a disc image, and the game's data is copied to `~/.local/share/gta2_re/`
(`$XDG_DATA_HOME/gta2_re/`), with the settings and saves. `./reg2 --setup` shows the setup again.
`README-port.md` describes the port.

The game is a 32-bit (i686) program. It brings SDL 3 and OpenAL Soft (`bin/`) and takes the rest from
the system's 32-bit libraries: glibc 2.29 or later, OpenGL (Mesa, or NVIDIA's driver), X11 or Wayland,
and PulseAudio (which PipeWire also serves) or ALSA. Without them, `./reg2` says which are missing.
Tested on Debian 11 and 12, Ubuntu 24.04 and Arch Linux.

## 32-bit libraries

Debian and Ubuntu (Debian 11 has no `libdecor-0-0`):

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install libc6:i386 libgcc-s1:i386 libgl1:i386 libegl1:i386 libgles2:i386 libgl1-mesa-dri:i386 \
    libx11-6:i386 libxext6:i386 libxi6:i386 libxcursor1:i386 libxrandr2:i386 libxss1:i386 \
    libxkbcommon0:i386 libwayland-client0:i386 libwayland-cursor0:i386 libwayland-egl1:i386 \
    libdecor-0-0:i386 libpulse0:i386 libdbus-1-3:i386 libudev1:i386
```

Arch Linux, with the `[multilib]` repository enabled in `/etc/pacman.conf`:

```bash
sudo pacman -S lib32-glibc lib32-gcc-libs lib32-mesa lib32-libglvnd lib32-libx11 lib32-libxext lib32-libxi \
    lib32-libxcursor lib32-libxrandr lib32-libxss lib32-libxkbcommon lib32-wayland lib32-libdecor \
    lib32-libpulse lib32-dbus lib32-systemd
```

Fedora (not tested):

```bash
sudo dnf install glibc.i686 libgcc.i686 mesa-libGL.i686 mesa-libEGL.i686 mesa-dri-drivers.i686 \
    libglvnd-gles.i686 libX11.i686 libXext.i686 libXi.i686 libXcursor.i686 libXrandr.i686 \
    libXScrnSaver.i686 libxkbcommon.i686 libwayland-client.i686 libwayland-cursor.i686 \
    libwayland-egl.i686 libdecor.i686 pulseaudio-libs.i686 dbus-libs.i686 systemd-libs.i686
```

With NVIDIA's proprietary driver, the GPU also needs its 32-bit libraries, of the same version as the
driver (Steam installs them too): `lib32-nvidia-utils` on Arch, `nvidia-driver-libs:i386` on Debian,
`libnvidia-gl-<version>:i386` on Ubuntu, `xorg-x11-drv-nvidia-libs.i686` on Fedora (RPM Fusion).
Without them, Mesa draws on the CPU (llvmpipe).

The setup's file chooser is the desktop's (xdg-desktop-portal), or zenity.

## Contents

- `reg2`: checks the 32-bit libraries, then starts `bin/reg2`.
- `bin/reg2`: the game, which finds `libSDL3.so.0` and `libopenal.so.1` (3D sound) next to itself.
- `licenses/`: the licences of SDL 3, OpenAL Soft (LGPL), and libarchive, unshield, zlib and xz, which
  the game links for its setup.
