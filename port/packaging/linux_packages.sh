#!/bin/sh
# linux_packages.sh <build dir> <deps prefix> <out dir>: the portable Linux package, reG2-linux-<arch>.tar.gz
# (i686 or x86_64, after the build's binary): the launcher reg2, bin/ (the game with the libraries distributions
# may lack, SDL3 and OpenAL Soft, which it finds next to itself), its README and the reG2 icon. glibc, OpenGL,
# X11, Wayland and sound come from the system's libraries.
set -eu
B=$1; P=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
if llvm-objdump -f "$B/reg2" | grep -q 'file format elf64'; then
    ARCH=x86_64
    README=$HERE/linux/README-x86_64.md
else
    ARCH=i686
    README=$HERE/linux/README.md
fi

S=$T/bin
mkdir -p "$S"
llvm-strip --strip-all -R .comment -o "$S/reg2" "$B/reg2"
llvm-strip --strip-unneeded -R .comment -o "$S/libSDL3.so.0" "$P/lib/libSDL3.so.0"
llvm-strip --strip-unneeded -R .comment -o "$S/libopenal.so.1" "$B/libopenal.so.1"
chmod 755 "$S/reg2"
chmod 644 "$S"/*.so.*
# The newest glibc symbol version they need: the oldest glibc they start with.
llvm-objdump -T "$S"/* | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 1

NAME=reG2-linux-$ARCH
D=$T/$NAME
mkdir -p "$D/licenses"
cp -a "$S" "$D/bin"
cp "$HERE/linux/reg2" "$D/reg2"
chmod 755 "$D/reg2"
cp "$README" "$D/README.md"
cp "$HERE/../README.md" "$D/README-port.md"
cp "$HERE/../resources/reg2_512.png" "$D/reG2.png"
cp "$P"/licenses/* "$D/licenses/"
tar -C "$T" --owner=0 --group=0 --numeric-owner -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"

rm -rf "$T"
