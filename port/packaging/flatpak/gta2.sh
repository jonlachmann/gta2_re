#!/bin/sh
# The game is i686: its loader comes from the Compat.i386 extension, which a bundle does not install.
if [ ! -e /lib/ld-linux.so.2 ]; then
    echo "gta2: the 32-bit runtime is missing: flatpak install flathub org.freedesktop.Platform.Compat.i386//26.08" >&2
    exit 1
fi
exec /app/lib32/gta2_re/gta2 "$@"
