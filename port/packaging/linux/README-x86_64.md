# reG2, portable Linux build (x86_64)

Start the game with `./gta2`. GTA2's files are not included: on the first start, choose your GTA2
folder, its installer or a disc image, and the game's data is copied to `~/.local/share/gta2_re/`
(`$XDG_DATA_HOME/gta2_re/`), with the settings and saves. `./gta2 --setup` shows the setup again.
`README-port.md` describes the port.

The game brings SDL 3 and OpenAL Soft (`bin/`) and takes the rest from the system: glibc 2.29 or
later, OpenGL (Mesa, or NVIDIA's driver), X11 or Wayland, and PulseAudio (which PipeWire also serves)
or ALSA. Desktop distributions have them installed; `./gta2` says which are missing. Tested on
Debian 11 and 12, Ubuntu 24.04 and Arch Linux.

On a minimal system, the libraries to install:

```bash
# Debian and Ubuntu
sudo apt install libgl1 libegl1 libgl1-mesa-dri libx11-6 libxext6 libxi6 libxcursor1 libxrandr2 libxss1 \
    libxkbcommon0 libwayland-client0 libwayland-cursor0 libwayland-egl1 libdecor-0-0 libpulse0
# Arch Linux
sudo pacman -S mesa libglvnd libx11 libxext libxi libxcursor libxrandr libxss libxkbcommon wayland \
    libdecor libpulse
```

The setup's file chooser is the desktop's (xdg-desktop-portal), or zenity.

## Contents

- `gta2`: checks the libraries, then starts `bin/gta2`.
- `bin/gta2`: the game, which finds `libSDL3.so.0` and `libopenal.so.1` (3D sound) next to itself.
- `reG2.png`: the icon, for a desktop launcher.
- `licenses/`: the licences of SDL 3, OpenAL Soft (LGPL), and libarchive, unshield, zlib and xz, which
  the game links for its setup.
