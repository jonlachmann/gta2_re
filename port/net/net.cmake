# The network game (port/docs/netcode.md): ENet, vendored, and the netcode's parts without the game, with their tests
# (ctest -L net). Included before the game's compile options: these are plain C and C++.
set(NET ${CMAKE_CURRENT_LIST_DIR})

# ENet 1.3.18, plain C, with what its configure finds on Linux and macOS
file(GLOB enet_src ${NET}/enet/*.c)
add_library(enet STATIC ${enet_src})
target_include_directories(enet PUBLIC ${NET}/enet/include)
target_compile_options(enet PRIVATE ${arch_flags} -w)
if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
    target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1
        HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
endif()

file(GLOB net_core_src ${NET}/core/*.cpp)
file(GLOB net_transport_src ${NET}/transport/*.cpp)

# The netcode's tests: no game, no SDL3
add_executable(test_core ${NET}/tests/test_core.cpp ${net_core_src})
add_executable(test_transport ${NET}/tests/test_transport.cpp ${net_transport_src} ${net_core_src})
target_link_libraries(test_transport PRIVATE enet)
add_executable(test_bot ${NET}/tests/test_bot.cpp ${NET}/session/bot_nav.cpp)
add_executable(test_rounds ${NET}/tests/test_rounds.cpp ${NET}/session/round_flow.cpp)
set(net_tests test_core test_transport test_bot test_rounds)
foreach(t ${net_tests})
    target_compile_options(${t} PRIVATE ${arch_flags})
    target_link_options(${t} PRIVATE ${arch_flags})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES LABELS net)
    if(WIN32)
        target_link_options(${t} PRIVATE -static)
    endif()
endforeach()

# The rest is the game's: built into it with the game's flags (CMakeLists.txt). Its own NetPlay and NetUi stand in for
# the game's DirectPlay ones (engine/netplay.cpp).
file(GLOB_RECURSE net_game_src ${NET}/*.cpp)
list(FILTER net_game_src EXCLUDE REGEX "/(tests|enet)/")
