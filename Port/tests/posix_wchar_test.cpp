// Port/posix_wchar.cpp check (ctest, Port/CMakeLists.txt).
#include "compat/posix_wchar.h"
#include <stdio.h>
#include "check.h"

static bool Eq(const wchar_t* a, const char* b)
{
    for (; *a && *b; a++, b++)
    {
        if (*a != (wchar_t)(unsigned char)*b)
        {
            return false;
        }
    }
    return *a == 0 && *b == 0;
}

int main()
{
    wchar_t buf[256];
    swprintf(buf, L"%d|%02d|%2d|%3.2f|%3.3f|%.1f", 42, 7, -3, 3.14159, 1.0, 2.25);
    CHECK(Eq(buf, "42|07|-3|3.14|1.000|2.2") || Eq(buf, "42|07|-3|3.14|1.000|2.3"));
    swprintf(buf, L"(%s)%s=%c%S", L"KEY", L"text", L'x', "narrow");
    CHECK(Eq(buf, "(KEY)text=xnarrow"));
    swprintf(buf, L"%5s|%-4s|%%|%u", L"ab", L"cd", 4000000000u);
    CHECK(Eq(buf, "   ab|cd  |%|4000000000"));
    CHECK(swprintf(buf, L"%d", 123) == 3 && wcslen(buf) == 3);

    wchar_t a[8];
    wcsncpy(a, L"abc", 8);
    CHECK(Eq(a, "abc") && a[7] == 0);
    wcscpy(a, L"ab");
    wcscat(a, L"cd");
    CHECK(Eq(a, "abcd") && wcscmp(a, L"abcd") == 0 && wcscmp(a, L"abce") < 0);

    char n[8];
    wchar_t w[] = {L'h', 0x00E9, 0x263A, 0};
    CHECK(wcstombs(n, w, 8) == 3 && n[0] == 'h' && (unsigned char)n[1] == 0xE9 && n[2] == '?' && n[3] == 0);
    CHECK(mbstowcs(a, "hey", 8) == 3 && Eq(a, "hey"));
    CHECK(Eq(_itow(-120, a, 10), "-120") && Eq(_itow(255, a, 16), "ff"));

    return CheckReport("posix_wchar_test");
}
