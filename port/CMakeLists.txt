cmake_minimum_required(VERSION 3.20)
project(gta2_port C CXX)

# Fork build: one executable from the game sources, clang, 32-bit.
#   cmake -S port -B build_port -G Ninja -DCMAKE_TOOLCHAIN_FILE=port/toolchain-mingw32-clang.cmake

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# The game sources are the VC6 build's gta2_lib_src list, so upstream additions come along on merges.
file(READ ${ROOT}/CMakeLists.txt top)
string(REGEX MATCH "set\\(gta2_lib_src\n([^)]*)\\)" _ "${top}")
string(REGEX MATCHALL "Source/[^\n ]+\\.cpp" lib_src "${CMAKE_MATCH_1}")
list(REMOVE_ITEM lib_src Source/ImGuiDebug.cpp)
list(TRANSFORM lib_src PREPEND ${ROOT}/)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    -m32 -fms-extensions -fasm-blocks -fshort-wchar -fsigned-char
    -fno-strict-aliasing -fwrapv -fno-autolink -w -Wno-address-of-temporary -Wno-c++11-narrowing)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS GTA2_PORT)
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
