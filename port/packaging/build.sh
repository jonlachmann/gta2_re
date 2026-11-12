#!/bin/sh
# build.sh <linux32|linux64|mingw32|mingw64|macos> <deps prefix> <build dir>: the port, with the libraries deps.sh
# installed. The Linux builds link against the system root of deps.sh, with the C++ runtime static, and find
# libSDL3.so.0 next to themselves (linux64 also in ../lib, the AppImage's layout). macos makes reG2.app.
set -eu
T=$1; P=$2; B=$3
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/.." && pwd)
case $T in
    linux32 | linux64)
        export GTA2_SYSROOT="$P/sysroot"
        RPATH='$ORIGIN'
        [ "$T" = linux64 ] && RPATH='$ORIGIN:$ORIGIN/../lib'
        set -- -DCMAKE_TOOLCHAIN_FILE="$HERE/$T-sysroot.cmake" -DCMAKE_PREFIX_PATH="$P" -DSDL3_DIR="$P/lib/cmake/SDL3" \
            -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON "-DCMAKE_INSTALL_RPATH=$RPATH" \
            "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc -Wl,--disable-new-dtags -Wl,-z,now"
        ;;
    mingw32 | mingw64)
        set -- -DCMAKE_TOOLCHAIN_FILE="$PORT/toolchain-$T-clang.cmake" -DSDL3_DIR="$P/sdl3/lib/cmake/SDL3"
        ;;
    macos) set -- -DCMAKE_PREFIX_PATH="$P" -DSDL3_DIR="$P/lib/cmake/SDL3" ;;
    *) echo "build.sh: unknown target $T" >&2; exit 1 ;;
esac
cmake -S "$PORT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" -DGTA2_SETUP_DEPS_DIR="$P" -DGTA2_OPENAL_DIR="$P"
ninja -C "$B" -j"$(getconf _NPROCESSORS_ONLN)"
