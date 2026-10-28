#!/bin/bash
# Run the native macOS build (cmake --preset mac -S Port, docs/port.md) in build_port/mac/game, a copy of the install.
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
# The game keeps its settings in gta2.ini (Port/registry_ini.cpp): the install's and the overrides below
{ [ -f "$GAME/gta2_install.reg" ] && python3 Scripts/port/reg2ini.py "$GAME/gta2_install.reg"
  printf '[screen]\nstart_mode=dword:0\nwindow_width=dword:640\nwindow_height=dword:480\nfull_width=dword:640\nfull_height=dword:480\ndo_play_movie=dword:0\n'
  [ -n "$GTA2_LOCKSTEP" ] && printf 'max_frame_rate=dword:0\nmin_frame_rate=dword:0\n'
  printf '\n[software\\freeloader.com\\gta2]\ncityinstalled=dword:3\n' # the installer's: every area playable, no CD check (FreeLoader::CheckCityInstalled_4AE1F0)
  printf '\n[debug]\n'
  [ -n "$MAP" ] && printf 'skip_frontend=dword:1\nmapname=str:%s.gmp\nstylename=str:%s.sty\nscriptname=str:%s.scr\n' "$MAP" "$MAP" "$MAP"
  for flag in $GTA2_DEBUG; do printf '%s=dword:1\n' $flag; done
  [ -n "$GTA2_INI_EXTRA" ] && printf '%b\n' "$GTA2_INI_EXTRA"
} > "$GAME/gta2.ini"
[ -n "$GTA2GL_CAPTURE" ] && mkdir -p "$GAME/${GTA2GL_CAPTURE%%,*}"
cd "$GAME" && exec ${GTA2_BIN:-../gta2_port} # GTA2_BIN: another build, e.g. ../../mac_x4/gta2_port
