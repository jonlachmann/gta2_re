# The port's Windows build (mingw-clang.cmake): clang compiles, the MinGW g++ links, with no compile flags
set(CMAKE_CXX_LINK_EXECUTABLE "i686-w64-mingw32-g++ <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY
    "i686-w64-mingw32-g++ -shared <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--out-implib,<TARGET_IMPLIB> <LINK_LIBRARIES>")
