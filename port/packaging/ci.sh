#!/bin/sh
# ci.sh <linux32|mingw32> <work dir>: one packaging job, run as root in a Debian 13 container
# (i386/debian:trixie for linux32, debian:trixie for mingw32), from the repository's root.
# <work dir>/dl holds the downloads and <work dir>/deps-<target> the libraries: both can be cached.
set -eu
T=$1; W=$2
HERE=$(cd "$(dirname "$0")" && pwd)
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
COMMON="cmake ninja-build pkg-config curl ca-certificates xz-utils file"
if [ "$T" = linux32 ]; then
    # SDL3's build headers and the static libraries' compression libraries.
    apt-get install -y -qq --no-install-recommends $COMMON clang clang-tools lld llvm g++ \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgles-dev libgl-dev \
        libpulse-dev libasound2-dev libpipewire-0.3-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev \
        zlib1g-dev liblzma-dev > /dev/null
else
    # GCC for OpenAL Soft (deps.sh); the rest builds with llvm-mingw.
    apt-get install -y -qq --no-install-recommends $COMMON zip g++-mingw-w64-i686-posix > /dev/null
fi

SRC="openal-soft-1.25.2.tar.gz unshield-1.6.2.tar.gz libarchive-3.8.11.tar.xz"
if [ "$T" = linux32 ]; then
    sh "$HERE/fetch.sh" "$W/dl" $SRC SDL3-3.4.18.tar.gz
else
    sh "$HERE/fetch.sh" "$W/dl" $SRC SDL3-devel-3.4.18-mingw.tar.gz zlib-1.3.2.tar.gz xz-5.8.4.tar.gz \
        llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz
    # llvm-mingw: clang with MinGW-w64 on UCRT, as the toolchain file expects it in PATH.
    rm -rf "$W/llvm-mingw"
    mkdir -p "$W/llvm-mingw"
    tar -xf "$W/dl/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz" -C "$W/llvm-mingw" --strip-components=1
    export PATH="$W/llvm-mingw/bin:$PATH"
fi
if [ ! -f "$W/deps-$T/.complete" ]; then
    rm -rf "$W/deps-$T" "$W/b_deps-$T"
    sh "$HERE/deps.sh" "$T" "$W/dl" "$W/deps-$T" "$W/b_deps-$T"
    rm -rf "$W/b_deps-$T"
fi
sh "$HERE/build.sh" "$T" "$W/deps-$T" "$W/build-$T"
mkdir -p "$W/out"
if [ "$T" = mingw32 ]; then
    sh "$HERE/windows_zip.sh" "$W/build-$T" "$W/deps-$T" "$W/out"
fi
