// Port (docs/port.md): GTA2_SPEED=<k> runs the game's clock k times faster, by patching the import table
// of the program (10.5.exe or gta2_port.exe) for timeGetTime, GetTickCount, QueryPerformanceCounter and
// Sleep. The game paces itself and starts attract mode by that clock, so a headless test run takes 1/k of
// the time. Windows (and wine) only.
#if defined(_WIN32)
#include "fast_clock.hpp"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{
double gSpeed = 1.0;
DWORD gTimeBase;
DWORD gTickBase;
LARGE_INTEGER gQpcBase;
DWORD(WINAPI* gRealTimeGetTime)();
DWORD(WINAPI* gRealGetTickCount)();
BOOL(WINAPI* gRealQpc)(LARGE_INTEGER*);
void(WINAPI* gRealSleep)(DWORD);

DWORD WINAPI FastTimeGetTime()
{
    return gTimeBase + (DWORD)((gRealTimeGetTime() - gTimeBase) * gSpeed);
}
DWORD WINAPI FastGetTickCount()
{
    return gTickBase + (DWORD)((gRealGetTickCount() - gTickBase) * gSpeed);
}
BOOL WINAPI FastQpc(LARGE_INTEGER* p)
{
    LARGE_INTEGER now;
    const BOOL ok = gRealQpc(&now);
    p->QuadPart = gQpcBase.QuadPart + (LONGLONG)((now.QuadPart - gQpcBase.QuadPart) * gSpeed);
    return ok;
}
void WINAPI FastSleep(DWORD ms)
{
    gRealSleep((DWORD)(ms / gSpeed));
}

// Points the program's imports of <dll>!<name> at pNew; returns the old target
void* PatchImport(const char* dll, const char* name, void* pNew)
{
    unsigned char* base = (unsigned char*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    void* pOld = (void*)GetProcAddress(GetModuleHandleA(dll), name);
    if (!dir.VirtualAddress || !pOld)
    {
        return pOld;
    }
    for (IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++)
    {
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; thunk->u1.Function; thunk++)
        {
            if ((void*)thunk->u1.Function == pOld)
            {
                DWORD protect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &protect);
                thunk->u1.Function = (ULONG_PTR)pNew;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), protect, &protect);
            }
        }
    }
    return pOld;
}
} // namespace

void FastClock_Install()
{
    const char* s = getenv("GTA2_SPEED");
    if (!s || gRealTimeGetTime || atof(s) <= 1.0)
    {
        return;
    }
    gSpeed = atof(s);
    LoadLibraryA("winmm.dll");
    gRealTimeGetTime = (DWORD(WINAPI*)())PatchImport("winmm.dll", "timeGetTime", (void*)FastTimeGetTime);
    gRealGetTickCount = (DWORD(WINAPI*)())PatchImport("kernel32.dll", "GetTickCount", (void*)FastGetTickCount);
    gRealQpc = (BOOL(WINAPI*)(LARGE_INTEGER*))PatchImport("kernel32.dll", "QueryPerformanceCounter", (void*)FastQpc);
    gRealSleep = (void(WINAPI*)(DWORD))PatchImport("kernel32.dll", "Sleep", (void*)FastSleep);
    gTimeBase = gRealTimeGetTime();
    gTickBase = gRealGetTickCount();
    gRealQpc(&gQpcBase);
}
#endif
