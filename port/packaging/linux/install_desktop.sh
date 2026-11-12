#!/bin/sh
# install_desktop.sh <prefix> <build dir>: the desktop file, AppStream metainfo and icons under <prefix>/share, named
# after APP_ID (app.sh), for the AppImage and the Flatpak. The metainfo's release is the build's version, today.
set -eu
P=$1
VERSION=$(sed -n 's/^GTA2_VERSION:STRING=//p' "$2/CMakeCache.txt")
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../app.sh"
R=$HERE/../../resources
mkdir -p "$P/share/applications" "$P/share/metainfo" "$P/share/icons/hicolor/256x256/apps" \
    "$P/share/icons/hicolor/512x512/apps"
sed "s|@APP_ID@|$APP_ID|g" "$HERE/reG2.desktop" > "$P/share/applications/$APP_ID.desktop"
# AppStream's developer id: the app id's prefix, in lower case.
DEV=$(echo "${APP_ID%.*}" | tr '[:upper:]' '[:lower:]')
sed -e "s|@APP_ID@|$APP_ID|g" -e "s|@DEVELOPER_ID@|$DEV|g" -e "s|@HOMEPAGE@|$HOMEPAGE|g" \
    -e "s|@VERSION@|$VERSION|g" -e "s|@DATE@|$(date -u +%Y-%m-%d)|g" \
    "$HERE/reG2.metainfo.xml" > "$P/share/metainfo/$APP_ID.metainfo.xml"
cp "$R/reg2_256.png" "$P/share/icons/hicolor/256x256/apps/$APP_ID.png"
cp "$R/reg2_512.png" "$P/share/icons/hicolor/512x512/apps/$APP_ID.png"
chmod 644 "$P/share/applications/$APP_ID.desktop" "$P/share/metainfo/$APP_ID.metainfo.xml" "$P"/share/icons/hicolor/*/apps/*
