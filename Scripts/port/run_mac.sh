#!/bin/bash
# Run the native macOS build (Scripts/port/build_mac.sh, docs/port.md) in build_port/mac/game, a copy of the install.
# usage: GTA2_DIR=~/Games/GTA2 Scripts/port/run_mac.sh [map]     map: e.g. wil (boots straight into data/wil.gmp)
#   env as run_wine.sh: GTA2_DEBUG, GTA2_LOCKSTEP, GTA2_INI_EXTRA, GTA2GL_CAPTURE, GTA2_HEADLESS, GTA2_STATELOG...
set -e
cd "$(dirname "$0")/../.."
MAP=$1
GAME=$PWD/build_port/mac/game
if [ ! -d "$GAME/data" ]; then
  : "${GTA2_DIR:?set GTA2_DIR to a GTA2 install (data/)}"
  mkdir -p "$GAME" && cp -R "$GTA2_DIR"/. "$GAME"/
  rm -rf "$GAME/test" && mkdir -p "$GAME/test"
fi
Scripts/port/make_ini.sh "$GAME" "$MAP" # the install's settings and the overrides of run_wine.sh
[ -n "$GTA2GL_CAPTURE" ] && mkdir -p "$GAME/${GTA2GL_CAPTURE%%,*}"
cd "$GAME" && exec ${GTA2_BIN:-../gta2_port} $GTA2_ARGS # GTA2_BIN: another build, e.g. ../../mac_x4/gta2_port; GTA2_ARGS: its command line
