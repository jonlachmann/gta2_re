#!/bin/sh
# flatpak_smoke_test.sh <bundle> [<GTA2 folder>]: installs the bundle alone (flatpak brings its runtime from
# Flathub) and starts it without game data: with SDL's dummy video driver the setup finds no data and quits;
# on the X display its window is captured, drawn with OpenGL and OpenGL ES. With a GTA2 folder, the setup also
# installs the game from it, then the game plays 30 seconds, with the sound written to a file.
# As root in debian:trixie (with the docker options of port/README.md, Packages, for bwrap) it installs flatpak
# and Xvfb and runs as a new user; as a normal user it needs DISPLAY and a session bus, and installs per user.
set -eu
if [ "$(id -u)" = 0 ]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends flatpak dbus-daemon dbus-system-bus-common xvfb xauth \
        ca-certificates > /dev/null
    # flatpak install asks the system bus for parental controls.
    mkdir -p /run/dbus
    dbus-daemon --system --fork
    mkdir -p -m 1777 /tmp/.X11-unix
    # bwrap run by root would need CAP_SYS_ADMIN.
    useradd -m player
    Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
    exec runuser -u player -- env HOME=/home/player DISPLAY=:99 dbus-run-session sh "$0" "$@"
fi
B=$(readlink -f "$1")
GAME=${2:+$(readlink -f "$2")}
. "$(dirname "$0")/app.sh"
ID=$APP_ID
flatpak info org.freedesktop.Platform//26.08 > /dev/null 2>&1 ||
    flatpak --user remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak --user install -y --noninteractive --bundle "$B"
flatpak list --app

T=$(mktemp -d)
DATA=$HOME/.var/app/$ID/data/gta2_re
GTA2_NO_SOUND=1 GTA2_NO_MESSAGEBOX=1 SDL_VIDEODRIVER=dummy GTA2_LOG_FILE=$T/dummy.log \
    flatpak run --filesystem="$T" "$ID"
cat "$T/dummy.log"
grep -q "game files were not found" "$T/dummy.log"
[ -d "$DATA" ]

for render in opengl opengles2; do
    C=$T/cap_$render
    mkdir -p "$C"
    rc=0
    GTA2_NO_SOUND=1 SDL_RENDER_DRIVER=$render SDL_VIDEO_FORCE_EGL=$([ $render = opengles2 ] && echo 1 || echo 0) \
    GTA2_SETUP_DRAWN=1 GTA2_SETUP_CAPTURE=$C GTA2_LOG_FILE=$T/$render.log \
        timeout -s KILL 20 flatpak run --filesystem="$T" "$ID" || rc=$?
    cat "$T/$render.log"
    ls -l "$C"
    # Killed by timeout while the window waits for an answer.
    [ "$rc" = 137 ]
    # The question's text and buttons, not a blank window.
    [ "$(od -An -tu1 -v "$C/setup_0.bmp" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 8 ]
done

if [ -n "$GAME" ]; then
    C=$T/cap_game
    mkdir -p "$C"
    GTA2_SETUP_PATH=$GAME GTA2_NO_MESSAGEBOX=1 GTA2_LOG_FILE=$T/game.log \
    SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$T/audio.raw \
    GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=$C,850-899 \
        timeout 300 flatpak run --filesystem="$T" --filesystem="$GAME":ro "$ID"
    tail -n 20 "$T/game.log"
    ls -l "$C" "$T/audio.raw"
    [ -f "$DATA/data/fstyle.sty" ]
    # llvmpipe draws a few frames per second: the steps drawn are some of those asked for.
    [ "$(od -An -tu1 -v "$(ls "$C"/*.bmp | tail -n 1)" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 64 ]
    # Not silence.
    [ "$(tr -d '\000' < "$T/audio.raw" | head -c 1 | wc -c)" = 1 ]
fi
echo "flatpak smoke test passed ($T)"
