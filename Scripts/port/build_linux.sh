#!/bin/bash
# Port build for 32-bit x86 Linux (docs/port.md), cross-compiled with clang and linked with lld:
# build_port/linux/gta2_port. Needs a Debian i386 sysroot (LINUX_SYSROOT, default build_port/linux_sysroot,
# see docs/port.md) with libc6-dev, libstdc++-14-dev, libgcc-14-dev, libsdl3-dev, libgl-dev, libegl-dev.
# usage: Scripts/port/build_linux.sh [extra clang flags]   env: OPT (default -O2), JOBS, FORCE
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=build_port/linux
R=${LINUX_SYSROOT:-$PWD/build_port/linux_sysroot}
mkdir -p $OUT/obj
COMMON="--target=i686-linux-gnu --sysroot=$R ${OPT:--O2} -g -w -fPIC"
# Game: the same flags as the Windows build (docs/port.md), plus the Windows names from Port/compat
CXXFLAGS="$COMMON -fshort-wchar -std=c++17 -fms-extensions -Wno-address-of-temporary -fno-strict-aliasing -fwrapv
  -fno-delete-null-pointer-checks -ftrivial-auto-var-init=zero -fno-strict-return -mno-sse -mfpmath=387 -IPort/compat -include Port/compat/port_linux.h
  -ISource -I. -D_CRT_SECURE_NO_WARNINGS -D_USE_32BIT_TIME_T -DGTA2_PORT -DNDEBUG -DBUILD_MSS -DSTATELOG_IN_PORT $*"
RENFLAGS="$COMMON -std=c++11"
SRC=($(sed -n '/set *(gta2_lib_src/,/)/p' CMakeLists.txt | grep -o 'Source/[^ ]*\.cpp' | grep -v 'ImGuiDebug.cpp\|NetPlay.cpp\|NetUi.cpp')
     StdAfx.cpp Port/*.cpp Port/headless/state_log.cpp Port/headless/fast_clock.cpp Port/headless/sim_bench.cpp $(find Port/net -name "*.cpp" -not -path "*/tests/*" | sort) Source/3rdParty/bink.cpp) # bink.cpp: stubs, no movies until M5
REN=Renderers/OpenGL
RENSRC=($REN/core/gl_funcs.cpp $REN/core/gl_renderer.cpp $REN/core/gl_enhanced.cpp $REN/core/vid_system.cpp $REN/core/capture.cpp
        $REN/core/options.cpp $REN/core/gbh_api.cpp $REN/core/cmdlog.cpp $REN/platform/sdl_context.cpp)
OBJS=()
for f in "${SRC[@]}" "${RENSRC[@]}"; do
  case $f in Renderers/*) FL=$RENFLAGS;; Source/3rdParty/bink.cpp) FL="$CXXFLAGS -DBUILD_BINK";; *) FL=$CXXFLAGS;; esac
  o=$OUT/obj/$(echo "$f" | tr '/' '_').o
  OBJS+=("$o")
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$FORCE" ]; then
    ( clang++ $FL -c "$f" -o "$o" 2> "$o.log" || { echo "FAILED: $f"; grep error "$o.log" | head -5; } ) &
  fi
  while [ "$(jobs -r | wc -l)" -ge "${JOBS:-8}" ]; do sleep 0.1; done
done
wait
for o in "${OBJS[@]}"; do [ -f "$o" ] || { echo "compile failed"; exit 1; }; done
# --no-dependent-libraries: the game's #pragma comment(lib, ...) name Windows libraries
# -rdynamic: the game finds the renderer's gbh_*/Vid_* functions in the program with dlsym
clang++ --target=i686-linux-gnu --sysroot=$R -fuse-ld=lld -rdynamic -Wl,--no-dependent-libraries -Wl,--error-limit=0 -o $OUT/gta2_port "${OBJS[@]}" \
  -L$R/usr/lib/i386-linux-gnu -lSDL3 -ldl -lpthread -lm 2>&1 | tee $OUT/link.log | head -60
echo "built $OUT/gta2_port"
