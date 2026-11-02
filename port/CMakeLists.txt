cmake_minimum_required(VERSION 3.20)
project(gta2_port C CXX)

# Fork build: one executable from the game sources, clang, 32-bit.
#   Windows (MinGW): cmake -S port -B build_port -G Ninja -DCMAKE_TOOLCHAIN_FILE=port/toolchain-mingw32-clang.cmake -DSDL3_DIR=...
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

# Windows can also build with DMA's DLLs (DirectX renderer and video, Miles, Bink) next to the exe.
option(GTA2_PORT_DIRECTX "Windows: use the original DirectX, Miles and Bink DLLs instead of SDL3" OFF)

if(WIN32 AND GTA2_PORT_DIRECTX)
    include_directories(${CMAKE_CURRENT_LIST_DIR}/include ${ROOT}/Source ${ROOT})

    add_library(mss32 SHARED ${ROOT}/Source/3rdParty/mss.cpp)
    target_compile_definitions(mss32 PRIVATE BUILD_MSS)

    add_library(binkw32 SHARED ${ROOT}/Source/3rdParty/bink.cpp)
    target_compile_definitions(binkw32 PRIVATE BUILD_BINK)

    add_executable(gta2 WIN32 ${lib_src} ${ROOT}/test.cpp ${ROOT}/StdAfx.cpp
        src/debug_stubs.cpp src/menu.cpp src/options_menu.cpp src/pause_menu.cpp src/lights.cpp src/shadows.cpp src/settings.cpp src/game_hooks.cpp src/frame_loop.cpp src/interpolation.cpp src/view.cpp src/day_night.cpp ${CMAKE_CURRENT_LIST_DIR}/resources.rc)
    target_link_libraries(gta2 PRIVATE mss32 binkw32
        ddraw dinput dxguid dplayx winmm ole32 oleaut32 user32 gdi32 advapi32 version shell32 comdlg32)
    target_link_options(gta2 PRIVATE -static -static-libgcc -static-libstdc++ -lpthread)
    set_target_properties(mss32 binkw32 PROPERTIES PREFIX "")
