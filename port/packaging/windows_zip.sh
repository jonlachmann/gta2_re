#!/bin/sh
# windows_zip.sh <build dir> <deps prefix> <out dir>: gta2_re-<version>-windows-i686.zip, the exe with the
# SDL3 and OpenAL Soft DLLs, the port's README and the libraries' licences.
set -eu
B=$1; P=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=gta2_re-${GTA2_VERSION:-dev}-windows-i686
Z=$(mktemp -d)/$NAME
mkdir -p "$Z" "$OUT"
llvm-strip --strip-debug -o "$Z/gta2.exe" "$B/gta2.exe"
llvm-strip --strip-debug -o "$Z/OpenAL32.dll" "$B/OpenAL32.dll"
cp "$B/SDL3.dll" "$Z/"
cp "$HERE/../README.md" "$Z/README.md"
cp -r "$P/licenses" "$Z/"
# The DLLs they import, for the log: SDL3.dll and system ones.
llvm-objdump -p "$Z/gta2.exe" "$Z/OpenAL32.dll" | sed -n 's/^ *DLL Name: //p' | sort -u
OUT=$(cd "$OUT" && pwd)
(cd "$(dirname "$Z")" && zip -qr "$OUT/$NAME.zip" "$NAME")
rm -rf "$(dirname "$Z")"
echo "$OUT/$NAME.zip"
