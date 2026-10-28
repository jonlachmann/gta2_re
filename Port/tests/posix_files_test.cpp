// Port/posix_files.cpp check (ctest, Port/CMakeLists.txt; it writes files in its working folder).
#include "compat/posix_files.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"

int main()
{
    mkdir("Data", 0755);
    mkdir("Data/Audio", 0755);
    FILE* f = ::fopen("Data/Audio/Test1.SEQ", "w");
    fputs("hello", f);
    fclose(f);

    char buf[256];
    CHECK(!strcmp(port_resolve_path("data\\audio\\test1.seq", buf, sizeof(buf)), "Data/Audio/Test1.SEQ"));
    CHECK(!strcmp(port_resolve_path("data\\new.txt", buf, sizeof(buf)), "Data/new.txt")); // new file: dir resolved
    f = fopen("DATA\\audio\\TEST1.seq", "rb");
    CHECK(f != NULL);
    if (f)
    {
        fclose(f);
    }

    struct _finddata_t fd;
    intptr_t h = _findfirst("data\\audio\\*.seq", &fd);
    CHECK(h != -1 && !strcmp(fd.name, "Test1.SEQ") && fd.size == 5);
    CHECK(_findnext(h, &fd) != 0); // only one
    _findclose(h);
    CHECK(_findfirst("data\\*.gmp", &fd) == -1);

    struct _stat st;
    CHECK(_stat("data\\AUDIO\\test1.seq", &st) == 0 && st.st_size == 5);

    return CheckReport("posix_files_test");
}
