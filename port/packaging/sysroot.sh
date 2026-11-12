#!/bin/sh
# sysroot.sh <dir> [i386|amd64]: the system root of Debian 11 (i386 by default) that the portable Linux build
# compiles and links against, so the game and its libraries need glibc 2.31 or later. Run as root in Debian 13 (ci.sh).
set -eu
SR=$1
ARCH=${2:-i386}
GNU=$([ "$ARCH" = amd64 ] && echo x86_64-linux-gnu || echo i686-linux-gnu)
DEB=http://archive.debian.org/debian
# libc and libstdc++ for the game, the headers SDL3 builds against (it loads the libraries at run time),
# and libdecor and PipeWire from bullseye-backports (SDL needs PipeWire 0.3.44 or later).
PKGS="libc6-dev libstdc++-10-dev linux-libc-dev libwayland-bin
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev
    libxkbcommon-dev libwayland-dev libegl-dev libgles-dev libgl-dev libdrm-dev libgbm-dev libudev-dev
    libdbus-1-dev libpulse-dev libasound2-dev
    libpipewire-0.3-dev/bullseye-backports libspa-0.2-dev/bullseye-backports libdecor-0-dev/bullseye-backports"
rm -rf "$SR"
mmdebstrap --quiet --variant=extract --arch="$ARCH" --aptopt='Acquire::Check-Valid-Until "false"' \
    --include="$(echo $PKGS | tr ' ' ',')" bullseye "$SR" "deb $DEB bullseye main" "deb $DEB bullseye-backports main"
# Absolute links point into the build machine's own libraries.
find "$SR" -type l -lname '/*' | while read -r l; do
    ln -sfn "$(realpath -s -m --relative-to="$(dirname "$l")" "$SR$(readlink "$l")")" "$l"
done
rm -rf "$SR/usr/share/doc" "$SR/usr/share/man" "$SR/usr/share/locale" "$SR/usr/share/info" "$SR/var/cache" \
    "$SR"/usr/lib/gcc/$GNU/10/lib*san* "$SR"/usr/lib/gcc/$GNU/10/libgomp*
