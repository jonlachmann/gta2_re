// LoadLibrary/GetProcAddress over DLLs linked into the executable: the renderer and video DLLs the
// game picks from the registry, and the DirectX DLLs it probes for.
#include "port_platform.hpp"
#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>
#include <dplay.h>
#include <dplobby.h>
#include <string.h>
#include <strings.h>
#include <map>
#include <string>

namespace
{
struct Dll
{
    const PortExport* mExports;
    int mCount;
};

std::map<std::string, Dll>& Dlls()
{
    static std::map<std::string, Dll> sDlls;
    return sDlls;
}

std::string BaseName(const char* path)
{
    const char* p = path;
    for (const char* s = path; *s; s++)
    {
        if (*s == '\\' || *s == '/')
        {
            p = s + 1;
        }
    }
    std::string r(p);
    for (char& c : r)
    {
        c = (char)tolower((unsigned char)c);
    }
    return r;
}

const PortExport kDInput[] = {
    {"DirectInputCreateA", (void*)&DirectInputCreateA},
};
} // namespace

void port_RegisterDll(const char* name, const PortExport* pExports, int count)
{
    Dlls()[BaseName(name)] = Dll{pExports, count};
}

extern "C" {

HMODULE WINAPI LoadLibraryA(LPCSTR name)
{
    static bool sInit;
    if (!sInit)
    {
        sInit = true;
        port_RegisterDll("dinput.dll", kDInput, 1);
    }
    auto it = Dlls().find(BaseName(name));
    if (it == Dlls().end())
    {
        SDL_Log("LoadLibrary: no built-in %s", name);
        return NULL;
    }
    return reinterpret_cast<HMODULE>(&it->second);
}

FARPROC WINAPI GetProcAddress(HMODULE h, LPCSTR name)
{
    const Dll* pDll = reinterpret_cast<const Dll*>(h);
    if (!pDll)
    {
        return NULL;
    }
    for (int i = 0; i < pDll->mCount; i++)
    {
        if (strcmp(pDll->mExports[i].mName, name) == 0)
        {
            return reinterpret_cast<FARPROC>(pDll->mExports[i].mProc);
        }
    }
    SDL_Log("GetProcAddress: %s not built in", name);
    return NULL;
}

BOOL WINAPI FreeLibrary(HMODULE)
{
    return TRUE;
}

// The port has no network play: DirectPlay cannot be created.
HRESULT WINAPI DirectPlayCreate(LPGUID, LPDIRECTPLAY* pp, IUnknown*)
{
    *pp = NULL;
    return DPERR_UNAVAILABLE;
}

HRESULT WINAPI DirectPlayLobbyCreateW(LPGUID, LPDIRECTPLAYLOBBY* pp, IUnknown*, LPVOID, DWORD)
{
    *pp = NULL;
    return DPERR_UNAVAILABLE;
}

} // extern "C"
