#!/bin/bash
# Port build (docs/port.md): the decomp_main sources compiled with clang for i686-w64-mingw32 and
# linked with the MinGW-w64 g++ into build_port/gta2_port.exe (a Windows exe; runs under wine).
# usage: Scripts/port/build.sh [extra clang flags]   env: OPT (default -O2), JOBS (default 8)
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=build_port
mkdir -p $OUT/obj
INC=$(echo | i686-w64-mingw32-g++ -xc++ -E -v - 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list./p' | grep '^ ' | grep -Ev '/lib/gcc/[^/]+/[^/]+/include(-fixed)?$' | sed 's/^ */-isystem /')
CXXFLAGS="--target=i686-w64-mingw32 -fshort-wchar -std=c++17 ${OPT:--O2} -g -w -Wno-address-of-temporary -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -ftrivial-auto-var-init=zero -fsjlj-exceptions -nostdinc++ -nostdlibinc $INC -ISource -I. -D_CRT_SECURE_NO_WARNINGS -D_CRT_NON_CONFORMING_SWPRINTFS -D_USE_32BIT_TIME_T -DGTA2_PORT $*"
RENFLAGS="--target=i686-w64-mingw32 -std=c++11 ${OPT:--O2} -g -w -nostdinc++ -nostdlibinc $INC -fsjlj-exceptions -DGTA2GL_BUILD_DLL"
# gta2_lib_src without the ImGui overlay, plus StdAfx.cpp and Port/ (main.cpp replaces test.cpp)
SRC=($(sed -n '/set *(gta2_lib_src/,/)/p' CMakeLists.txt | grep -o 'Source/[^ ]*\.cpp' | grep -v ImGuiDebug.cpp) StdAfx.cpp Port/*.cpp)
# The OpenGL renderer (PR #837), linked in: its gbh_*/Vid_* exports are found in the exe itself
REN=Renderers/OpenGL
RENSRC=($REN/core/gl_funcs.cpp $REN/core/gl_renderer.cpp $REN/core/gl_enhanced.cpp $REN/core/vid_system.cpp $REN/core/capture.cpp
        $REN/core/options.cpp $REN/core/gbh_api.cpp $REN/core/cmdlog.cpp $REN/dll/glvideo.cpp $REN/dll/dinput_hook.cpp $REN/platform/wgl_context.cpp)
OBJS=()
fail=0
for f in "${SRC[@]}" "${RENSRC[@]}"; do
  case $f in Renderers/*) FL=$RENFLAGS;; *) FL=$CXXFLAGS;; esac
  o=$OUT/obj/$(echo "$f" | tr '/' '_').o
  OBJS+=("$o")
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$FORCE" ]; then
    ( clang++ $FL -c "$f" -o "$o" 2> "$o.log" || { echo "FAILED: $f"; cat "$o.log" | grep error | head -5; } ) &
  fi
  while [ "$(jobs -r | wc -l)" -ge "${JOBS:-8}" ]; do sleep 0.1; done
done
wait
for o in "${OBJS[@]}"; do [ -f "$o" ] || fail=1; done
[ $fail -eq 0 ] || { echo "compile failed"; exit 1; }
i686-w64-mingw32-windres -I. resources.rc -O coff -o $OUT/obj/resources.o
# Import libraries for the game's own mss32.dll and binkw32.dll (from the GTA2 install): their exports are
# decorated stdcall names (_AIL_startup@0), which the "name == export" form of a .def maps to.
for dll in mss32 binkw32; do
  { echo "LIBRARY $dll.dll"; echo EXPORTS
    i686-w64-mingw32-objdump -p "${GTA2_DLL_DIR:-$OUT/dlls}/$dll.dll" | sed -n '/Ordinal\/Name Pointer/,/^$/p' | awk '/^[[:space:]]+\[/ {print $NF}' |
      sed -E 's/^_([A-Za-z0-9_]+@[0-9]+)$/\1 == _\1/'; } > $OUT/$dll.def
  i686-w64-mingw32-dlltool -d $OUT/$dll.def -l $OUT/lib$dll.a
done
i686-w64-mingw32-g++ -mwindows -static -Wl,--kill-at -o $OUT/gta2_port.exe "${OBJS[@]}" $OUT/obj/resources.o \
  -L$OUT -lmss32 -lbinkw32 -lversion \
  -ldplayx -ldinput -ldxguid -lddraw -lwinmm -lole32 -luuid -lgdi32 -lcomctl32 -lopengl32 2>&1 | tee $OUT/link.log | head -60
echo "built $OUT/gta2_port.exe"
