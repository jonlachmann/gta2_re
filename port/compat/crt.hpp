// Microsoft C runtime functions the game uses, and file access with Windows paths: '\' separators
// and names matched without regard to case, as the install has both "STE.RAW" and "bil.raw".
#pragma once

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

// Resolves a game path to the host file system; out must hold PATH_MAX bytes.
const char* port_resolve_path(const char* path, char* out);

FILE* port_fopen(const char* path, const char* mode);
int port_remove(const char* path);

struct _finddata_t
{
    unsigned attrib;
    time_t time_create;
    time_t time_access;
    time_t time_write;
    unsigned long size;
    char name[260];
};

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_SUBDIR 0x10

long _findfirst(const char* pattern, struct _finddata_t* pData);
int _findnext(long handle, struct _finddata_t* pData);
int _findclose(long handle);

struct _stat
{
    unsigned st_mode;
    long st_size;
};
int _stat(const char* path, struct _stat* pStat);

int _chdir(const char* path);
char* _getcwd(char* buf, int size);
int _strnicmp(const char* a, const char* b, size_t n);
int _stricmp(const char* a, const char* b);
#define _strcmpi _stricmp
char* _strupr(char* s);
char* _itoa(int value, char* dst, int radix);

#define fopen port_fopen
#define remove(p) port_remove(p)
