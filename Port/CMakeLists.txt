# The port for the host (macOS or Linux) with clang, as Scripts/port/build_mac.sh and build_linux.sh build it
# (docs/port.md): gta2_port, the game with its network game (docs/netcode.md), and the netcode's tests.
#   cmake -S Port -B build_port/mac && cmake --build build_port/mac -j
#   ctest --test-dir build_port/mac          (the net tests; -DGTA2_NET_TESTS=OFF leaves them out)
# The root CMakeLists.txt is the VC6 build; its gta2_lib_src is the game's source list, read from it here.
cmake_minimum_required(VERSION 3.16)
project(gta2_port C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "the port needs clang (-fms-extensions, -fshort-wchar...): CXX=clang++ CC=clang cmake ...")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo) # -O2 -g, as the scripts
endif()
option(GTA2_NET_TESTS "build the netcode's tests (ctest)" ON)
get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

# The game: gta2_lib_src without the ImGui overlay and DirectPlay (Port/net/engine/netplay.cpp stands in), plus
# StdAfx.cpp, Port/ (main.cpp replaces test.cpp), the headless tools and the netcode (Scripts/port/build_common.sh)
file(READ ${ROOT}/CMakeLists.txt ROOT_LISTS)
string(REGEX MATCH "set *\\(gta2_lib_src[^)]*\\)" LIB_SRC "${ROOT_LISTS}")
string(REGEX MATCHALL "Source/[^ \n\t)]*\\.cpp" LIB_SRC "${LIB_SRC}")
list(FILTER LIB_SRC EXCLUDE REGEX "ImGuiDebug|NetPlay|NetUi")
list(TRANSFORM LIB_SRC PREPEND ${ROOT}/)
file(GLOB PORT_SRC ${ROOT}/Port/*.cpp)
file(GLOB_RECURSE NET_SRC ${ROOT}/Port/net/*.cpp)
list(FILTER NET_SRC EXCLUDE REGEX "/tests/")
file(GLOB ENET_SRC ${ROOT}/Port/net/enet/*.c)
set(REN ${ROOT}/Renderers/OpenGL)
set(REN_SRC ${REN}/core/gl_funcs.cpp ${REN}/core/gl_renderer.cpp ${REN}/core/gl_enhanced.cpp ${REN}/core/vid_system.cpp
    ${REN}/core/capture.cpp ${REN}/core/options.cpp ${REN}/core/gbh_api.cpp ${REN}/core/cmdlog.cpp ${REN}/platform/sdl_context.cpp)
set(BINK ${ROOT}/Source/3rdParty/bink.cpp) # stubs: no movies yet

find_package(SDL3 REQUIRED)

add_executable(gta2_port ${LIB_SRC} ${ROOT}/StdAfx.cpp ${PORT_SRC} ${ROOT}/Port/headless/state_log.cpp
               ${ROOT}/Port/headless/fast_clock.cpp ${ROOT}/Port/headless/sim_bench.cpp ${ROOT}/Port/headless/neteval.cpp
               ${NET_SRC} ${BINK} ${REN_SRC} ${ENET_SRC})
set_target_properties(gta2_port PROPERTIES ENABLE_EXPORTS ON RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}) # -rdynamic: the game finds the renderer's gbh_*/Vid_* with dlsym
target_link_libraries(gta2_port PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS} pthread m)
if(APPLE)
  target_link_libraries(gta2_port PRIVATE "-framework OpenGL")
  target_compile_definitions(gta2_port PRIVATE GL_SILENCE_DEPRECATION)
endif()

# The game's flags (the Windows build's, plus the Windows names from Port/compat); the renderer and ENet keep their own
set(GAME_FLAGS -w -Wno-address-of-temporary -fshort-wchar -fms-extensions -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks
    -ftrivial-auto-var-init=zero -fno-strict-return -include ${ROOT}/Port/compat/port_linux.h)
set(GAME_DEFS _CRT_SECURE_NO_WARNINGS _USE_32BIT_TIME_T GTA2_PORT NDEBUG BUILD_MSS)
if(CMAKE_SIZEOF_VOID_P EQUAL 4) # 32-bit x86 (the Linux build): x87 floats, as 10.5.exe
  list(APPEND GAME_FLAGS -mno-sse -mfpmath=387)
  list(APPEND GAME_DEFS STATELOG_IN_PORT)
endif()
set(GAME_CXX ${LIB_SRC} ${ROOT}/StdAfx.cpp ${PORT_SRC} ${ROOT}/Port/headless/state_log.cpp ${ROOT}/Port/headless/fast_clock.cpp
    ${ROOT}/Port/headless/sim_bench.cpp ${ROOT}/Port/headless/neteval.cpp ${NET_SRC} ${BINK})
string(REPLACE ";" " " GAME_FLAGS_STR "${GAME_FLAGS}")
set_source_files_properties(${GAME_CXX} PROPERTIES COMPILE_FLAGS "-std=c++17 ${GAME_FLAGS_STR}"
                            INCLUDE_DIRECTORIES "${ROOT}/Port/compat;${ROOT}/Source;${ROOT};${ROOT}/Port/net/enet/include"
                            COMPILE_DEFINITIONS "${GAME_DEFS}")
set_source_files_properties(${BINK} PROPERTIES COMPILE_DEFINITIONS "${GAME_DEFS};BUILD_BINK")
set_source_files_properties(${REN_SRC} PROPERTIES COMPILE_FLAGS "-std=c++11 -w")
# What ENet's configure would find on Linux and macOS
set(ENET_DEFS HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
set_source_files_properties(${ENET_SRC} PROPERTIES COMPILE_FLAGS "-w" INCLUDE_DIRECTORIES ${ROOT}/Port/net/enet/include
                            COMPILE_DEFINITIONS "${ENET_DEFS}")

# The netcode's tests without the game (Scripts/port/net_tests.sh, there with UBSan)
if(GTA2_NET_TESTS)
  enable_testing()
  add_library(enet_test STATIC ${ENET_SRC})
  target_include_directories(enet_test PUBLIC ${ROOT}/Port/net/enet/include)
  file(GLOB CORE_SRC ${ROOT}/Port/net/core/*.cpp)
  file(GLOB TRANSPORT_SRC ${ROOT}/Port/net/transport/*.cpp)
  add_executable(test_core ${ROOT}/Port/net/tests/test_core.cpp ${CORE_SRC})
  add_executable(test_transport ${ROOT}/Port/net/tests/test_transport.cpp ${TRANSPORT_SRC} ${CORE_SRC})
  target_link_libraries(test_transport PRIVATE enet_test)
  add_executable(test_bot ${ROOT}/Port/net/tests/test_bot.cpp ${ROOT}/Port/net/session/bot_nav.cpp)
  foreach(t test_core test_transport test_bot)
    set_target_properties(${t} PROPERTIES CXX_STANDARD 17)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()
