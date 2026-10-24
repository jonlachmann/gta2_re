#!/bin/bash
# gta2.ini, the port's settings file (Port/registry_ini.cpp), rewritten by run_wine.sh and run_mac.sh before every run:
# the install's settings (gta2_install.reg, as 10.5.exe gets them in the registry), then the same overrides run_wine.sh
# sets in 10.5.exe's registry.
# usage: Scripts/port/make_ini.sh <game folder> [map]   env: GTA2_LOCKSTEP, GTA2_DEBUG, GTA2_INI_EXTRA (see run_wine.sh)
GAME=$1
MAP=$2
{ [ -f "$GAME/gta2_install.reg" ] && python3 "$(dirname "$0")/reg2ini.py" "$GAME/gta2_install.reg"
  printf '[screen]\nstart_mode=dword:0\nwindow_width=dword:640\nwindow_height=dword:480\nfull_width=dword:640\nfull_height=dword:480\ndo_play_movie=dword:0\n'
  [ -n "$GTA2_LOCKSTEP" ] && printf 'max_frame_rate=dword:0\nmin_frame_rate=dword:0\n'
  printf '\n[software\\freeloader.com\\gta2]\ncityinstalled=dword:3\n' # the installer's: every area playable, no CD check (FreeLoader::CheckCityInstalled_4AE1F0)
  printf '\n[debug]\n'
  [ -n "$MAP" ] && printf 'skip_frontend=dword:1\nmapname=str:%s.gmp\nstylename=str:%s.sty\nscriptname=str:%s.scr\n' "$MAP" "$MAP" "$MAP"
  for flag in $GTA2_DEBUG; do printf '%s=dword:1\n' $flag; done
  [ -n "$GTA2_INI_EXTRA" ] && printf '%b\n' "$GTA2_INI_EXTRA" # more sections, e.g. '[control]\n0=dword:32994'
} > "$GAME/gta2.ini"
exit 0
