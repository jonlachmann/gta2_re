// Kernel, version and shell functions: what the game asks of Windows outside the window, the
// registry and DirectX.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

#undef fopen

namespace
{
enum class HandleKind
{
    Mutex,
    Event,
    Find,
};

struct Handle
{
    HandleKind mKind;
    std::vector<std::string> mNames; // Find
    std::string mDir;
    size_t mNext = 0;
};

void FillFindData(Handle* pFind, WIN32_FIND_DATAA* pData)
{
    const std::string& name = pFind->mNames[pFind->mNext++];
    memset(pData, 0, sizeof(*pData));
    snprintf(pData->cFileName, sizeof(pData->cFileName), "%s", name.c_str());
    struct stat st;
    if (stat((pFind->mDir + name).c_str(), &st) == 0)
    {
        pData->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        pData->nFileSizeLow = (DWORD)st.st_size;
    }
}
} // namespace

extern "C" {

BOOL WINAPI CloseHandle(HANDLE h)
{
    delete static_cast<Handle*>(h);
    return TRUE;
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR)
{
    return new Handle{HandleKind::Event};
}

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR)
{
    return new Handle{HandleKind::Mutex};
}

// One instance per process: no other game holds the mutex.
HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR)
{
    return NULL;
}

BOOL WINAPI ReleaseMutex(HANDLE)
{
    return TRUE;
}

void WINAPI Sleep(DWORD ms)
{
    SDL_Delay(ms);
}

DWORD WINAPI GetTickCount()
{
    return (DWORD)SDL_GetTicks();
}

DWORD WINAPI timeGetTime()
{
    return (DWORD)SDL_GetTicks();
}

void WINAPI OutputDebugStringA(LPCSTR s)
{
    static const bool sEnabled = getenv("GTA2_DEBUG_OUTPUT") != NULL;
    if (sEnabled)
    {
        fputs(s, stderr);
    }
}

UINT WINAPI GetDriveTypeA(LPCSTR)
{
    return DRIVE_FIXED;
}

DWORD WINAPI GetFileAttributesA(LPCSTR path)
{
    char real[PATH_MAX];
    struct stat st;
    if (stat(port_resolve_path(path, real), &st) != 0)
    {
        return INVALID_FILE_ATTRIBUTES;
    }
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

HANDLE WINAPI FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA pData)
{
    std::string p(pattern);
    for (char& c : p)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    const size_t slash = p.rfind('/');
    std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
    const std::string mask = slash == std::string::npos ? p : p.substr(slash + 1);
    char real[PATH_MAX];
    if (!dir.empty())
    {
        dir = std::string(port_resolve_path(dir.c_str(), real)) + "/";
    }
    Handle* pFind = new Handle{HandleKind::Find};
    pFind->mDir = dir;
    if (DIR* d = opendir(dir.empty() ? "." : dir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            if (fnmatch(mask.c_str(), e->d_name, FNM_CASEFOLD) == 0)
            {
                pFind->mNames.push_back(e->d_name);
            }
        }
        closedir(d);
    }
    if (pFind->mNames.empty())
    {
        delete pFind;
        return INVALID_HANDLE_VALUE;
    }
    std::sort(pFind->mNames.begin(), pFind->mNames.end());
    FillFindData(pFind, pData);
    return pFind;
}

BOOL WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA pData)
{
    Handle* pFind = static_cast<Handle*>(h);
    if (pFind->mNext >= pFind->mNames.size())
    {
        return FALSE;
    }
    FillFindData(pFind, pData);
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE h)
{
    delete static_cast<Handle*>(h);
    return TRUE;
}

// Only the [section] key=value lookups the game makes on the .mmp map descriptions.
INT WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR def, LPSTR out, UINT size, LPCSTR file)
{
    snprintf(out, size, "%s", def ? def : "");
    FILE* f = port_fopen(file, "r");
    if (!f)
    {
        return (INT)strlen(out);
    }
    char line[1024];
    bool inSection = false;
    while (fgets(line, sizeof(line), f))
    {
        char* p = line;
        while (*p == ' ' || *p == '\t')
        {
            p++;
        }
        p[strcspn(p, "\r\n")] = 0;
        if (*p == '[')
        {
            char* end = strchr(p, ']');
            if (end)
            {
                *end = 0;
            }
            inSection = strcasecmp(p + 1, section) == 0;
            continue;
        }
        char* eq = strchr(p, '=');
        if (!inSection || !eq)
        {
            continue;
        }
        *eq = 0;
        char* k = p;
        char* kEnd = eq;
        while (kEnd > k && (kEnd[-1] == ' ' || kEnd[-1] == '\t'))
        {
            *--kEnd = 0;
        }
        if (strcasecmp(k, key) == 0)
        {
            char* v = eq + 1;
            while (*v == ' ' || *v == '\t')
            {
                v++;
            }
            snprintf(out, size, "%s", v);
            break;
        }
    }
    fclose(f);
    return (DWORD)strlen(out);
}

