cmake_minimum_required(VERSION 3.20)
project(gta2_port C CXX)

# Fork build: one executable from the game sources, clang, 32-bit.
#   Windows (MinGW): cmake -S port -B build_port -G Ninja -DCMAKE_TOOLCHAIN_FILE=port/toolchain-mingw32-clang.cmake
#   Linux:           cmake -S port -B build_linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=port/toolchain-linux32-clang.cmake

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# The game sources are the VC6 build's gta2_lib_src list, so upstream additions come along on merges.
file(READ ${ROOT}/CMakeLists.txt top)
string(REGEX MATCH "set\\(gta2_lib_src\n([^)]*)\\)" _ "${top}")
string(REGEX MATCHALL "Source/[^\n ]+\\.cpp" lib_src "${CMAKE_MATCH_1}")
list(REMOVE_ITEM lib_src Source/ImGuiDebug.cpp)
list(TRANSFORM lib_src PREPEND ${ROOT}/)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The game relies on what VC6 did with undefined behaviour (uninitialised locals returned on unused
# paths, null checks after use): clang must not optimise on it.
add_compile_options(
    -m32 -fms-extensions -fasm-blocks -fshort-wchar -fsigned-char
    -fno-strict-aliasing -fwrapv -ftrivial-auto-var-init=zero -fno-delete-null-pointer-checks -fno-autolink -w -Wno-address-of-temporary -Wno-c++11-narrowing)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS GTA2_PORT)

if(WIN32)
    include_directories(${CMAKE_CURRENT_LIST_DIR}/include ${ROOT}/Source ${ROOT})

    add_library(mss32 SHARED ${ROOT}/Source/3rdParty/mss.cpp)
    target_compile_definitions(mss32 PRIVATE BUILD_MSS)

    add_library(binkw32 SHARED ${ROOT}/Source/3rdParty/bink.cpp)
    target_compile_definitions(binkw32 PRIVATE BUILD_BINK)

    add_executable(gta2 WIN32 ${lib_src} ${ROOT}/test.cpp ${ROOT}/StdAfx.cpp
        src/debug_stubs.cpp ${CMAKE_CURRENT_LIST_DIR}/resources.rc)
    target_link_libraries(gta2 PRIVATE mss32 binkw32
        ddraw dinput dxguid dplayx winmm ole32 oleaut32 user32 gdi32 advapi32 version shell32 comdlg32)
    target_link_options(gta2 PRIVATE -static -static-libgcc -static-libstdc++ -lpthread)
    set_target_properties(mss32 binkw32 PROPERTIES PREFIX "")
else()
    # Win32 and DirectX declarations come from Wine's headers; compat/ implements what the game calls.
    find_path(WINE_INCLUDE windows.h PATHS /usr/include/wine/windows /usr/include/wine/wine/windows NO_DEFAULT_PATH REQUIRED)
    find_package(SDL3 REQUIRED CONFIG)

    # The OpenGL ES renderer: portable, no Windows headers.
    file(GLOB renderer_src ${CMAKE_CURRENT_LIST_DIR}/renderer/*.cpp)
    add_library(gta2gl STATIC ${renderer_src})
    target_link_libraries(gta2gl PRIVATE SDL3::SDL3)

    file(GLOB compat_src ${CMAKE_CURRENT_LIST_DIR}/compat/src/*.cpp)
    add_executable(gta2 ${lib_src} ${ROOT}/test.cpp ${ROOT}/StdAfx.cpp
        ${ROOT}/Source/3rdParty/bink.cpp
        src/debug_stubs.cpp ${compat_src})
    target_compile_options(gta2 PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/compat/prelude.hpp)
    target_compile_definitions(gta2 PRIVATE __NT__ GTA2_PORT_SDL)
    target_include_directories(gta2 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/compat/include ${CMAKE_CURRENT_LIST_DIR}/include
        ${WINE_INCLUDE} ${ROOT}/Source ${ROOT})
    target_link_libraries(gta2 PRIVATE gta2gl SDL3::SDL3)
    target_link_options(gta2 PRIVATE -m32 -Wl,--no-dependent-libraries -Wl,--error-limit=0)
endif()
