// Port (docs/port.md): the game's file calls take Windows paths ("data\\audio\\vocals\\x.wav"). On
// non-Windows builds they go to Port/posix_files.cpp, which turns backslashes into slashes and matches
// each path component case-insensitively, as Windows does.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

struct _finddata_t
{
    unsigned attrib;
    time_t time_create;
    time_t time_access;
    time_t time_write;
    unsigned int size;
    char name[260];
};
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_SUBDIR 0x10

struct _stat // only what the game reads (st_mtime is a macro in the C libraries)
{
    unsigned int st_size;
};

#ifdef __cplusplus
extern "C" {
#endif
FILE* port_fopen(const char* path, const char* mode);
intptr_t port_findfirst(const char* pattern, struct _finddata_t* data);
int port_findnext(intptr_t handle, struct _finddata_t* data);
int port_findclose(intptr_t handle);
int port_stat(const char* path, struct _stat* st);
char* port_getcwd(char* buf, int size);
int port_chdir(const char* path);
// The path the C library can open: slashes, and the case of what exists on disk.
const char* port_resolve_path(const char* path, char* out, size_t outSize);
#ifdef __cplusplus
}
#endif

#define fopen port_fopen
#define _findfirst port_findfirst
#define _findnext port_findnext
#define _findclose port_findclose
#define _stat(path, st) port_stat(path, st)
#define _getcwd port_getcwd
#define _chdir port_chdir