BOOL WINAPI GetUserNameA(LPSTR out, LPDWORD pSize)
{
    const char* name = getenv("USER");
    snprintf(out, *pSize, "%s", name ? name : "player");
    *pSize = (DWORD)strlen(out) + 1;
    return TRUE;
}

BOOL WINAPI GetVersionExA(OSVERSIONINFOA* pInfo)
{
    pInfo->dwMajorVersion = 5;
    pInfo->dwMinorVersion = 1;
    pInfo->dwBuildNumber = 2600;
    pInfo->dwPlatformId = VER_PLATFORM_WIN32_NT;
    pInfo->szCSDVersion[0] = 0;
    return TRUE;
}

DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR, LPDWORD pHandle)
{
    if (pHandle)
    {
        *pHandle = 0;
    }
    return 0;
}

BOOL WINAPI GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID)
{
    return FALSE;
}

BOOL WINAPI VerQueryValueA(LPCVOID, LPCSTR, LPVOID*, PUINT)
{
    return FALSE;
}

// The language part of the KLID picks the game's key names; French and German layouts differ from US.
BOOL WINAPI GetKeyboardLayoutNameA(LPSTR out)
{
    const char* lang = getenv("LANG");
    const char* klid = "00000409";
    if (lang && strncmp(lang, "fr", 2) == 0)
    {
        klid = "0000040C";
    }
    else if (lang && strncmp(lang, "de", 2) == 0)
    {
        klid = "00000407";
    }
    strcpy(out, klid);
    return TRUE;
}

HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT)
{
    return (HINSTANCE)(intptr_t)SE_ERR_FNF;
}

LPSTR WINAPI lstrcatA(LPSTR dst, LPCSTR src)
{
    return strcat(dst, src);
}

INT WINAPI lstrlenA(LPCSTR s)
{
    return s ? (INT)strlen(s) : 0;
}

INT WINAPI MultiByteToWideChar(UINT, DWORD, LPCSTR src, INT srcLen, LPWSTR dst, INT dstLen)
{
    if (srcLen < 0)
    {
        srcLen = (INT)strlen(src) + 1;
    }
    if (!dstLen)
    {
        return srcLen;
    }
    INT n = srcLen < dstLen ? srcLen : dstLen;
    for (INT i = 0; i < n; i++)
    {
        dst[i] = (unsigned char)src[i];
    }
    return n;
}

INT WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR src, INT srcLen, LPSTR dst, INT dstLen, LPCSTR def, LPBOOL pUsedDef)
{
    if (srcLen < 0)
    {
        srcLen = (INT)port_wcslen(src) + 1;
    }
    if (!dstLen)
    {
        return srcLen;
    }
    INT n = srcLen < dstLen ? srcLen : dstLen;
    for (INT i = 0; i < n; i++)
    {
        const unsigned c = (unsigned short)src[i];
        if (c > 0xFF)
        {
            dst[i] = def ? *def : '?';
            if (pUsedDef)
            {
                *pUsedDef = TRUE;
            }
        }
        else
        {
            dst[i] = (char)c;
        }
    }
    return n;
}

INT WINAPI wvsprintfA(LPSTR dst, LPCSTR fmt, __ms_va_list args)
{
    return vsprintf(dst, fmt, args);
}

INT WINAPIV wsprintfA(LPSTR dst, LPCSTR fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int n = vsprintf(dst, fmt, args);
    va_end(args);
    return n;
}

HRESULT WINAPI CoInitialize(LPVOID)
{
    return S_OK;
}

void WINAPI CoUninitialize()
{
}

// Only DirectPlay is created through COM, and the port has no network play.
HRESULT WINAPI CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID* ppv)
{
    *ppv = NULL;
    return REGDB_E_CLASSNOTREG;
}

} // extern "C"
