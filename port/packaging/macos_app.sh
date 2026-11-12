#!/bin/sh
# macos_app.sh <build dir> <deps prefix> <out dir>: reG2-macos-<arm64|x86_64>.dmg, with the build's reg2.app as
# reG2.app (SDL3 and OpenAL Soft in Contents/Frameworks), the port's README and the libraries' licences. The app
# is signed ad hoc only: not notarized, so Gatekeeper asks before the first start.
set -eu
B=$1; P=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
VERSION=${GTA2_VERSION:-dev}
ARCH=$(uname -m)
APP=$B/reg2.app
T=$(mktemp -d)

# The icon, from the 1024 px master.
I=$T/reG2.iconset
mkdir -p "$I"
for s in 16 32 128 256 512; do
    sips -z $s $s "$HERE/../resources/reg2_1024.png" --out "$I/icon_${s}x${s}.png" > /dev/null
    sips -z $((s * 2)) $((s * 2)) "$HERE/../resources/reg2_1024.png" --out "$I/icon_${s}x${s}@2x.png" > /dev/null
done
mkdir -p "$APP/Contents/Resources"
iconutil -c icns -o "$APP/Contents/Resources/reG2.icns" "$I"

case $VERSION in
    v[0-9]*) SHORT=${VERSION#v} ;;
    *) SHORT=0.0 ;;
esac
plutil -replace CFBundleShortVersionString -string "$SHORT" "$APP/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$VERSION" "$APP/Contents/Info.plist"
otool -L "$APP/Contents/MacOS/reg2" "$APP"/Contents/Frameworks/*.dylib
codesign --force --deep --sign - "$APP"

D=$T/reG2
mkdir -p "$D"
cp -R "$APP" "$D/reG2.app"
cp "$HERE/../README.md" "$D/README.md"
cp -R "$P/licenses" "$D/"
ln -s /Applications "$D/Applications"
mkdir -p "$OUT"
NAME=$(cd "$OUT" && pwd)/reG2-macos-$ARCH.dmg
hdiutil create -volname reG2 -srcfolder "$D" -format UDZO -ov "$NAME" > /dev/null
rm -rf "$T"
echo "$NAME"
