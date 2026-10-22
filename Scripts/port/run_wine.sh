#!/bin/bash
# Run build_port/gta2_port.exe (or the exe given) under wine with PR #837's OpenGL renderer (docs/port.md).
# usage: GTA2_DIR=~/Games/GTA2 Scripts/port/run_wine.sh [exe] [map]
#   gta2_port.exe has the renderer linked in; for another exe (10.5.exe) set RENDERER to the folder with
#   glvideo.dll and glrender.dll (make -C Renderers/OpenGL).
#   map: e.g. wil (boots straight into data/wil.gmp, skipping the frontend); none = the frontend
# Frames: set GTA2GL_CAPTURE=<dir>,<frame>,... (dir relative to the game folder), see the renderer README.
set -e
cd "$(dirname "$0")/../.."
OUT=$PWD/build_port
EXE=${1:-$OUT/gta2_port.exe}
MAP=$2
export WINEPREFIX=${WINEPREFIX:-$OUT/wineprefix} WINEDEBUG=${WINEDEBUG:--all}
: "${GTA2_DIR:?set GTA2_DIR to a GTA2 install (data/, mss32.dll, binkw32.dll)}"
GAME=$OUT/game
if [ ! -d "$GAME/data" ]; then
  mkdir -p "$GAME" && cp -R "$GTA2_DIR"/. "$GAME"/
  wineboot -i > /dev/null 2>&1
fi
[ -n "$RENDERER" ] && cp "$RENDERER"/glvideo.dll "$RENDERER"/glrender.dll "$GAME"/
cp "$EXE" "$GAME"/
[ -f "$(dirname "$EXE")/SDL3.dll" ] && cp "$(dirname "$EXE")/SDL3.dll" "$GAME"/
# 32-bit programs in a 64-bit prefix read the 32-bit registry view: always /reg:32.
S='HKLM\SOFTWARE\DMA Design Ltd\GTA2\Screen'; D='HKLM\SOFTWARE\DMA Design Ltd\GTA2\Debug'
wine reg add "$S" /reg:32 /f /v rendername /d glrender.dll > /dev/null
wine reg add "$S" /reg:32 /f /v videoname /d glvideo.dll > /dev/null
wine reg add "$S" /reg:32 /f /v do_play_movie /t REG_DWORD /d 0 > /dev/null
# The game switches to full screen (and saves it) when windowed mode fails once; reset it every run.
wine reg add "$S" /reg:32 /f /v start_mode /t REG_DWORD /d 0 > /dev/null
if [ -n "$MAP" ]; then
  wine reg add "$D" /reg:32 /f /v skip_frontend /t REG_DWORD /d 1 > /dev/null
  for k in map:gmp style:sty script:scr; do wine reg add "$D" /reg:32 /f /v ${k%%:*}name /d "$MAP.${k##*:}" > /dev/null; done
else
  # Debug settings are on when the value exists, whatever it holds
  wine reg delete "$D" /reg:32 /f /v skip_frontend > /dev/null 2>&1 || true
fi
# gta2_port.exe keeps its settings in gta2.ini (Port/registry_ini.cpp): the same few, rewritten every run
{ printf '[screen]\nstart_mode=dword:0\nwindow_width=dword:960\nwindow_height=dword:720\ndo_play_movie=dword:0\n\n[debug]\n'
  [ -n "$MAP" ] && printf 'skip_frontend=dword:1\nmapname=str:%s.gmp\nstylename=str:%s.sty\nscriptname=str:%s.scr\n' "$MAP" "$MAP" "$MAP"
  [ -n "$GTA2_INI_EXTRA" ] && printf '%b\n' "$GTA2_INI_EXTRA" # more sections, e.g. '[control]\n0=dword:32994'
} > "$GAME/gta2.ini"
[ -n "$GTA2GL_CAPTURE" ] && mkdir -p "$GAME/${GTA2GL_CAPTURE%%,*}"
cd "$GAME" && exec wine "$(basename "$EXE")"
