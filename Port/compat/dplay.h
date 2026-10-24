// Port (docs/port.md): non-Windows builds have no DirectPlay (NetPlay.cpp isn't built; the game's network layer
// is Port/net/engine/netplay.cpp). These are the DirectPlay types NetPlay.hpp's class layout names.
#pragma once
#include "objbase.h"

typedef DWORD DPID;
typedef struct DPNAME
{
    DWORD dwSize;
    DWORD dwFlags;
    union
    {
        LPWSTR lpszShortName;
        LPSTR lpszShortNameA;
    };
    union
    {
        LPWSTR lpszLongName;
        LPSTR lpszLongNameA;
    };
} DPNAME, *LPDPNAME;
typedef struct DPSESSIONDESC2
{
    DWORD dwSize;
    DWORD dwFlags;
    GUID guidInstance;
    GUID guidApplication;
    DWORD dwMaxPlayers;
    DWORD dwCurrentPlayers;
    union
    {
        LPWSTR lpszSessionName;
        LPSTR lpszSessionNameA;
    };
    union
    {
        LPWSTR lpszPassword;
        LPSTR lpszPasswordA;
    };
    DWORD dwReserved1;
    DWORD dwReserved2;
    DWORD dwUser1;
    DWORD dwUser2;
    DWORD dwUser3;
    DWORD dwUser4;
} DPSESSIONDESC2, *LPDPSESSIONDESC2;
typedef struct DPCOMPORTADDRESS
{
    DWORD dwComPort;
    DWORD dwBaudRate;
    DWORD dwStopBits;
    DWORD dwParity;
    DWORD dwFlowControl;
} DPCOMPORTADDRESS;
struct IDirectPlay3;
