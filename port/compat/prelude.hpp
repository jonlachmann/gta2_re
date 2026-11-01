// Forced first include of every TU of the non-Windows build.
#pragma once

// Before <windows.h>, whose min/max macros break these headers.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <math.h>
#include <cstdio>
#include <stdio.h>
#include <cstdlib>
#include <stdlib.h>
#include <cstring>
#include <string.h>
#include <cwchar>
#include <deque>
#include <wchar.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// WCHAR as wchar_t, which -fshort-wchar makes 2 bytes like on Windows.
#define WINE_UNICODE_NATIVE
#define __forceinline inline

typedef unsigned char byte;
typedef unsigned char boolean;
typedef long long hyper;

#include "crt.hpp"
#include "wstr.hpp"
