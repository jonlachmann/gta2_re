# The port (docs/port.md): gta2_port, the game on SDL3 and OpenGL for macOS, Linux and Windows, with its network game
# (docs/netcode.md), and the tests. The
# targets are presets (CMakePresets.json), from the repository root:
#   cmake --preset mac -S Port && cmake --build build_port/mac && ctest --test-dir build_port/mac
# The root CMakeLists.txt is the VC6 build; its gta2_lib_src is the game's source list, read from it here.
cmake_minimum_required(VERSION 3.21)
project(gta2_port C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "the port needs clang (-fms-extensions, -fshort-wchar...): use a preset, or CXX=clang++ cmake ...")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
enable_testing()

# ---- Sources

# The game: gta2_lib_src without the ImGui overlay and DirectPlay (NetPlay, NetUi; Port/net/engine/netplay.cpp
# stands in)
file(READ ${ROOT}/CMakeLists.txt ROOT_LISTS)
string(REGEX MATCH "set *\\(gta2_lib_src[^)]*\\)" LIB_SRC "${ROOT_LISTS}")
string(REGEX MATCHALL "Source/[^ \n\t)]*\\.cpp" LIB_SRC "${LIB_SRC}")
list(FILTER LIB_SRC EXCLUDE REGEX "ImGuiDebug|NetPlay|NetUi")
list(TRANSFORM LIB_SRC PREPEND ${ROOT}/)
list(APPEND LIB_SRC ${ROOT}/StdAfx.cpp)

# The platform layer: what the game uses of Windows and DirectX, on SDL3 and POSIX (docs/port.md, "Code layout")
set(PORT_SRC main.cpp port_globals.cpp port_hooks.cpp rand/rand.cpp rand/particle.cpp ptr_table.cpp registry_ini.cpp
    sdl_dinput.cpp sdl_miles.cpp sdl_video.cpp sdl_win32.cpp stubs.cpp)
if(NOT WIN32)
  list(APPEND PORT_SRC posix_files.cpp posix_wchar.cpp posix_win32.cpp)
endif()
# Validation against 10.5.exe and test aids, linked in, off unless their environment variables are set
set(VALIDATION_SRC validation/validation.cpp validation/state_log.cpp validation/fast_clock.cpp validation/scripted_input.cpp)
list(TRANSFORM PORT_SRC PREPEND ${ROOT}/Port/)
list(TRANSFORM VALIDATION_SRC PREPEND ${ROOT}/Port/)

