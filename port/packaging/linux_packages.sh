#!/bin/sh
# linux_packages.sh <build dir> <deps prefix> <out dir>: the Linux packages, from the same binaries: the game
# with the libraries distributions may lack (SDL3, OpenAL Soft), which it finds next to itself. glibc, OpenGL,
# X11, Wayland and sound come from the system's 32-bit libraries.
# - gta2_re-<version>-linux-i686.tar.gz, the portable build: the launcher gta2, bin/ and linux/README.md.
set -eu
B=$1; P=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
VERSION=${GTA2_VERSION:-dev}
T=$(mktemp -d)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

S=$T/bin
mkdir -p "$S"
llvm-strip --strip-all -R .comment -o "$S/gta2" "$B/gta2"
llvm-strip --strip-unneeded -R .comment -o "$S/libSDL3.so.0" "$P/lib/libSDL3.so.0"
llvm-strip --strip-unneeded -R .comment -o "$S/libopenal.so.1" "$B/libopenal.so.1"
chmod 755 "$S/gta2"
chmod 644 "$S"/*.so.*
# The newest glibc symbol version they need: the oldest glibc they start with.
llvm-objdump -T "$S"/* | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 1

NAME=gta2_re-$VERSION-linux-i686
D=$T/$NAME
mkdir -p "$D/licenses"
cp -a "$S" "$D/bin"
cp "$HERE/linux/gta2" "$D/gta2"
chmod 755 "$D/gta2"
cp "$HERE/linux/README.md" "$D/README.md"
cp "$HERE/../README.md" "$D/README-port.md"
cp "$P"/licenses/* "$D/licenses/"
tar -C "$T" --owner=0 --group=0 --numeric-owner -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"

rm -rf "$T"
