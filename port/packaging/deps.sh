#!/bin/sh
# deps.sh <linux32|mingw32> <downloads> <prefix> <build dir>: SDL3, OpenAL Soft, unshield and libarchive
# (and for MinGW zlib and xz) from the sources of versions.sh, installed into <prefix>.
# linux32 runs in an i386 userland (i386/debian), so the libraries build natively.
set -eu
T=$1; DL=$2; P=$3; B=$4
PORT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$P" "$B"
J=$(nproc)

unpack() { tar -xf "$DL/$1" -C "$B"; }

if [ "$T" = linux32 ]; then
    C="-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH=$P
       -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    unpack SDL3-3.4.18.tar.gz
    cmake -S "$B/SDL3-3.4.18" -B "$B/b_sdl3" $C -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
          -DSDL_INSTALL_DOCS=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF
    ninja -C "$B/b_sdl3" -j"$J" install
else
    C="-G Ninja -DCMAKE_TOOLCHAIN_FILE=$PORT/toolchain-mingw32-clang.cmake -DCMAKE_BUILD_TYPE=Release
       -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_PREFIX_PATH=$P"
    unpack SDL3-devel-3.4.18-mingw.tar.gz
    mkdir -p "$P/sdl3"
    cp -r "$B/SDL3-3.4.18/i686-w64-mingw32/." "$P/sdl3/"
    cp "$B/SDL3-3.4.18/LICENSE.txt" "$P/sdl3/"
    unpack zlib-1.3.2.tar.gz
    cmake -S "$B/zlib-1.3.2" -B "$B/b_zlib" $C -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_TESTING=OFF
    ninja -C "$B/b_zlib" -j"$J" install
    unpack xz-5.8.4.tar.gz
    cmake -S "$B/xz-5.8.4" -B "$B/b_xz" $C -DBUILD_SHARED_LIBS=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF \
          -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_TOOL_SYMLINKS=OFF \
          -DXZ_TOOL_SYMLINKS_LZMA=OFF -DXZ_DOC=OFF -DXZ_NLS=OFF -DXZ_THREADS=no -DXZ_ASM_I386=OFF -DBUILD_TESTING=OFF
    ninja -C "$B/b_xz" -j"$J" install
    C="$C -DZLIB_LIBRARY=$P/lib/libzs.a -DZLIB_INCLUDE_DIR=$P/include -DLIBLZMA_LIBRARY=$P/lib/liblzma.a
       -DLIBLZMA_INCLUDE_DIR=$P/include -DCMAKE_C_FLAGS=-DLZMA_API_STATIC"
fi

# OpenAL Soft: no audio backend, it renders into SDL's mix through its loopback device.
AL="-DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_UPDATE_BUILD_VERSION=OFF
    -DALSOFT_INSTALL=ON -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF
    -DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_BACKEND_PIPEWIRE=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF
    -DALSOFT_BACKEND_ALSA=OFF -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_JACK=OFF
    -DALSOFT_BACKEND_PORTAUDIO=OFF -DALSOFT_BACKEND_SDL2=OFF -DALSOFT_BACKEND_SDL3=OFF -DALSOFT_BACKEND_WASAPI=OFF
    -DALSOFT_BACKEND_DSOUND=OFF -DALSOFT_BACKEND_WINMM=OFF -DALSOFT_BACKEND_WAVE=OFF -DALSOFT_ENABLE_MODULES=OFF
    -DHAVE_WFUNCTION_EFFECTS=OFF"
unpack openal-soft-1.25.2.tar.gz
# GCC: OpenAL Soft 1.25.2 builds neither with Debian 13's clang 19 nor with libc++ (llvm-mingw). OpenAL32.dll
# links its C and C++ runtimes statically and exchanges no CRT object with the game.
AL_C="-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_INSTALL_LIBDIR=lib"
if [ "$T" = linux32 ]; then
    cmake -S "$B/openal-soft-1.25.2" -B "$B/b_openal" $AL_C $AL -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
else
    cmake -S "$B/openal-soft-1.25.2" -B "$B/b_openal" $AL_C $AL -DALSOFT_EAX=OFF -DCMAKE_SYSTEM_NAME=Windows \
          -DCMAKE_SYSTEM_PROCESSOR=i686 -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix \
          -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix -DCMAKE_RC_COMPILER=i686-w64-mingw32-windres \
          "-DCMAKE_SHARED_LINKER_FLAGS=-static -static-libgcc -static-libstdc++"
fi
ninja -C "$B/b_openal" -j"$J" install

unpack unshield-1.6.2.tar.gz
cmake -S "$B/unshield-1.6.2" -B "$B/b_unshield" $C -DBUILD_STATIC=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON
ninja -C "$B/b_unshield" -j"$J" install
# unshield's static library leaves out its own MD5 objects.
llvm-ar rs "$P/lib/libunshield.a" "$B"/b_unshield/lib/md5/CMakeFiles/md5.dir/md5c.c.o*

unpack libarchive-3.8.11.tar.xz
cmake -S "$B/libarchive-3.8.11" -B "$B/b_libarchive" $C -DBUILD_SHARED_LIBS=OFF -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF \
      -DENABLE_NETTLE=OFF -DENABLE_MD=OFF -DENABLE_LIBB2=OFF -DENABLE_LZ4=OFF -DENABLE_ZSTD=OFF -DENABLE_BZip2=OFF \
      -DENABLE_LIBXML2=OFF -DENABLE_BSDXML=OFF -DENABLE_EXPAT=OFF -DENABLE_WIN32_XMLLITE=OFF -DENABLE_PCREPOSIX=OFF \
      -DENABLE_PCRE2POSIX=OFF -DENABLE_LIBGCC=OFF -DENABLE_CNG=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF \
      -DENABLE_UNZIP=OFF -DENABLE_XATTR=OFF -DENABLE_ACL=OFF -DENABLE_ICONV=OFF -DENABLE_TEST=OFF -DPOSIX_REGEX_LIB=NONE
ninja -C "$B/b_libarchive" -j"$J" install

# The licences shipped with the packages.
mkdir -p "$P/licenses"
cp "$B/openal-soft-1.25.2/COPYING" "$P/licenses/OpenAL-Soft.txt"
cp "$B/unshield-1.6.2/LICENSE" "$P/licenses/unshield.txt"
cp "$B/libarchive-3.8.11/COPYING" "$P/licenses/libarchive.txt"
if [ "$T" = linux32 ]; then
    cp "$B/SDL3-3.4.18/LICENSE.txt" "$P/licenses/SDL3.txt"
else
    cp "$P/sdl3/LICENSE.txt" "$P/licenses/SDL3.txt"
    cp "$B/zlib-1.3.2/LICENSE" "$P/licenses/zlib.txt"
    cat "$B/xz-5.8.4/COPYING" "$B/xz-5.8.4/COPYING.0BSD" > "$P/licenses/xz.txt"
fi
touch "$P/.complete"
