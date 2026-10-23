#!/bin/bash
# Port build (docs/port.md): the decomp_main sources compiled with clang for i686-w64-mingw32 and
# linked with the MinGW-w64 g++ into build_port/gta2_port.exe (a Windows exe; runs under wine).
# usage: Scripts/port/build.sh [extra clang flags]   env: OPT (default -O2 -fno-optimize-sibling-calls: no tail
#   calls, so the call trace of Scripts/port/state_check.py sees every return), JOBS (default 8), FORCE=1 (all objects)
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=build_port
mkdir -p $OUT/obj
INC=$(echo | i686-w64-mingw32-g++ -xc++ -E -v - 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list./p' | grep '^ ' | grep -Ev '/lib/gcc/[^/]+/[^/]+/include(-fixed)?$' | sed 's/^ */-isystem /')
# SDL3 from the official MinGW package (SDL3_DIR, default build_port/sdl3 = its i686-w64-mingw32 folder)
SDL3_DIR=${SDL3_DIR:-$OUT/sdl3}
# ponytail: SDL's headers pull in MinGW's intrin.h, whose __cpuidex clashes with Apple clang's cpuid.h; nothing here
# uses cpuid.h, so its include guard is preset. Drop it with a clang/MinGW pair that agree.
SDLINC="-I$SDL3_DIR/include -D__CPUID_H"
CXXFLAGS="--target=i686-w64-mingw32 -fshort-wchar -std=c++17 ${OPT:--O2 -fno-optimize-sibling-calls} -g -w -Wno-address-of-temporary -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -ftrivial-auto-var-init=zero -fno-strict-return -mno-sse -mfpmath=387 -fsjlj-exceptions -nostdinc++ -nostdlibinc $INC -ISource -I. -IPort/net/enet/include -D_CRT_SECURE_NO_WARNINGS -D_CRT_NON_CONFORMING_SWPRINTFS -D_USE_32BIT_TIME_T -DGTA2_PORT -DNDEBUG -DBUILD_MSS -DSTATELOG_IN_PORT $SDLINC $*"
RENFLAGS="--target=i686-w64-mingw32 -std=c++11 ${OPT:--O2} -g -w -nostdinc++ -nostdlibinc $INC -fsjlj-exceptions -DGTA2GL_BUILD_DLL $SDLINC"
# ENet (Port/net/enet, vendored), plain C, with MinGW's gcc
ENETCC="i686-w64-mingw32-gcc ${OPT:--O2} -g -w -IPort/net/enet/include"
# gta2_lib_src without the ImGui overlay and DirectPlay (NetPlay/NetUi: Port/net/engine/netplay.cpp instead), plus StdAfx.cpp and Port/ (main.cpp replaces test.cpp)
SRC=($(sed -n '/set *(gta2_lib_src/,/)/p' CMakeLists.txt | grep -o 'Source/[^ ]*\.cpp' | grep -v 'ImGuiDebug.cpp\|NetPlay.cpp\|NetUi.cpp') StdAfx.cpp Port/*.cpp Port/headless/state_log.cpp Port/headless/fast_clock.cpp Port/headless/sim_bench.cpp $(find Port/net -name "*.cpp" -not -path "*/tests/*" | sort))
TESTS=(Port/tests/*.cpp)
# The OpenGL renderer (PR #837), linked in: its gbh_* exports and Port/sdl_video.cpp's Vid_* ones are found in the
# exe itself.
REN=Renderers/OpenGL
RENSRC=($REN/core/gl_funcs.cpp $REN/core/gl_renderer.cpp $REN/core/gl_enhanced.cpp $REN/core/vid_system.cpp $REN/core/capture.cpp
        $REN/core/options.cpp $REN/core/gbh_api.cpp $REN/core/cmdlog.cpp $REN/platform/sdl_context.cpp)
# Port/net's sources depend on its headers (the rest still needs FORCE=1 after a header change): the newest one
NETHDR=$(ls -t $(find Port/net -name "*.hpp" -not -path "*/enet/*") | head -1)
OBJS=()
fail=0
for f in "${SRC[@]}" "${RENSRC[@]}" Port/net/enet/*.c; do
  CC_="clang++"
  case $f in Renderers/*) FL=$RENFLAGS;; *.c) CC_=$ENETCC; FL="";; *) FL=$CXXFLAGS;; esac
  o=$OUT/obj/$(echo "$f" | tr '/' '_').o
  OBJS+=("$o")
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$FORCE" ] || { [[ $f == Port/net/* ]] && [ "$NETHDR" -nt "$o" ]; }; then
    ( $CC_ $FL -c "$f" -o "$o" 2> "$o.log" || { echo "FAILED: $f"; cat "$o.log" | grep error | head -5; } ) &
  fi
  while [ "$(jobs -r | wc -l)" -ge "${JOBS:-8}" ]; do sleep 0.1; done
done
wait
for o in "${OBJS[@]}"; do [ -f "$o" ] || fail=1; done
[ $fail -eq 0 ] || { echo "compile failed"; exit 1; }
i686-w64-mingw32-windres -I. resources.rc -O coff -o $OUT/obj/resources.o
# Import library for the game's own binkw32.dll (Miles is Port/miles_sdl.cpp) (from the GTA2 install): their exports are
# decorated stdcall names (_AIL_startup@0), which the "name == export" form of a .def maps to.
for dll in binkw32; do
  { echo "LIBRARY $dll.dll"; echo EXPORTS
    i686-w64-mingw32-objdump -p "${GTA2_DLL_DIR:-$OUT/dlls}/$dll.dll" | sed -n '/Ordinal\/Name Pointer/,/^$/p' | awk '/^[[:space:]]+\[/ {print $NF}' |
      sed -E 's/^_([A-Za-z0-9_]+@[0-9]+)$/\1 == _\1/'; } > $OUT/$dll.def
  i686-w64-mingw32-dlltool -d $OUT/$dll.def -l $OUT/lib$dll.a
done
i686-w64-mingw32-g++ -mwindows -static -Wl,--kill-at -o $OUT/gta2_port.exe "${OBJS[@]}" $OUT/obj/resources.o \
  -L$OUT $SDL3_DIR/lib/libSDL3.dll.a -lbinkw32 -lversion \
  -ldinput -ldxguid -lddraw -lws2_32 -lwinmm -lole32 -luuid -lgdi32 -lcomctl32 -lopengl32 2>&1 | tee $OUT/link.log | head -60
# Port/tests: one exe per test, linked with the Port/ file of the same name
for t in "${TESTS[@]}"; do
  n=$(basename "$t" .cpp); u=${n%_test}
  clang++ $CXXFLAGS -c "$t" -o $OUT/obj/test_$n.o && i686-w64-mingw32-g++ -static -o $OUT/$n.exe $OUT/obj/test_$n.o $OUT/obj/Port_$u.cpp.o $SDL3_DIR/lib/libSDL3.dll.a
done
cp $SDL3_DIR/bin/SDL3.dll $OUT/
echo "built $OUT/gta2_port.exe (needs SDL3.dll next to it)"
