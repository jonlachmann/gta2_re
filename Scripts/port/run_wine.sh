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
VIDEO_DLL=${GTA2_VIDEO_DLL:-glvideo.dll} # nullvideo.dll: headless (Port/headless/nullvideo.cpp)
[ -n "$RENDERER" ] && cp "$RENDERER/$VIDEO_DLL" "$RENDERER"/glrender.dll "$GAME"/
[ "$(cd "$(dirname "$EXE")" && pwd)" = "$GAME" ] || cp "$EXE" "$GAME"/
[ -f "$(dirname "$EXE")/SDL3.dll" ] && [ "$(cd "$(dirname "$EXE")" && pwd)" != "$GAME" ] && cp "$(dirname "$EXE")/SDL3.dll" "$GAME"/
true
# 32-bit programs in a 64-bit prefix read the 32-bit registry view: always /reg:32.
# The install's settings, every run (the game writes some back): its .reg writes HKCU, the game reads HKLM.
if [ -f "$GAME/gta2_install.reg" ]; then
  LC_ALL=C sed 's/HKEY_CURRENT_USER/HKEY_LOCAL_MACHINE/' "$GAME/gta2_install.reg" > "$OUT/gta2_hklm.reg"
  wine reg import "$(winepath -w "$OUT/gta2_hklm.reg" 2>/dev/null || echo "$OUT/gta2_hklm.reg")" /reg:32 > /dev/null 2>&1 || true
fi
# No crash dialog: a crashed run just ends (state_check.py notices)
wine reg add "HKCU\Software\Wine\WineDbg" /f /v ShowCrashDialog /t REG_DWORD /d 0 > /dev/null
S='HKLM\SOFTWARE\DMA Design Ltd\GTA2\Screen'; D='HKLM\SOFTWARE\DMA Design Ltd\GTA2\Debug'
wine reg add "$S" /reg:32 /f /v rendername /d glrender.dll > /dev/null
wine reg add "$S" /reg:32 /f /v videoname /d "$VIDEO_DLL" > /dev/null
wine reg add "$S" /reg:32 /f /v do_play_movie /t REG_DWORD /d 0 > /dev/null
# The game switches to full screen (and saves it) when windowed mode fails once; reset it every run.
wine reg add "$S" /reg:32 /f /v start_mode /t REG_DWORD /d 0 > /dev/null
for v in window_width:640 window_height:480 full_width:640 full_height:480; do
  wine reg add "$S" /reg:32 /f /v ${v%%:*} /t REG_DWORD /d ${v##*:} > /dev/null
done
if [ -n "$MAP" ]; then
  wine reg add "$D" /reg:32 /f /v skip_frontend /t REG_DWORD /d 1 > /dev/null
  for k in map:gmp style:sty script:scr; do wine reg add "$D" /reg:32 /f /v ${k%%:*}name /d "$MAP.${k##*:}" > /dev/null; done
else
  # Debug settings are on when the value exists, whatever it holds
  wine reg delete "$D" /reg:32 /f /v skip_frontend > /dev/null 2>&1 || true
fi
# GTA2_LOCKSTEP=1: one logic frame, then one draw, whatever the clock says (RunGameFrame_4DA850 with
# max_frame_rate=0 and min_frame_rate=0), so a run doesn't depend on timing: for the state log (docs/port.md)
if [ -n "$GTA2_LOCKSTEP" ]; then
  for v in max_frame_rate min_frame_rate; do wine reg add "$S" /reg:32 /f /v $v /t REG_DWORD /d 0 > /dev/null; done
fi
# GTA2_DEBUG="log_random do_show_ids ...": debug settings for this run, for both kinds of exe
for flag in $GTA2_DEBUG; do wine reg add "$D" /reg:32 /f /v $flag /t REG_DWORD /d 1 > /dev/null; done
# gta2_port.exe keeps its settings in gta2.ini (Port/registry_ini.cpp): the install's (gta2_install.reg, as
# 10.5.exe gets them in the registry) and then the same few overrides as below, rewritten every run
{ [ -f "$GAME/gta2_install.reg" ] && python3 "$(dirname "$0")/reg2ini.py" "$GAME/gta2_install.reg"
  printf '[screen]\nstart_mode=dword:0\nwindow_width=dword:640\nwindow_height=dword:480\nfull_width=dword:640\nfull_height=dword:480\ndo_play_movie=dword:0\n'
  [ -n "$GTA2_LOCKSTEP" ] && printf 'max_frame_rate=dword:0\nmin_frame_rate=dword:0\n'
  printf '\n[debug]\n'
  [ -n "$MAP" ] && printf 'skip_frontend=dword:1\nmapname=str:%s.gmp\nstylename=str:%s.sty\nscriptname=str:%s.scr\n' "$MAP" "$MAP" "$MAP"
  for flag in $GTA2_DEBUG; do printf '%s=dword:1\n' $flag; done
  [ -n "$GTA2_INI_EXTRA" ] && printf '%b\n' "$GTA2_INI_EXTRA" # more sections, e.g. '[control]\n0=dword:32994'
} > "$GAME/gta2.ini"
[ -n "$GTA2GL_CAPTURE" ] && mkdir -p "$GAME/${GTA2GL_CAPTURE%%,*}"
cd "$GAME" && exec wine $WINE_LAUNCHER "${WINE_LAUNCHER:+./}$(basename "$EXE")" # WINE_LAUNCHER=winedbg: under the debugger