else()
    # Windows: SDL3-devel-<version>-mingw, -DSDL3_DIR=<it>/i686-w64-mingw32/lib/cmake/SDL3.
    find_package(SDL3 REQUIRED CONFIG)

    # The OpenGL ES renderer: portable, no Windows headers.
    file(GLOB renderer_src ${CMAKE_CURRENT_LIST_DIR}/renderer/*.cpp)
    add_library(gta2gl STATIC ${renderer_src})
    target_link_libraries(gta2gl PRIVATE SDL3::SDL3)

    # First-run setup (setup/): the game folder, and copying the game's data into the per-user one
    # from an install, an archive or an installer, with miniz and the LZMA SDK's decoders.
    set(third_party ${CMAKE_CURRENT_LIST_DIR}/third_party)
    set(lzma_src 7zAlloc 7zArcIn 7zBuf 7zBuf2 7zCrc 7zCrcOpt 7zDec 7zFile 7zStream Bcj2 Bra Bra86 BraIA64
        CpuArch Delta Lzma2Dec LzmaDec Ppmd7 Ppmd7Dec)
    list(TRANSFORM lzma_src PREPEND ${third_party}/lzma/)
    list(TRANSFORM lzma_src APPEND .c)
    file(GLOB setup_src ${CMAKE_CURRENT_LIST_DIR}/setup/*.cpp)
    add_library(gta2setup STATIC ${setup_src} ${lzma_src} ${third_party}/miniz/miniz.c)
    target_include_directories(gta2setup PRIVATE ${third_party}/lzma ${third_party}/miniz)
    target_compile_definitions(gta2setup PRIVATE _FILE_OFFSET_BITS=64)
    target_link_libraries(gta2setup PRIVATE SDL3::SDL3)
    if(NOT WIN32)
        # The C++ library's wchar_t.
        target_compile_options(gta2setup PRIVATE -fno-short-wchar)
    endif()

    set(compat ${CMAKE_CURRENT_LIST_DIR}/compat)
    set(compat_src crt cursor dinput dll guids kernel main miles mouse pad miles3d registry test_input video_port window)
    if(NOT WIN32)
        list(APPEND compat_src kernel_posix wstr)
    endif()
    list(TRANSFORM compat_src PREPEND ${compat}/src/)
    list(TRANSFORM compat_src APPEND .cpp)
    # Our Miles and Bink are defined here, not imported from their DLLs.
    set_source_files_properties(${compat}/src/miles.cpp ${compat}/src/miles3d.cpp PROPERTIES COMPILE_DEFINITIONS BUILD_MSS)
    # 3D sound: OpenAL Soft, loaded at run time from next to the exe. Without it the game has 2D sound only.
    set(GTA2_OPENAL_DIR "" CACHE PATH "OpenAL Soft install prefix: headers, and the library copied next to the exe")
    if(GTA2_OPENAL_DIR)
        set_source_files_properties(${compat}/src/miles3d.cpp PROPERTIES
            COMPILE_DEFINITIONS "BUILD_MSS;GTA2_HAVE_OPENAL" INCLUDE_DIRECTORIES ${GTA2_OPENAL_DIR}/include)
        if(WIN32)
            set(openal_runtime ${GTA2_OPENAL_DIR}/bin/OpenAL32.dll)
        else()
            set(openal_runtime ${GTA2_OPENAL_DIR}/lib/libopenal.so.1)
        endif()
    endif()
    set_source_files_properties(${ROOT}/Source/3rdParty/bink.cpp PROPERTIES COMPILE_DEFINITIONS BUILD_BINK)

    add_executable(gta2 ${lib_src} ${ROOT}/test.cpp ${ROOT}/StdAfx.cpp
        ${ROOT}/Source/3rdParty/bink.cpp
        src/debug_stubs.cpp src/menu.cpp src/options_menu.cpp src/pause_menu.cpp src/lights.cpp src/shadows.cpp src/settings.cpp src/game_hooks.cpp src/frame_loop.cpp src/interpolation.cpp src/view.cpp src/day_night.cpp ${compat_src})
    target_compile_definitions(gta2 PRIVATE GTA2_PORT_SDL)
    target_link_libraries(gta2 PRIVATE gta2gl gta2setup SDL3::SDL3)
    if(openal_runtime)
        add_custom_command(TARGET gta2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${openal_runtime} $<TARGET_FILE_DIR:gta2>)
    endif()

    if(WIN32)
        # MinGW's headers declare the Win32 API; win32_redirect.hpp sends the game's calls to compat/.
        target_sources(gta2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/resources.rc ${CMAKE_CURRENT_LIST_DIR}/setup/manifest.rc)
        set_target_properties(gta2 PROPERTIES WIN32_EXECUTABLE ON)
        target_compile_options(gta2 PRIVATE -include ${compat}/win32_redirect.hpp)
        target_include_directories(gta2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include ${ROOT}/Source ${ROOT})
        target_link_libraries(gta2 PRIVATE user32 advapi32 version)
        target_link_options(gta2 PRIVATE -static -static-libgcc -static-libstdc++ -lpthread)
        add_custom_command(TARGET gta2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:gta2> $<TARGET_FILE_DIR:gta2>
            COMMAND_EXPAND_LISTS)
    else()
        # compat/include declares the Win32 and DirectX subset the game uses; compat/src implements it.
        target_compile_options(gta2 PRIVATE -include ${compat}/prelude.hpp)
        target_compile_definitions(gta2 PRIVATE __NT__)
        # LLVM turns wide string loops into calls to the C library, whose wchar_t is 4 bytes.
        target_compile_options(gta2 PRIVATE -fno-builtin-wcslen -fno-builtin-wcscpy -fno-builtin-wcscmp
            -fno-builtin-wcsncmp -fno-builtin-wcschr -fno-builtin-wmemcpy -fno-builtin-wmemset -fno-builtin-wmemcmp)
        target_include_directories(gta2 PRIVATE
            ${compat}/include ${CMAKE_CURRENT_LIST_DIR}/include ${ROOT}/Source ${ROOT})
        target_link_options(gta2 PRIVATE -m32 -Wl,--no-dependent-libraries -Wl,--error-limit=0)
    endif()
endif()
