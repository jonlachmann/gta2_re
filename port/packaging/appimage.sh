#!/bin/sh
# appimage.sh <build dir> <deps prefix> <AppImage runtime> <out dir>: reG2-x86_64.AppImage, the
# linux64 build in the usual AppDir layout (usr/bin, usr/lib, the desktop file, icon and AppStream metainfo)
# behind the type2 runtime. As the portable build, it brings SDL3 and OpenAL Soft and takes glibc, OpenGL,
# X11, Wayland and sound from the system.
set -eu
B=$1; P=$2; RUNTIME=$3; OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/app.sh"
A=$(mktemp -d)/AppDir
mkdir -p "$A/usr/bin" "$A/usr/lib" "$A/usr/share/doc/reG2"

# The game finds the libraries through its RPATH, $ORIGIN/../lib (build.sh).
llvm-strip --strip-all -R .comment -o "$A/usr/bin/reg2" "$B/reg2"
llvm-strip --strip-unneeded -R .comment -o "$A/usr/lib/libSDL3.so.0" "$P/lib/libSDL3.so.0"
llvm-strip --strip-unneeded -R .comment -o "$A/usr/lib/libopenal.so.1" "$B/libopenal.so.1"
chmod 755 "$A/usr/bin/reg2"
chmod 644 "$A"/usr/lib/*

printf '#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/usr/bin/reg2" "$@"\n' > "$A/AppRun"
chmod 755 "$A/AppRun"
sh "$HERE/linux/install_desktop.sh" "$A/usr" "$B"
ln -s "usr/share/applications/$APP_ID.desktop" "$A/$APP_ID.desktop"
ln -s "usr/share/icons/hicolor/256x256/apps/$APP_ID.png" "$A/$APP_ID.png"
ln -s "$APP_ID.png" "$A/.DirIcon"
cp "$HERE/../README.md" "$A/usr/share/doc/reG2/README.md"
cp -r "$P/licenses" "$A/usr/share/doc/reG2/"

mkdir -p "$OUT"
NAME=$OUT/reG2-x86_64.AppImage
mksquashfs "$A" "$A.squashfs" -root-owned -noappend -comp zstd -quiet -no-progress
cat "$RUNTIME" "$A.squashfs" > "$NAME"
chmod +x "$NAME"
rm -rf "$(dirname "$A")"
echo "$NAME"
