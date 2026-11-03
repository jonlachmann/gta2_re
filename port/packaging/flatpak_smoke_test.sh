#!/bin/sh
# flatpak_smoke_test.sh <bundle> [<GTA2 folder>]: installs the bundle alone on a Debian 13 host (flatpak
# brings its runtime from Flathub), then starts it without game data, as smoke_test.sh does for the
# AppImage. Run as root in debian:trixie, with the docker options of port/README.md (Packages) for bwrap.
# With a GTA2 folder, the setup also installs the game from it, then the game plays a few seconds under
# Xvfb, with the sound written to a file. All of it runs on the app's own Mesa, then again with the
# GL32.default extension installed, which the app must then use.
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
flatpak list
if flatpak list | grep -q -E 'Compat\.i386|GL32'; then
    echo "a 32-bit extension came with the bundle" >&2
    exit 1
fi

T=$(mktemp -d)
DATA=$HOME/.var/app/$ID/data/gta2_re
Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
XVFB=$!
trap 'kill $XVFB' EXIT
export DISPLAY=:99

# run <name> <Mesa folder>: the tests, with Mesa's libgallium loaded from that folder.
run() {
    N=$1
    rm -rf "$DATA"
    GTA2_NO_SOUND=1 GTA2_NO_MESSAGEBOX=1 SDL_VIDEODRIVER=dummy GTA2_LOG_FILE=$T/$N-dummy.log \
        flatpak run --filesystem="$T" "$ID"
    cat "$T/$N-dummy.log"
    grep -q "game files were not found" "$T/$N-dummy.log"
    [ -d "$DATA" ]

    for render in opengl opengles2; do
        C=$T/$N-cap_$render
        mkdir -p "$C"
        rc=0
        GTA2_NO_SOUND=1 SDL_RENDER_DRIVER=$render SDL_VIDEO_FORCE_EGL=$([ $render = opengles2 ] && echo 1 || echo 0) \
        GTA2_SETUP_DRAWN=1 GTA2_SETUP_CAPTURE=$C GTA2_LOG_FILE=$T/$N-$render.log \
            timeout -s KILL 20 flatpak run --filesystem="$T" --env=LD_DEBUG=libs --env=LD_DEBUG_OUTPUT="$T/$N-ld_$render" \
            "$ID" || rc=$?
        cat "$T/$N-$render.log"
        ls -l "$C"
        # Killed by timeout while the window waits for an answer.
        [ "$rc" = 137 ]
        # The question's text and buttons, not a blank window.
        [ "$(od -An -tu1 -v "$C/setup_0.bmp" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 8 ]
        grep -h "calling init: .*/libgallium" "$T/$N-ld_$render".*
        grep -q "calling init: $2/libgallium" "$T/$N-ld_$render".*
    done

    if [ -n "$GAME" ]; then
        C=$T/$N-cap_game
        mkdir -p "$C"
        GTA2_SETUP_PATH=$GAME GTA2_NO_MESSAGEBOX=1 GTA2_LOG_FILE=$T/$N-game.log \
        SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$T/$N-audio.raw \
        GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=$C,850-899 \
            timeout 300 flatpak run --filesystem="$T" --filesystem="$GAME":ro "$ID"
        tail -n 20 "$T/$N-game.log"
        ls -l "$C" "$T/$N-audio.raw"
        [ -f "$DATA/data/fstyle.sty" ]
        # llvmpipe draws a few frames per second: the steps drawn are some of those asked for.
        [ "$(od -An -tu1 -v "$(ls "$C"/*.bmp | tail -n 1)" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 64 ]
        # Not silence.
        [ "$(tr -d '\000' < "$T/$N-audio.raw" | head -c 1 | wc -c)" = 1 ]
    fi
}

run bundled /app/lib32/mesa
flatpak --user install -y --noninteractive flathub org.freedesktop.Platform.GL32.default//26.08
run gl32 /app/lib/i386-linux-gnu/GL/default/lib
echo "flatpak smoke test passed"
