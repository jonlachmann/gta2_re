#!/bin/sh
# smoke_test.sh <AppImage>: starts it without game data on a host with no 32-bit library (run as root in
# debian:trixie). With SDL's dummy video driver the setup finds no data and quits; under Xvfb its own
# window, drawn through the AppImage's Mesa (GLX, then EGL), is captured.
set -eu
APP=$(readlink -f "$1")
if [ -e /lib/ld-linux.so.2 ] || [ -e /lib32/ld-linux.so.2 ]; then
    echo "smoke_test.sh: this host has 32-bit libraries" >&2
    exit 1
fi
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends xvfb xauth > /dev/null

T=$(mktemp -d)
export APPIMAGE_EXTRACT_AND_RUN=1 GTA2_NO_SOUND=1 HOME=$T/home XDG_DATA_HOME=$T/data
mkdir -p "$HOME"

GTA2_NO_MESSAGEBOX=1 SDL_VIDEODRIVER=dummy GTA2_LOG_FILE=$T/dummy.log "$APP"
cat "$T/dummy.log"
grep -q "game files were not found" "$T/dummy.log"
[ -d "$XDG_DATA_HOME/gta2_re" ]

for render in opengl opengles2; do
    mkdir -p "$T/cap_$render"
    rc=0
    SDL_RENDER_DRIVER=$render SDL_VIDEO_FORCE_EGL=$([ $render = opengles2 ] && echo 1 || echo 0) \
    GTA2_SETUP_DRAWN=1 GTA2_SETUP_CAPTURE=$T/cap_$render GTA2_LOG_FILE=$T/$render.log \
        timeout -s KILL 20 xvfb-run -a -s "-screen 0 1024x768x24" "$APP" || rc=$?
    cat "$T/$render.log"
    ls -l "$T/cap_$render"
    # Killed by timeout while the window waits for an answer.
    [ "$rc" = 137 ]
    # The question's text and buttons, not a blank window.
    [ "$(od -An -tu1 -v "$T/cap_$render/setup_0.bmp" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 8 ]
done
echo "smoke test passed"
