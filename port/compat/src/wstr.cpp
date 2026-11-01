#include <stdio.h>
#include <string.h>
#include <string>

size_t port_wcslen(const wchar_t* s)
{
    const wchar_t* p = s;
    while (*p)
    {
        p++;
    }
    return p - s;
}

wchar_t* port_wcscpy(wchar_t* dst, const wchar_t* src)
{
    wchar_t* d = dst;
    while ((*d++ = *src++) != 0)
    {
    }
    return dst;
}

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

int port_wcsncmp(const wchar_t* a, const wchar_t* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i] || !a[i])
        {
            return (int)(unsigned short)a[i] - (int)(unsigned short)b[i];
        }
    }
    return 0;
}

// Latin-1, like the "C" locale of Microsoft's CRT; other characters fail the conversion.
size_t port_wcstombs(char* dst, const wchar_t* src, size_t n)
{
    size_t i = 0;
    for (; src[i]; i++)
    {
        if ((unsigned short)src[i] > 0xFF)
        {
            return (size_t)-1;
        }
        if (dst)
        {
            if (i >= n)
            {
                return n;
            }
            dst[i] = (char)src[i];
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
                return n;
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

wchar_t* port_itow(int value, wchar_t* dst, int radix)
{
    char buf[40];
    unsigned v = (unsigned)value;
    bool neg = radix == 10 && value < 0;
    if (neg)
    {
        v = 0u - v;
    }
    int i = 0;
    do
    {
        unsigned d = v % radix;
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= radix;
    } while (v);
    int o = 0;
    if (neg)
    {
        dst[o++] = L'-';
    }
    while (i)
    {
        dst[o++] = buf[--i];
    }
    dst[o] = 0;
    return dst;
}

// std::u16string, not std::wstring: wchar_t traits call the C library, which has a 4-byte wchar_t.
// Microsoft semantics: %s and %c take wide arguments, %S and %C narrow ones; numbers go through snprintf.
int port_vswprintf(wchar_t* dst, const wchar_t* fmt, va_list args)
{
    std::u16string out;
    char spec[32];
    char num[512];
    while (*fmt)
    {
        if (*fmt != L'%')
        {
            out += (char16_t)*fmt++;
            continue;
        }
        if (fmt[1] == L'%')
        {
            out += u'%';
            fmt += 2;
            continue;
        }
        int n = 0;
        spec[n++] = '%';
        fmt++;
        while (*fmt && strchr("-+ #0123456789.*", (char)*fmt) && n < 24)
        {
            if (*fmt == L'*')
            {
                n += snprintf(spec + n, sizeof(spec) - n, "%d", va_arg(args, int));
                fmt++;
                continue;
            }
            spec[n++] = (char)*fmt++;
        }
        bool isLong = false;
        bool isShort = false;
        while (*fmt == L'l' || *fmt == L'h')
        {
            (*fmt == L'l' ? isLong : isShort) = true;
            fmt++;
        }
        const wchar_t c = *fmt ? *fmt++ : 0;
        switch (c)
        {
            case L's':
            case L'S':
            {
                const bool wide = (c == L's') ? !isShort : isLong;
                spec[n] = 0;
                int width = 0;
                int precision = -1;
                bool left = strchr(spec, '-') != NULL;
                sscanf(spec + 1 + (left ? 1 : 0), "%d", &width);
                if (const char* dot = strchr(spec, '.'))
                {
                    precision = atoi(dot + 1);
                }
                std::u16string s;
                if (wide)
                {
                    const wchar_t* p = va_arg(args, const wchar_t*);
                    for (; p && *p && (precision < 0 || (int)s.size() < precision); p++)
                    {
                        s += (char16_t)*p;
                    }
                }
                else
                {
                    const char* p = va_arg(args, const char*);
                    for (; p && *p && (precision < 0 || (int)s.size() < precision); p++)
                    {
                        s += (char16_t)(unsigned char)*p;
                    }
                }
                if (!left)
                {
                    out.append(width > (int)s.size() ? width - s.size() : 0, u' ');
                }
                out += s;
                if (left)
                {
                    out.append(width > (int)s.size() ? width - s.size() : 0, u' ');
                }
                break;
            }
            case L'c':
            case L'C':
                out += (char16_t)va_arg(args, int);
                break;
            case L'd':
            case L'i':
            case L'u':
            case L'x':
            case L'X':
            case L'o':
                spec[n++] = (char)c;
                spec[n] = 0;
                snprintf(num, sizeof(num), spec, va_arg(args, int));
                for (const char* p = num; *p; p++)
                {
                    out += (char16_t)*p;
                }
                break;
            case L'f':
            case L'e':
            case L'E':
            case L'g':
            case L'G':
                spec[n++] = (char)c;
                spec[n] = 0;
                snprintf(num, sizeof(num), spec, va_arg(args, double));
                for (const char* p = num; *p; p++)
                {
                    out += (char16_t)*p;
                }
                break;
            case L'p':
                snprintf(num, sizeof(num), "%08X", (unsigned)(size_t)va_arg(args, void*));
                for (const char* p = num; *p; p++)
                {
                    out += (char16_t)*p;
                }
                break;
            default:
                break;
        }
    }
    memcpy(dst, out.c_str(), (out.size() + 1) * sizeof(char16_t));
    return (int)out.size();
}

int port_swprintf(wchar_t* dst, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int n = port_vswprintf(dst, fmt, args);
    va_end(args);
    return n;
}
