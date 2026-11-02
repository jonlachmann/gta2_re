#!/bin/sh
# build.sh <linux32|mingw32> <deps prefix> <build dir>: the port, with the libraries deps.sh installed.
set -eu
T=$1; P=$2; B=$3
PORT=$(cd "$(dirname "$0")/.." && pwd)
if [ "$T" = linux32 ]; then
    TC=$PORT/toolchain-linux32-clang.cmake
    SDL3_DIR=$P/lib/cmake/SDL3
else
    TC=$PORT/toolchain-mingw32-clang.cmake
    SDL3_DIR=$P/sdl3/lib/cmake/SDL3
fi
cmake -S "$PORT" -B "$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSDL3_DIR="$SDL3_DIR" -DGTA2_SETUP_DEPS_DIR="$P" -DGTA2_OPENAL_DIR="$P"
ninja -C "$B" -j"$(nproc)"
