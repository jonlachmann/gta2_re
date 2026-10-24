#!/bin/bash
# Port build for macOS on Apple Silicon (docs/port.md): native arm64, 64-bit, with Homebrew's SDL3 and the
# system OpenGL: build_port/mac/gta2_port. The Linux build's POSIX layer (Port/compat), no x87.
# usage: Scripts/port/build_mac.sh [extra clang flags]   env: OPT (default -O2), JOBS, FORCE
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=${OUT:-build_port/mac} # OUT=build_port/mac_x4 Scripts/port/build_mac.sh -DGTA2_POOL_SCALE=4: a second build next to it
BREW=${BREW:-/opt/homebrew}
mkdir -p $OUT/obj
COMMON="${OPT:--O2} -g -w -I$BREW/include -DGL_SILENCE_DEPRECATION"
# Game: the same flags as the Windows build (docs/port.md), plus the Windows names from Port/compat
CXXFLAGS="$COMMON -fshort-wchar -std=c++17 -fms-extensions -Wno-address-of-temporary -fno-strict-aliasing -fwrapv
  -fno-delete-null-pointer-checks -ftrivial-auto-var-init=zero -fno-strict-return -IPort/compat -include Port/compat/port_linux.h
  -ISource -I. -IPort/net/enet/include -D_CRT_SECURE_NO_WARNINGS -D_USE_32BIT_TIME_T -DGTA2_PORT -DNDEBUG -DBUILD_MSS -DSTATELOG_IN_PORT $*"
RENFLAGS="$COMMON -std=c++11"
. Scripts/port/build_common.sh
ENETCC="clang ${OPT:--O2} -g -w -IPort/net/enet/include $ENET_DEFS"
compile $OUT/obj "${GAME_SRC[@]}" Source/3rdParty/bink.cpp "${RENSRC[@]}" "${ENET_SRC[@]}" # bink.cpp: stubs, no movies until M5
# -rdynamic: the game finds the renderer's gbh_*/Vid_* functions in the program with dlsym
clang++ -rdynamic -o $OUT/gta2_port "${OBJS[@]}" -L$BREW/lib -lSDL3 -framework OpenGL 2>&1 | tee $OUT/link.log | head -60
echo "built $OUT/gta2_port"
