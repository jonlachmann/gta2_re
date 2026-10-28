// Port (docs/port.md): force-included (-include) first in every game file of non-Windows builds. On
// Windows the game gets these names from headers that pull in windows.h before they use them; here they
// come first, along with the replacements for the C library parts that differ (2-byte wide strings,
// Windows paths) and the window calls on SDL3.
#pragma once
#ifdef __cplusplus
    // These #undef the C library's names (fopen, swprintf, ...), so they come before the redirects below,
    // and their include guards keep later includes from undoing them.
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>
    #include <cwchar>
    #include <string>
#endif
#include "windows.h"
#include "posix_files.h"
#include "posix_wchar.h"
#ifdef __cplusplus
    #include "../sdl_win32.hpp"
#endif
