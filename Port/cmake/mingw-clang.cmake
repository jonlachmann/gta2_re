# The port's Windows build (docs/port.md, "Building"): 32-bit x86, compiled with clang against the MinGW-w64 headers
# and linked with the MinGW-w64 g++. Its tests run under wine.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_COMPILER_TARGET i686-w64-mingw32)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY) # clang doesn't link for MinGW here
set(CMAKE_CROSSCOMPILING_EMULATOR wine)

# The MinGW g++'s C++ and C header directories, in the order libstdc++'s #include_next needs
execute_process(COMMAND sh -c "echo | i686-w64-mingw32-g++ -xc++ -E -v - 2>&1" OUTPUT_VARIABLE GCC_V)
string(REGEX MATCH "#include <...> search starts here:\n(.*)End of search list" _ "${GCC_V}")
string(REPLACE "\n" ";" GCC_INC "${CMAKE_MATCH_1}")
set(MINGW_ISYSTEM "")
foreach(d ${GCC_INC})
  string(STRIP "${d}" d)
  if(d AND NOT d MATCHES "/lib/gcc/[^/]+/[^/]+/include(-fixed)?$")
    string(APPEND MINGW_ISYSTEM " -isystem ${d}")
  endif()
endforeach()
# __CPUID_H: SDL's headers pull in MinGW's intrin.h, whose __cpuidex clashes with clang's cpuid.h, which nothing here
# uses; its include guard is preset
set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -nostdlibinc -fsjlj-exceptions -D__CPUID_H${MINGW_ISYSTEM}")

# Linked by the MinGW g++ (its runtime and libraries): rules CMake reads after its platform's
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX ${CMAKE_CURRENT_LIST_DIR}/mingw-clang-link.cmake)
