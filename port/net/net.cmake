# The network game (docs/netcode.md): ENet, vendored, and the netcode's parts without the game, with their tests
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