# The network game (docs/netcode.md, "Code layout"), and ENet, vendored, plain C
file(GLOB_RECURSE NET_SRC ${ROOT}/Port/net/*.cpp)
list(FILTER NET_SRC EXCLUDE REGEX "/tests/")
file(GLOB ENET_SRC ${ROOT}/Port/net/enet/*.c)

# The OpenGL renderer, linked in: the game finds its gbh_* and Vid_* functions in the program itself
include(${ROOT}/Renderers/OpenGL/sources.cmake)
set(REN_SRC ${GL_VIDEO_SRC} ${GL_RENDER_SRC} ${GL_ROOT}/platform/sdl_context.cpp)
list(REMOVE_DUPLICATES REN_SRC)

# ---- The netcode's tests, without the game (ctest -L net; the net-tests preset builds only them, with UBSan)

# ENet keeps its own: what its configure would find on Linux and macOS
add_library(enet STATIC ${ENET_SRC})
target_include_directories(enet PUBLIC ${ROOT}/Port/net/enet/include)
target_compile_options(enet PRIVATE -w)
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1
                             HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(enet PRIVATE -fPIC)
  endif()
endif()

set(NET ${ROOT}/Port/net)
file(GLOB CORE_SRC ${NET}/core/*.cpp)
file(GLOB TRANSPORT_SRC ${NET}/transport/*.cpp)
add_executable(test_core ${NET}/tests/test_core.cpp ${CORE_SRC})
add_executable(test_transport ${NET}/tests/test_transport.cpp ${TRANSPORT_SRC} ${CORE_SRC})
target_link_libraries(test_transport PRIVATE enet)
add_executable(test_bot ${NET}/tests/test_bot.cpp ${NET}/session/bot_nav.cpp)
add_executable(test_rounds ${NET}/tests/test_rounds.cpp ${NET}/session/round_flow.cpp)
foreach(t test_core test_transport test_bot test_rounds)
  set_target_properties(${t} PROPERTIES CXX_STANDARD 17)
  add_test(NAME ${t} COMMAND ${t})
  set_tests_properties(${t} PROPERTIES LABELS net)
  if(WIN32)
    target_link_options(${t} PRIVATE -static)
  endif()
endforeach()
add_custom_target(net_tests DEPENDS test_core test_transport test_bot test_rounds)

option(GTA2_GAME "the game and its tests (needs SDL3); OFF: the netcode's tests only" ON)
if(NOT GTA2_GAME)
  return()
endif()

# ---- Flags

if(WIN32)
  set(GTA2_SDL3_DIR ${ROOT}/build_port/sdl3 CACHE PATH "SDL3's MinGW package, its i686-w64-mingw32 folder")
  list(APPEND CMAKE_PREFIX_PATH ${GTA2_SDL3_DIR})
endif()
find_package(SDL3 REQUIRED CONFIG)

# The game's: as the Windows build's (docs/port.md, "Compiler flags"), plus on other systems the Windows names of
# Port/compat, force-included first
add_library(game_flags INTERFACE)
target_compile_options(game_flags INTERFACE -std=c++17 -w -Wno-address-of-temporary -fshort-wchar -fno-strict-aliasing -fwrapv
                       -fno-delete-null-pointer-checks -ftrivial-auto-var-init=zero -fno-strict-return)
target_compile_definitions(game_flags INTERFACE _CRT_SECURE_NO_WARNINGS _USE_32BIT_TIME_T GTA2_PORT NDEBUG BUILD_MSS)
target_include_directories(game_flags INTERFACE ${ROOT}/Source ${ROOT} ${ROOT}/Port/net/enet/include)
if(WIN32)
  # -fno-optimize-sibling-calls: no tail calls, so the state check's call trace sees every return
  target_compile_options(game_flags INTERFACE -fno-optimize-sibling-calls)
  target_compile_definitions(game_flags INTERFACE _CRT_NON_CONFORMING_SWPRINTFS)
else()
  target_compile_options(game_flags INTERFACE -fms-extensions -include ${ROOT}/Port/compat/posix_prefix.h)
  target_include_directories(game_flags BEFORE INTERFACE ${ROOT}/Port/compat)
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  # x87 floats, as 10.5.exe; the state log compares the globals with 10.5.exe's (32-bit builds only)
  target_compile_options(game_flags INTERFACE -mno-sse -mfpmath=387)
  target_compile_definitions(game_flags INTERFACE STATELOG_IN_PORT)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_options(game_flags INTERFACE -fPIC)
endif()
target_link_libraries(game_flags INTERFACE SDL3::SDL3)

# The renderer keeps its own (C++11, its own warnings off)
add_library(renderer OBJECT ${REN_SRC})
set_target_properties(renderer PROPERTIES CXX_STANDARD 11)
target_compile_options(renderer PRIVATE -w)
target_link_libraries(renderer PRIVATE SDL3::SDL3)
if(WIN32)
  target_compile_definitions(renderer PRIVATE GTA2GL_BUILD_DLL)
elseif(APPLE)
  target_compile_definitions(renderer PRIVATE GL_SILENCE_DEPRECATION)
else()
  target_compile_options(renderer PRIVATE -fPIC)
endif()

# ---- gta2_port

add_executable(gta2_port ${LIB_SRC} ${PORT_SRC} ${VALIDATION_SRC} ${NET_SRC})
target_link_libraries(gta2_port PRIVATE game_flags renderer enet)
set_target_properties(gta2_port PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(NOT WIN32)
  add_library(bink OBJECT ${ROOT}/Source/3rdParty/bink.cpp) # stubs, no movies yet
  target_link_libraries(bink PRIVATE game_flags)
  target_compile_definitions(bink PRIVATE BUILD_BINK)
  target_link_libraries(gta2_port PRIVATE bink)
  target_link_options(gta2_port PRIVATE -rdynamic) # the game finds the renderer's gbh_*/Vid_* with dlsym
endif()
if(APPLE)
  target_link_libraries(gta2_port PRIVATE "-framework OpenGL")
elseif(WIN32)
  # The game's own binkw32.dll (from the GTA2 install); Miles is Port/sdl_miles.cpp
  set(GTA2_DLL_DIR ${ROOT}/build_port/dlls CACHE PATH "the GTA2 install's binkw32.dll")
  add_custom_command(OUTPUT libbinkw32.a COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/import_lib.sh ${GTA2_DLL_DIR}/binkw32.dll
                     libbinkw32.a DEPENDS ${GTA2_DLL_DIR}/binkw32.dll)
  add_custom_target(binkw32_lib DEPENDS libbinkw32.a)
  add_dependencies(gta2_port binkw32_lib)
  target_sources(gta2_port PRIVATE ${ROOT}/resources.rc)
  target_link_options(gta2_port PRIVATE -mwindows -static -Wl,--kill-at)
  target_link_libraries(gta2_port PRIVATE ${CMAKE_BINARY_DIR}/libbinkw32.a version dinput dxguid ddraw ws2_32 winmm ole32
                        uuid gdi32 comctl32 opengl32)
  add_custom_command(TARGET gta2_port POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                     ${GTA2_SDL3_DIR}/bin/SDL3.dll ${CMAKE_BINARY_DIR}/)
else()
  # lld's --no-dependent-libraries: the game's #pragma comment(lib, ...) name Windows libraries. The C++ runtime is
  # linked in, and the program finds libSDL3.so.0 next to itself first.
  target_link_options(gta2_port PRIVATE -fuse-ld=lld -Wl,--no-dependent-libraries -Wl,--error-limit=0 -static-libstdc++ -static-libgcc
                      "-Wl,-rpath,\$ORIGIN")
  target_link_libraries(gta2_port PRIVATE ${CMAKE_DL_LIBS} pthread m)
endif()

# ---- The renderer's own targets, in gl/: on Windows its DLLs for 10.5.exe, built with the port's compiler, so
# 10.5.exe renders with the same code as gta2_port.exe and frame comparisons only show the game's differences; on other
# systems its tests

add_subdirectory(${GL_ROOT} gl)
if(WIN32)
  # nullvideo.dll: draws nothing, and runs GTA2_SPEED and GTA2_STATELOG inside 10.5.exe (the state check)
  add_library(nullvideo SHARED ${ROOT}/Port/validation/nullvideo.cpp ${ROOT}/Port/validation/state_log.cpp
              ${ROOT}/Port/validation/fast_clock.cpp ${GL_VIDEO_SRC})
  set_target_properties(nullvideo PROPERTIES PREFIX "" CXX_STANDARD 11 LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gl
                        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gl)
  target_compile_options(nullvideo PRIVATE -w)
  target_compile_definitions(nullvideo PRIVATE GTA2GL_BUILD_DLL)
  target_link_options(nullvideo PRIVATE -static -Wl,--kill-at)
endif()

# ---- Tests (ctest; the Windows build's run under wine)

function(port_test name)
  add_executable(${name} ${ARGN})
  set(dir ${CMAKE_BINARY_DIR}/test_${name}) # they write files where they run
  file(MAKE_DIRECTORY ${dir})
  add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${dir})
