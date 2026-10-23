#!/bin/bash
# Port build for macOS on Apple Silicon (docs/port.md): native arm64, 64-bit, with Homebrew's SDL3 and the
# system OpenGL: build_port/mac/gta2_port. The Linux build's POSIX layer (Port/compat), no x87.
# usage: Scripts/port/build_linux.sh [extra clang flags]   env: OPT (default -O2), JOBS, FORCE
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
# ENet (Port/net/enet, vendored), plain C
ENET_DEFS="-DHAS_SOCKLEN_T=1 -DHAS_POLL=1 -DHAS_FCNTL=1 -DHAS_MSGHDR_FLAGS=1 -DHAS_GETADDRINFO=1 -DHAS_GETNAMEINFO=1 -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1"
ENETCC="clang ${OPT:--O2} -g -w -IPort/net/enet/include $ENET_DEFS"
SRC=($(sed -n '/set *(gta2_lib_src/,/)/p' CMakeLists.txt | grep -o 'Source/[^ ]*\.cpp' | grep -v 'ImGuiDebug.cpp\|NetPlay.cpp\|NetUi.cpp')
     StdAfx.cpp Port/*.cpp Port/headless/state_log.cpp Port/headless/fast_clock.cpp Port/headless/sim_bench.cpp $(find Port/net -name "*.cpp" -not -path "*/tests/*" | sort) Source/3rdParty/bink.cpp) # bink.cpp: stubs, no movies until M5
REN=Renderers/OpenGL
RENSRC=($REN/core/gl_funcs.cpp $REN/core/gl_renderer.cpp $REN/core/gl_enhanced.cpp $REN/core/vid_system.cpp $REN/core/capture.cpp
        $REN/core/options.cpp $REN/core/gbh_api.cpp $REN/core/cmdlog.cpp $REN/platform/sdl_context.cpp)
# Port/net's sources depend on its headers (the rest still needs FORCE=1 after a header change): the newest one
NETHDR=$(ls -t $(find Port/net -name "*.hpp" -not -path "*/enet/*") | head -1)
OBJS=()
for f in "${SRC[@]}" "${RENSRC[@]}" Port/net/enet/*.c; do
  CC_="clang++"
  case $f in Renderers/*) FL=$RENFLAGS;; Source/3rdParty/bink.cpp) FL="$CXXFLAGS -DBUILD_BINK";; *.c) CC_=$ENETCC; FL="";; *) FL=$CXXFLAGS;; esac
  o=$OUT/obj/$(echo "$f" | tr '/' '_').o
  OBJS+=("$o")
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$FORCE" ] || { [[ $f == Port/net/* ]] && [ "$NETHDR" -nt "$o" ]; }; then
    ( $CC_ $FL -c "$f" -o "$o" 2> "$o.log" || { echo "FAILED: $f"; grep error "$o.log" | head -5; } ) &
  fi
  while [ "$(jobs -r | wc -l)" -ge "${JOBS:-8}" ]; do sleep 0.1; done
done
wait
for o in "${OBJS[@]}"; do [ -f "$o" ] || { echo "compile failed"; exit 1; }; done
# -rdynamic: the game finds the renderer's gbh_*/Vid_* functions in the program with dlsym
clang++ -rdynamic -o $OUT/gta2_port "${OBJS[@]}" -L$BREW/lib -lSDL3 -framework OpenGL 2>&1 | tee $OUT/link.log | head -60
echo "built $OUT/gta2_port"
