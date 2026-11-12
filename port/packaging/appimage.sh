#!/bin/sh
# appimage.sh <build dir> <deps prefix> <AppImage runtime> <out dir>: reG2-<version>-x86_64.AppImage, the
# linux64 build in the usual AppDir layout (usr/bin, usr/lib, the desktop file, icon and AppStream metainfo)
# behind the type2 runtime. As the portable build, it brings SDL3 and OpenAL Soft and takes glibc, OpenGL,
# X11, Wayland and sound from the system.
set -eu
B=$1; P=$2; RUNTIME=$3; OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd)
ID=io.github.parigi_n.reG2
VERSION=${GTA2_VERSION:-dev}
A=$(mktemp -d)/AppDir
mkdir -p "$A/usr/bin" "$A/usr/lib" "$A/usr/share/applications" "$A/usr/share/metainfo" \
    "$A/usr/share/icons/hicolor/256x256/apps" "$A/usr/share/icons/hicolor/512x512/apps" "$A/usr/share/doc/reG2"

# The game finds the libraries through its RPATH, $ORIGIN/../lib (build.sh).
llvm-strip --strip-all -R .comment -o "$A/usr/bin/gta2" "$B/gta2"
llvm-strip --strip-unneeded -R .comment -o "$A/usr/lib/libSDL3.so.0" "$P/lib/libSDL3.so.0"
llvm-strip --strip-unneeded -R .comment -o "$A/usr/lib/libopenal.so.1" "$B/libopenal.so.1"
chmod 755 "$A/usr/bin/gta2"
chmod 644 "$A"/usr/lib/*

printf '#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/usr/bin/gta2" "$@"\n' > "$A/AppRun"
chmod 755 "$A/AppRun"
cp "$HERE/linux/$ID.desktop" "$A/usr/share/applications/"
cp "$HERE/linux/$ID.metainfo.xml" "$A/usr/share/metainfo/"
cp "$HERE/../resources/gta2_256.png" "$A/usr/share/icons/hicolor/256x256/apps/$ID.png"
cp "$HERE/../resources/reg2_512.png" "$A/usr/share/icons/hicolor/512x512/apps/$ID.png"
ln -s "usr/share/applications/$ID.desktop" "$A/$ID.desktop"
ln -s "usr/share/icons/hicolor/256x256/apps/$ID.png" "$A/$ID.png"
ln -s "$ID.png" "$A/.DirIcon"
cp "$HERE/../README.md" "$A/usr/share/doc/reG2/README.md"
cp -r "$P/licenses" "$A/usr/share/doc/reG2/"

mkdir -p "$OUT"
NAME=$OUT/reG2-$VERSION-x86_64.AppImage
mksquashfs "$A" "$A.squashfs" -root-owned -noappend -comp zstd -quiet -no-progress
cat "$RUNTIME" "$A.squashfs" > "$NAME"
chmod +x "$NAME"
rm -rf "$(dirname "$A")"
echo "$NAME"
