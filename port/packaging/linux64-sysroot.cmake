# The portable Linux build, x86_64 (linux-sysroot.cmake).
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
include(${CMAKE_CURRENT_LIST_DIR}/linux-sysroot.cmake)
