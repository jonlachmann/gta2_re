// Port (docs/port.md): 2-byte wide string functions with Microsoft's semantics, see compat/posix_wchar.h.
// Characters above 0xFF become '?' when narrowed; the game's text is Latin-1 and its own wide encoding.
#if !defined(_WIN32) // non-Windows builds only: Windows' C runtime has these
#include "compat/posix_wchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef swprintf
#undef vswprintf
#undef wcslen
#undef wcscpy
#undef wcsncpy
#undef wcscat
#undef wcscmp
#undef wcstombs
#undef mbstowcs

static_assert(sizeof(wchar_t) == 2, "build with -fshort-wchar");

extern "C" {

size_t port_wcslen(const wchar_t* s)
{
    size_t n = 0;
    while (s[n])
    {
        n++;
    }
    return n;
}

wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src)
{
    wchar_t* d = dst;
    while ((*d++ = *src++))
    {
    }
    return dst;
}

// Like the CRT: pads with zeros up to n, and doesn't terminate when src is n characters or longer.
wchar_t* port_wcsncpy(wchar_t* dst, const wchar_t* src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
    {
        dst[i] = src[i];
    }
    for (; i < n; i++)
    {
        dst[i] = 0;
    }
    return dst;
}

wchar_t* port_wcscat(wchar_t* dst, const wchar_t* src)
{
    port_wcscpy(dst + port_wcslen(dst), src);
    return dst;
}

int port_wcscmp(const wchar_t* a, const wchar_t* b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)(unsigned short)*a - (int)(unsigned short)*b;
}

size_t port_wcstombs(char* dst, const wchar_t* src, size_t n)
{
    size_t i = 0;
    for (; src[i]; i++)
    {
        if (dst)
        {
            if (i >= n)
            {
                return i;
            }
            dst[i] = (unsigned short)src[i] < 0x100 ? (char)src[i] : '?';
        }
    }
    if (dst && i < n)
    {
        dst[i] = 0;
    }
    return i;
}

size_t port_mbstowcs(wchar_t* dst, const char* src, size_t n)
{
    size_t i = 0;
    for (; src[i]; i++)
    {
        if (dst)
        {
            if (i >= n)
            {
                return i;
            }
            dst[i] = (unsigned char)src[i];
        }
    }
    if (dst && i < n)
    {
        dst[i] = 0;
    }
    return i;
}

wchar_t* port_itow(int value, wchar_t* buf, int radix)
{
    char tmp[40];
    if (radix == 10)
    {
        snprintf(tmp, sizeof(tmp), "%d", value);
    }
    else
    {
        // Other radixes print the value as unsigned, as _itoa does
        unsigned int v = (unsigned int)value;
        int len = 0;
        do
        {
            const int d = v % radix;
            tmp[len++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v /= radix;
        } while (v);
        for (int i = 0; i < len / 2; i++)
        {
            const char c = tmp[i];
            tmp[i] = tmp[len - 1 - i];
            tmp[len - 1 - i] = c;
        }
        tmp[len] = 0;
    }
    port_mbstowcs(buf, tmp, 40);
    return buf;
}

// Each conversion is formatted by snprintf with the same flags, width and precision, then widened;
// %s/%c take wide arguments and %S/%C narrow ones, as in Microsoft's CRT.
int port_vswprintf(wchar_t* buf, const wchar_t* fmt, va_list args)
{
    wchar_t* out = buf;
    while (*fmt)
    {
        if (*fmt != L'%')
        {
            *out++ = *fmt++;
            continue;
        }
        char spec[32];
        int len = 0;
        spec[len++] = '%';
        fmt++;
        while (*fmt && strchr("-+ #0123456789.*hlLwI", (char)*fmt) && len < 24)
        {
            spec[len++] = (char)*fmt++;
        }
        spec[len] = 0;
        const wchar_t conv = *fmt ? *fmt++ : 0;
        char tmp[512];
        tmp[0] = 0;
        int width = 0;
        bool left = false;
        for (int i = 1; i < len; i++)
        {
            if (spec[i] == '-')
            {
                left = true;
            }
            else if (spec[i] >= '1' && spec[i] <= '9' && (i == 1 || spec[i - 1] < '0' || spec[i - 1] > '9') && spec[i - 1] != '.')
            {
                width = atoi(&spec[i]);
            }
        }
        // Drop length modifiers snprintf would read differently (h, l, w, I): the game passes ints and doubles.
        int kept = 0;
        for (int i = 0; i < len; i++)
        {
            if (!strchr("hlLwI", spec[i]))
            {
                spec[kept++] = spec[i];
            }
        }
        len = kept;
        switch (conv)
        {
            case L'%':
                *out++ = L'%';
                continue;
            case L's':
            case L'S':
            {
                const bool wide = conv == L's';
                const void* p = va_arg(args, const void*);
                const size_t n = !p ? 6 : wide ? port_wcslen((const wchar_t*)p) : strlen((const char*)p);
                const int pad = width > (int)n ? width - (int)n : 0;
                for (int i = 0; !left && i < pad; i++)
                {
                    *out++ = L' ';
                }
                for (size_t i = 0; i < n; i++)
                {
                    *out++ = !p ? (wchar_t)"(null)"[i] : wide ? ((const wchar_t*)p)[i] : (wchar_t)(unsigned char)((const char*)p)[i];
                }
                for (int i = 0; left && i < pad; i++)
                {
                    *out++ = L' ';
                }
                continue;
            }
            case L'c':
            case L'C':
            {
                const int c = va_arg(args, int); // promoted
                *out++ = conv == L'c' ? (wchar_t)c : (wchar_t)(unsigned char)c;
                continue;
            }
            case L'd':
            case L'i':
            case L'u':
            case L'x':
            case L'X':
            case L'o':
                spec[len++] = (char)conv;
                spec[len] = 0;
                snprintf(tmp, sizeof(tmp), spec, va_arg(args, int));
                break;
            case L'f':
            case L'F':
            case L'e':
            case L'E':
            case L'g':
            case L'G':
                spec[len++] = (char)conv;
                spec[len] = 0;
                snprintf(tmp, sizeof(tmp), spec, va_arg(args, double));
                break;
            case L'p':
                snprintf(tmp, sizeof(tmp), "%08X", (unsigned int)(size_t)va_arg(args, void*));
                break;
            default:
                break;
        }
        for (const char* t = tmp; *t; t++)
        {
            *out++ = (wchar_t)(unsigned char)*t;
        }
    }
    *out = 0;
    return (int)(out - buf);
}

int port_swprintf(wchar_t* buf, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int n = port_vswprintf(buf, fmt, args);
    va_end(args);
    return n;
}
}
#endif
