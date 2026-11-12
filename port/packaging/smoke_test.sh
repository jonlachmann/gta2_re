#!/bin/sh
# smoke_test.sh <package> [<GTA2 folder>]: installs a Linux package (reG2-*-linux-i686.tar.gz or
# reG2-*-linux-x86_64.tar.gz) and the libraries it needs (the lists of linux/README.md)
# as root in a clean Debian, Ubuntu or Arch Linux container, then starts the game without its data: with SDL's
# dummy video driver the setup finds no data and quits; under Xvfb its window is captured, drawn with OpenGL and
# with OpenGL ES. With a GTA2 folder (read-only is fine), the setup installs the game from it, then the game
# plays 30 seconds under Xvfb, with the sound written to a file.
set -eu
PKG=$(readlink -f "$1")
GAME=${2:+$(readlink -f "$2")}
T=$(mktemp -d)
export DEBIAN_FRONTEND=noninteractive
case $PKG in *i686*) I386=1 ;; *) I386= ;; esac

if command -v apt-get > /dev/null; then
    [ -z "$I386" ] || dpkg --add-architecture i386
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends xvfb xauth > /dev/null
else
    [ -z "$I386" ] || printf '[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf
    pacman -Syu --noconfirm --needed xorg-server-xvfb > /dev/null
fi

tar -xzf "$PKG" -C "$T"
GTA2=$(echo "$T"/reG2-*/gta2)
if [ -n "$I386" ]; then
    rc=0
    "$GTA2" 2> "$T/no32.log" || rc=$?
    cat "$T/no32.log"
    [ $rc = 1 ] && grep -q "no 32-bit libraries" "$T/no32.log"
fi
if command -v apt-get > /dev/null; then
    LIBS="libc6 libgcc-s1 libgl1 libegl1 libgles2 libgl1-mesa-dri libx11-6 libxext6 libxi6 libxcursor1
          libxrandr2 libxss1 libxkbcommon0 libwayland-client0 libwayland-cursor0 libwayland-egl1 libpulse0
          libdbus-1-3 libudev1"
    # Debian 11 has no libdecor.
    apt-cache show libdecor-0-0 > /dev/null 2>&1 && LIBS="$LIBS libdecor-0-0"
    apt-get install -y -qq $(printf "%s${I386:+:i386} " $LIBS) > /dev/null
else
    LIBS="glibc gcc-libs mesa libglvnd libx11 libxext libxi libxcursor libxrandr libxss libxkbcommon wayland libdecor
          libpulse dbus systemd-libs"
    [ -z "$I386" ] || LIBS=$(printf 'lib32-%s ' $LIBS | sed 's/lib32-systemd-libs/lib32-systemd/')
    pacman -S --noconfirm --needed $LIBS > /dev/null
fi

export HOME=$T/home XDG_DATA_HOME=$T/data
DATA=$XDG_DATA_HOME/gta2_re
mkdir -p "$HOME"

GTA2_NO_SOUND=1 GTA2_NO_MESSAGEBOX=1 SDL_VIDEODRIVER=dummy GTA2_LOG_FILE=$T/dummy.log "$GTA2" 2> "$T/dummy.err"
cat "$T/dummy.log" "$T/dummy.err"
grep -q "game files were not found" "$T/dummy.log"
if grep -q "libraries not found" "$T/dummy.err"; then exit 1; fi
[ -d "$DATA" ]

Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
XVFB=$!
trap 'kill $XVFB' EXIT
export DISPLAY=:99
for render in opengl opengles2; do
    C=$T/cap_$render
    mkdir -p "$C"
    rc=0
    GTA2_NO_SOUND=1 SDL_RENDER_DRIVER=$render SDL_VIDEO_FORCE_EGL=$([ $render = opengles2 ] && echo 1 || echo 0) \
    GTA2_SETUP_DRAWN=1 GTA2_SETUP_CAPTURE=$C GTA2_LOG_FILE=$T/$render.log \
        timeout -s KILL 20 "$GTA2" || rc=$?
    cat "$T/$render.log"
    ls -l "$C"
    # Killed by timeout while the window waits for an answer.
    [ "$rc" = 137 ]
    # The question's text and buttons, not a blank window.
    [ "$(od -An -tu1 -v "$C/setup_0.bmp" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 8 ]
    cp "$C/setup_0.bmp" "$T/setup_$render.bmp"
done

if [ -n "$GAME" ]; then
    C=$T/cap_game
    mkdir -p "$C"
    GTA2_SETUP_PATH=$GAME GTA2_NO_MESSAGEBOX=1 GTA2_LOG_FILE=$T/game.log \
    SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=$T/audio.raw \
    GTA2_INPUT=100:Return,160:Return GTA2_EXIT_AT=900 GTA2GL_CAPTURE=$C,850-899 \
        timeout 300 "$GTA2"
    tail -n 20 "$T/game.log"
    ls -l "$C" "$T/audio.raw"
    [ -f "$DATA/data/fstyle.sty" ]
    # llvmpipe draws a few frames per second: the steps drawn are some of those asked for.
    [ "$(od -An -tu1 -v "$(ls "$C"/*.bmp | tail -n 1)" | tr -s ' ' '\n' | sort -u | wc -l)" -gt 64 ]
    # Not silence.
    [ "$(tr -d '\000' < "$T/audio.raw" | head -c 1 | wc -c)" = 1 ]
    cp "$(ls "$C"/*.bmp | tail -n 1)" "$T/game.bmp"
fi

echo "smoke test passed ($T)"
