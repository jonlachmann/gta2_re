#!/bin/bash
# Test aid (docs/port.md): PR #837's glvideo.dll/glrender.dll built with the port's compiler and flags
# (clang, Scripts/port/build.sh's RENFLAGS) into build_port/gl_clang/, so 10.5.exe renders with the same
# code as gta2_port.exe and frame comparisons only show game differences. Also nullvideo.dll (headless).
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=build_port/gl_clang
mkdir -p $OUT
. Scripts/port/build_common.sh
INC=$(mingw_includes)
FL="--target=i686-w64-mingw32 -std=c++11 ${OPT:--O2} -g -w -nostdinc++ -nostdlibinc $INC -fsjlj-exceptions -DGTA2GL_BUILD_DLL"
R=Renderers/OpenGL
CORE="$R/core/gl_funcs.cpp $R/core/gl_renderer.cpp $R/core/gl_enhanced.cpp $R/core/vid_system.cpp $R/core/capture.cpp $R/core/options.cpp"
build() { # name sources... libs
  local name=$1; shift; local objs=()
  for f in $SOURCES; do o=$OUT/$(echo $f | tr / _).$name.o; clang++ $FL -c $f -o $o; objs+=($o); done
  i686-w64-mingw32-g++ -shared -static -Wl,--kill-at -o $OUT/$name.dll "${objs[@]}" "$@"
}
SOURCES="$R/dll/glvideo.cpp $R/dll/dinput_hook.cpp $R/platform/wgl_context.cpp $CORE" build glvideo -lopengl32 -lgdi32 -ldinput -ldxguid
SOURCES="$R/dll/glrender.cpp $R/core/gbh_api.cpp $R/core/cmdlog.cpp $R/core/capture.cpp" build glrender
# Port/headless/nullvideo.cpp: draws nothing, for fast state comparisons (GTA2_SPEED, GTA2_STATELOG)
SOURCES="Port/headless/nullvideo.cpp Port/headless/state_log.cpp Port/headless/fast_clock.cpp $CORE" build nullvideo
ls -la $OUT/*.dll
