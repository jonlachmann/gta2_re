# The portable Linux build, i686 (linux-sysroot.cmake).
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)
include(${CMAKE_CURRENT_LIST_DIR}/linux-sysroot.cmake)
