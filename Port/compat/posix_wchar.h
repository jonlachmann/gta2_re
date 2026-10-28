// Port (docs/port.md): the game's wide strings are 2-byte (-fshort-wchar), as on Windows, but the C
// library's wide functions on Linux and macOS take 4-byte wchar_t. The game's calls go to
// Port/posix_wchar.cpp instead, with Microsoft's meaning: swprintf(buf, fmt, ...) without a size, and %s/%c
// for wide strings/characters (%S/%C narrow).
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif
int port_swprintf(wchar_t* buf, const wchar_t* fmt, ...);
int port_vswprintf(wchar_t* buf, const wchar_t* fmt, va_list args);
size_t port_wcslen(const wchar_t* s);
wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src);
wchar_t* port_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* port_wcscat(wchar_t* dst, const wchar_t* src);
int port_wcscmp(const wchar_t* a, const wchar_t* b);
size_t port_wcstombs(char* dst, const wchar_t* src, size_t n);
size_t port_mbstowcs(wchar_t* dst, const char* src, size_t n);
wchar_t* port_itow(int value, wchar_t* buf, int radix);
#ifdef __cplusplus
}
#endif

#define swprintf port_swprintf
#define vswprintf port_vswprintf
#define wcslen port_wcslen
#define wcscpy port_wcscpy
#define wcsncpy port_wcsncpy
#define wcscat port_wcscat
#define wcscmp port_wcscmp
#define wcstombs port_wcstombs
#define mbstowcs port_mbstowcs
#define _itow port_itow
