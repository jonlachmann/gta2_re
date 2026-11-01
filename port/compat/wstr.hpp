// 2-byte wide string functions (-fshort-wchar): the C library's expect a 4-byte wchar_t.
// Formats follow Microsoft's CRT: in a wide format, %s is a wide string and %S a narrow one.
#pragma once

#include <stdarg.h>
#include <stddef.h>

static_assert(sizeof(wchar_t) == 2, "build with -fshort-wchar");

size_t port_wcslen(const wchar_t* s);
wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src);
wchar_t* port_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* port_wcscat(wchar_t* dst, const wchar_t* src);
int port_wcscmp(const wchar_t* a, const wchar_t* b);
int port_wcsncmp(const wchar_t* a, const wchar_t* b, size_t n);
size_t port_wcstombs(char* dst, const wchar_t* src, size_t n);
size_t port_mbstowcs(wchar_t* dst, const char* src, size_t n);
wchar_t* port_itow(int value, wchar_t* dst, int radix);
int port_vswprintf(wchar_t* dst, const wchar_t* fmt, va_list args);
int port_swprintf(wchar_t* dst, const wchar_t* fmt, ...);

#define wcslen port_wcslen
#define wcscpy port_wcscpy
#define wcsncpy port_wcsncpy
#define wcscat port_wcscat
#define wcscmp port_wcscmp
#define wcsncmp port_wcsncmp
#define wcstombs port_wcstombs
#define mbstowcs port_mbstowcs
#define _itow port_itow
#define vswprintf port_vswprintf
#define swprintf port_swprintf
