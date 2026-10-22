// Port (docs/port.md): non-Windows versions of winmain.cpp's Windows-only functions.
#if !defined(_WIN32)
#include "Source/Function.hpp"
#include "Source/winmain.hpp"

// There is no DirectX: report the version the game needs (>= 6.1) on an NT-like system.
EXPORT void __stdcall GetDirectXVersion_4C4EC0(u32* pDXVer, u32* osKind)
{
    *pDXVer = 0x700;
    *osKind = 2; // VER_PLATFORM_WIN32_NT
}

EXPORT void __stdcall GetDirectMusicVer_4A0650()
{
}

// The game's version from its exe's version resource (10.5)
EXPORT void __stdcall GetGTA2Version_5E5D60(s32* pVerMinor, s32* pVerMajor)
{
    *pVerMinor = 10;
    *pVerMajor = 5;
}

// GTA2 Manager is a separate Windows tool; nothing to start.
EXPORT void Start_GTA2Manager_5E4DE0()
{
}
#endif
