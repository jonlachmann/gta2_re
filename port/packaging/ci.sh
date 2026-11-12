#!/bin/sh
# ci.sh <linux32|linux64|mingw32|mingw64> <work dir>: one packaging job, run as root in a Debian 13 container
# (i386/debian:trixie for linux32, debian:trixie for the others), from the repository's root.
# <work dir>/dl holds the downloads and <work dir>/deps-<target> the libraries: both can be cached.
set -eu
T=$1; W=$2
HERE=$(cd "$(dirname "$0")" && pwd)
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
COMMON="cmake ninja-build pkg-config curl ca-certificates xz-utils file"
case $T in
    # The headers and libraries come from the system root that deps.sh makes with mmdebstrap.
    linux32) PKGS="clang lld llvm g++ mmdebstrap gpg debian-archive-keyring" ;;
    linux64) PKGS="clang lld llvm g++ mmdebstrap gpg debian-archive-keyring squashfs-tools" ;;
    # GCC for OpenAL Soft (deps.sh); the rest builds with llvm-mingw.
    mingw32) PKGS="zip g++-mingw-w64-i686-posix" ;;
    mingw64) PKGS="zip g++-mingw-w64-x86-64-posix" ;;
    *) echo "ci.sh: unknown target $T" >&2; exit 1 ;;
esac
apt-get install -y -qq --no-install-recommends $COMMON $PKGS > /dev/null

SRC="openal-soft-1.25.2.tar.gz unshield-1.6.2.tar.gz libarchive-3.8.11.tar.xz zlib-1.3.2.tar.gz xz-5.8.4.tar.gz"
case $T in
    linux32) sh "$HERE/fetch.sh" "$W/dl" $SRC SDL3-3.4.18.tar.gz ;;
    linux64) sh "$HERE/fetch.sh" "$W/dl" $SRC SDL3-3.4.18.tar.gz appimage-runtime-x86_64 ;;
    mingw*)
        sh "$HERE/fetch.sh" "$W/dl" $SRC SDL3-devel-3.4.18-mingw.tar.gz llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz
        # llvm-mingw: clang with MinGW-w64 on UCRT, as the toolchain file expects it in PATH.
        rm -rf "$W/llvm-mingw"
        mkdir -p "$W/llvm-mingw"
        tar -xf "$W/dl/llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz" -C "$W/llvm-mingw" --strip-components=1
        export PATH="$W/llvm-mingw/bin:$PATH"
        ;;
esac
if [ ! -f "$W/deps-$T/.complete" ]; then
    rm -rf "$W/deps-$T" "$W/b_deps-$T"
    sh "$HERE/deps.sh" "$T" "$W/dl" "$W/deps-$T" "$W/b_deps-$T"
    rm -rf "$W/b_deps-$T"
fi
sh "$HERE/build.sh" "$T" "$W/deps-$T" "$W/build-$T"
mkdir -p "$W/out"
case $T in
    linux32) sh "$HERE/linux_packages.sh" "$W/build-$T" "$W/deps-$T" "$W/out" ;;
    linux64)
        sh "$HERE/linux_packages.sh" "$W/build-$T" "$W/deps-$T" "$W/out"
        sh "$HERE/appimage.sh" "$W/build-$T" "$W/deps-$T" "$W/dl/appimage-runtime-x86_64" "$W/out"
        ;;
    mingw*) sh "$HERE/windows_zip.sh" "$W/build-$T" "$W/deps-$T" "$W/out" ;;
esac
