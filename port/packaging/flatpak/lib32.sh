#!/bin/sh
# lib32.sh <GL32.default> <lib32>: run by the manifest's gta2 module, once the game and SDL3 are in <lib32>.
# Copies into <lib32> the 32-bit loader and the libraries the game, SDL3 and the GL drivers link or open,
# from the SDK's Compat.i386 extension, and Mesa from the GL32.default extension into <lib32>/mesa, so the
# app needs neither extension installed.
set -eu
GL=$1; M=$1/lib; O=$2
C=/usr/lib/i386-linux-gnu
mkdir -p "$O/mesa/gbm" "$O/mesa/egl_vendor.d" "$O/libdecor/plugins-1"

copy() {
    if [ -e "$O/$1" ] || [ -e "$O/mesa/$1" ]; then
        return
    elif [ -e "$M/$1" ]; then
        cp -L "$M/$1" "$O/mesa/"
    else
        # Also in subfolders, for libraries a RUNPATH finds there (libpulsecommon).
        src=$(find "$C" -name "$1" | head -n 1)
        if [ -z "$src" ]; then
            echo "lib32.sh: $1 not found" >&2
            exit 1
        fi
        cp -L "$src" "$O/"
    fi
}

cp -L "$C/ld-linux.so.2" "$O/"
cp "$C/libdecor/plugins-1/libdecor-cairo.so" "$O/libdecor/plugins-1/"
cp "$M/gbm/dri_gbm.so" "$O/mesa/gbm/"
cp "$GL/glvnd/egl_vendor.d/50_mesa.json" "$O/mesa/egl_vendor.d/"
for d in mesa llvm; do
    mkdir -p /app/share/licenses/io.github.gta2re.gta2/$d
    cp -r "$GL/share/licenses/freedesktop-sdk/$d/." /app/share/licenses/io.github.gta2re.gta2/$d/
done

# What SDL3 and glvnd open with dlopen (as appimage.sh), Mesa, and what NVIDIA's GL32 driver links beyond
# Mesa's needs; then everything those link.
for lib in libasound.so.2 libpulse.so.0 libudev.so.1 libdrm.so.2 libgbm.so.1 libwayland-client.so.0 \
        libwayland-cursor.so.0 libwayland-egl.so.1 libdecor-0.so.0 libxkbcommon.so.0 libX11.so.6 \
        libX11-xcb.so.1 libXcursor.so.1 libXext.so.6 libXfixes.so.3 libXrandr.so.2 libXss.so.1 libXtst.so.6 \
        libXi.so.6 libdbus-1.so.3 libEGL.so.1 libGLESv2.so.2 libGL.so.1 libGLX.so.0 libEGL_mesa.so.0 \
        libGLX_mesa.so.0 libwayland-server.so.0 libdl.so.2 libpthread.so.0 librt.so.1; do
    copy "$lib"
done
while :; do
    before=$(find "$O" -type f | wc -l)
    for lib in $(find "$O" -type f \( -name '*.so*' -o -name gta2 \) -exec readelf -d {} + |
                 sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort -u); do
        copy "$lib"
    done
    [ "$(find "$O" -type f | wc -l)" != "$before" ] || break
done
