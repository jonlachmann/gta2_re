#!/bin/sh
# appimage.sh <build dir> <deps prefix> <AppImage runtime> <out dir>: gta2_re-<version>-x86_64.AppImage.
# Runs in the i386 userland the game was built in: the AppImage takes its glibc and loader, Mesa (EGL, GLX,
# llvmpipe and the GPU drivers), and the libraries SDL3 loads at run time (X11, Wayland, PulseAudio, ALSA).
# The runtime is x86_64, the hosts the AppImage is for; the game inside is i686.
set -eu
B=$1; P=$2; RUNTIME=$3; OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd)
VERSION=${GTA2_VERSION:-dev}
A=$(mktemp -d)/AppDir
SYS=/usr/lib/i386-linux-gnu
mkdir -p "$A/usr/bin" "$A/usr/lib" "$A/usr/share/doc/gta2_re" "$A/usr/share/applications" \
         "$A/usr/share/icons/hicolor/32x32/apps" "$A/usr/share/glvnd/egl_vendor.d"

llvm-strip --strip-debug -o "$A/usr/bin/gta2" "$B/gta2"
cp "$B/libopenal.so.1" "$A/usr/lib/"
mkdir -p "$A/usr/lib/gbm" "$A/usr/lib/libdecor/plugins-1"
cp "$SYS/gbm/dri_gbm.so" "$A/usr/lib/gbm/"
cp "$SYS/libdecor/plugins-1/libdecor-cairo.so" "$A/usr/lib/libdecor/plugins-1/"
cp /usr/share/glvnd/egl_vendor.d/50_mesa.json "$A/usr/share/glvnd/egl_vendor.d/"

# What SDL3 and glvnd open with dlopen, then everything they and the game link, glibc included.
DLOPENED="libasound.so.2 libpulse.so.0 libudev.so.1 libdrm.so.2 libgbm.so.1 libwayland-client.so.0
    libwayland-cursor.so.0 libwayland-egl.so.1 libdecor-0.so.0 libxkbcommon.so.0 libX11.so.6 libX11-xcb.so.1
    libXcursor.so.1 libXext.so.6 libXfixes.so.3 libXrandr.so.2 libXss.so.1 libXtst.so.6 libXi.so.6
    libdbus-1.so.3 libEGL.so.1 libGLESv2.so.2 libGL.so.1 libGLX.so.0 libEGL_mesa.so.0 libGLX_mesa.so.0"
for lib in $DLOPENED; do
    cp -L "$SYS/$lib" "$A/usr/lib/"
done
cp -L "$P/lib/libSDL3.so.0" "$A/usr/lib/"
cp -L /lib/ld-linux.so.2 "$A/usr/lib/"
while :; do
    new=0
    for dep in $(find "$A/usr" -type f \( -name '*.so*' -o -name gta2 \) -exec ldd {} + 2>/dev/null |
                 awk '$2 == "=>" && $3 ~ /^\// {print $3}' | sort -u); do
        if [ ! -e "$A/usr/lib/$(basename "$dep")" ]; then
            cp -L "$dep" "$A/usr/lib/"
            new=1
        fi
    done
    [ $new = 1 ] || break
done

cp "$HERE/AppRun" "$A/AppRun"
cp "$HERE/gta2_re.desktop" "$A/gta2_re.desktop"
cp "$HERE/gta2_re.desktop" "$A/usr/share/applications/"
cp "$HERE/../resources/gta2.png" "$A/usr/share/icons/hicolor/32x32/apps/gta2_re.png"
cp "$HERE/../resources/gta2.png" "$A/gta2_re.png"
ln -s gta2_re.png "$A/.DirIcon"

# The port's README, the licences of the libraries built from source, and the Debian copyright of the rest.
D=$A/usr/share/doc/gta2_re
cp "$HERE/../README.md" "$D/"
cp -r "$P/licenses" "$D/"
for f in "$A"/usr/lib/*.so* "$A"/usr/lib/*/*.so "$A"/usr/lib/*/*/*.so; do
    src=$(readlink -f "$SYS/${f#"$A"/usr/lib/}" 2>/dev/null || true)
    [ -e "$src" ] || src=$(readlink -f "/lib/$(basename "$f")")
    pkg=$(dpkg -S "$src" 2>/dev/null | head -n1 | cut -d: -f1) || true
    if [ -n "$pkg" ] && [ -f "/usr/share/doc/$pkg/copyright" ]; then
        mkdir -p "$D/debian/$pkg"
        cp "/usr/share/doc/$pkg/copyright" "$D/debian/$pkg/"
    fi
done

mkdir -p "$OUT"
NAME=$OUT/gta2_re-$VERSION-x86_64.AppImage
mksquashfs "$A" "$A.squashfs" -root-owned -noappend -comp zstd -quiet -no-progress
cat "$RUNTIME" "$A.squashfs" > "$NAME"
chmod +x "$NAME"
rm -rf "$(dirname "$A")"
echo "$NAME"
