#!/bin/sh
# flatpak_smoke_test.sh <bundle> [<GTA2 folder>]: installs the bundle and its runtime from Flathub on a
# Debian 13 host, then starts it without game data, as smoke_test.sh does for the AppImage. Run as root in
# debian:trixie, with the docker options of port/README.md (Packages) for bwrap. With a GTA2 folder, the
# setup also installs the game from it, then the game plays a few seconds under Xvfb, with Mesa from the
# GL32 extension and the sound written to a file.
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
    exec runuser -u player -- env HOME=/home/player dbus-run-session sh "$0" "$@"
fi
B=$(readlink -f "$1")
GAME=${2:+$(readlink -f "$2")}
ID=io.github.gta2re.gta2
flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak --user install -y --noninteractive --bundle "$B"
# A bundle brings no extension: what a user installs by hand (port/README.md, Packages).
flatpak --user install -y --noninteractive flathub org.freedesktop.Platform.Compat.i386//26.08 \
    org.freedesktop.Platform.GL32.default//26.08

T=$(mktemp -d)
DATA=$HOME/.var/app/$ID/data/gta2_re
export GTA2_NO_SOUND=1

GTA2_NO_MESSAGEBOX=1 SDL_VIDEODRIVER=dummy GTA2_LOG_FILE=$T/dummy.log flatpak run --filesystem="$T" "$ID"
cat "$T/dummy.log"
grep -q "game files were not found" "$T/dummy.log"
[ -d "$DATA" ]

Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
XVFB=$!
trap 'kill $XVFB' EXIT
export DISPLAY=:99
for render in opengl opengles2; do
    mkdir -p "$T/cap_$render"
    rc=0
    SDL_RENDER_DRIVER=$render SDL_VIDEO_FORCE_EGL=$([ $render = opengles2 ] && echo 1 || echo 0) \
    GTA2_SETUP_DRAWN=1 GTA2_SETUP_CAPTURE=$T/cap_$render GTA2_LOG_FILE=$T/$render.log \
        timeout -s KILL 20 flatpak run --filesystem="$T" "$ID" || rc=$?
    cat "$T/$render.log"
    ls -l "$T/cap_$render"
    # Killed by timeout while the window waits for an answer.
    [ "$rc" = 137 ]
    # The question's text and buttons, not a blank window.
    [ "$(od -An -tu1 -v "$T/cap_$render/setup_0.bmp" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 8 ]
done

if [ -n "$GAME" ]; then
    unset GTA2_NO_SOUND
    mkdir -p "$T/cap_game"
    GTA2_SETUP_PATH=$GAME GTA2_NO_MESSAGEBOX=1 GTA2_LOG_FILE=$T/game.log \
    SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$T/audio.raw \
    GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=$T/cap_game,850-899 \
        timeout 300 flatpak run --filesystem="$T" --filesystem="$GAME":ro "$ID"
    tail -n 20 "$T/game.log"
    ls -l "$T/cap_game" "$T/audio.raw"
    [ -f "$DATA/data/fstyle.sty" ]
    # llvmpipe draws a few frames per second: the steps drawn are some of those asked for.
    [ "$(od -An -tu1 -v "$(ls "$T"/cap_game/*.bmp | tail -n 1)" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 64 ]
    # Not silence.
    [ "$(tr -d '\000' < "$T/audio.raw" | head -c 1 | wc -c)" = 1 ]
fi
echo "flatpak smoke test passed"