endfunction()

# With the game's flags, each linked with the file it checks
set(TEST_POSIX "")
if(NOT WIN32)
  set(TEST_POSIX ${ROOT}/Port/posix_files.cpp ${ROOT}/Port/posix_wchar.cpp)
  port_test(posix_files_test ${ROOT}/Port/tests/posix_files_test.cpp ${TEST_POSIX})
  port_test(posix_wchar_test ${ROOT}/Port/tests/posix_wchar_test.cpp ${TEST_POSIX})
endif()
port_test(registry_ini_test ${ROOT}/Port/tests/registry_ini_test.cpp ${ROOT}/Port/registry_ini.cpp ${TEST_POSIX})
port_test(sdl_miles_test ${ROOT}/Port/tests/sdl_miles_test.cpp ${ROOT}/Port/sdl_miles.cpp ${TEST_POSIX})
foreach(t registry_ini_test sdl_miles_test posix_files_test posix_wchar_test)
  if(NOT TARGET ${t})
    continue()
  endif()
  target_link_libraries(${t} PRIVATE game_flags)
  target_include_directories(${t} PRIVATE ${ROOT}/Port)
  if(WIN32)
    target_link_options(${t} PRIVATE -static)
    add_custom_command(TARGET ${t} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GTA2_SDL3_DIR}/bin/SDL3.dll
                       ${CMAKE_BINARY_DIR}/)
  endif()
endforeach()
