#!/bin/sh
# build.sh <linux32|mingw32> <deps prefix> <build dir>: the port, with the libraries deps.sh installed.
# linux32 links against the system root of deps.sh, with the C++ runtime static, and finds libSDL3.so.0
# next to itself.
set -eu
T=$1; P=$2; B=$3
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/.." && pwd)
if [ "$T" = linux32 ]; then
    export GTA2_SYSROOT="$P/sysroot"
    set -- -DCMAKE_TOOLCHAIN_FILE="$HERE/linux32-sysroot.cmake" -DCMAKE_PREFIX_PATH="$P" -DSDL3_DIR="$P/lib/cmake/SDL3" \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN' \
        "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc -Wl,--disable-new-dtags -Wl,-z,now"
else
    set -- -DCMAKE_TOOLCHAIN_FILE="$PORT/toolchain-mingw32-clang.cmake" -DSDL3_DIR="$P/sdl3/lib/cmake/SDL3"
fi
cmake -S "$PORT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" -DGTA2_SETUP_DEPS_DIR="$P" -DGTA2_OPENAL_DIR="$P"
ninja -C "$B" -j"$(nproc)"
