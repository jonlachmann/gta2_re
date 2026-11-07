#!/bin/bash
# A release zip of the port (Port/dist): the game, its SDL3, the start scripts, gta2.ini and the README; no game data.
# usage: Scripts/port/package.sh linux|mac|windows <build dir> <out.zip>
set -e
cd "$(dirname "$0")/../.."
platform=$1 build=$2 out=$3
name=gta2-$platform
stage=$(mktemp -d)/$name
mkdir -p "$stage/player"
cp Port/dist/README.txt Port/dist/gta2.ini "$stage/"
case $platform in
  linux)
    cp "$build/gta2_port" Port/dist/gta2.sh Port/dist/gta2-server.sh "$stage/"
    # The game looks next to itself first (rpath $ORIGIN)
    sdl=$(ldd "$build/gta2_port" | awk '/libSDL3\.so/ {print $3}')
    cp -L "$sdl" "$stage/libSDL3.so.0" ;;
  mac)
    cp "$build/gta2_port" Port/dist/gta2.sh Port/dist/gta2-server.sh "$stage/"
    # Homebrew's SDL3 next to the game; both signed again (ad hoc) once changed, as arm64 requires
    sdl=$(otool -L "$stage/gta2_port" | awk '/libSDL3/ {print $1}')
    cp -L "${sdl/@rpath/$(brew --prefix sdl3)/lib}" "$stage/libSDL3.0.dylib"
    chmod u+w "$stage/libSDL3.0.dylib"
    install_name_tool -id @executable_path/libSDL3.0.dylib "$stage/libSDL3.0.dylib"
    install_name_tool -change "$sdl" @executable_path/libSDL3.0.dylib "$stage/gta2_port"
    codesign --force -s - "$stage/libSDL3.0.dylib" "$stage/gta2_port" ;;
  windows)
    cp "$build/gta2_port.exe" "$build/SDL3.dll" Port/dist/gta2.bat Port/dist/gta2-server.bat "$stage/" ;;
  *) echo "no platform $platform"; exit 1 ;;
esac
rm -f "$out"
(cd "$stage/.." && zip -qr - "$name") > "$out"
echo "$out: $(du -h "$out" | cut -f1)"
