// The DirectPlay lobby declarations the network code compiles against (see dplay.h).
#pragma once

#include "dplay.h"

DEFINE_GUID(IID_IDirectPlayLobby, 0xAF465C71, 0x9588, 0x11CF, 0xA0, 0x20, 0x00, 0xAA, 0x00, 0x61, 0x57, 0xAC);
DEFINE_GUID(IID_IDirectPlayLobby2, 0x0194C220, 0xA303, 0x11D0, 0x9C, 0x4F, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);
DEFINE_GUID(DPAID_ServiceProvider, 0x07D916C0, 0xE0AF, 0x11CF, 0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);
DEFINE_GUID(DPAID_INetW, 0xE63232A0, 0x9DBF, 0x11D0, 0x9C, 0xC1, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);
DEFINE_GUID(DPAID_ComPort, 0xF2F0CE00, 0xE0AF, 0x11CF, 0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);
DEFINE_GUID(DPAID_Modem, 0xF6DCC200, 0xA2FE, 0x11D0, 0x9C, 0x4F, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);

typedef struct tagDPCOMPORTADDRESS
{
    DWORD dwComPort;
    DWORD dwBaudRate;
    DWORD dwStopBits;
    DWORD dwParity;
    DWORD dwFlowControl;
} DPCOMPORTADDRESS;

typedef struct DPCOMPOUNDADDRESSELEMENT
{
    GUID guidDataType;
    DWORD dwDataSize;
    LPVOID lpData;
} DPCOMPOUNDADDRESSELEMENT, *LPDPCOMPOUNDADDRESSELEMENT;
typedef const DPCOMPOUNDADDRESSELEMENT* LPCDPCOMPOUNDADDRESSELEMENT;

typedef BOOL(CALLBACK* LPDPENUMADDRESSCALLBACK)(REFGUID dataType, DWORD size, LPCVOID pData, LPVOID context);
typedef BOOL(CALLBACK* LPDPLENUMADDRESSTYPESCALLBACK)(REFGUID dataType, LPVOID context, DWORD flags);
typedef BOOL(CALLBACK* LPDPLENUMLOCALAPPLICATIONSCALLBACK)(const struct DPLAPPINFO* pInfo, LPVOID context, DWORD flags);

DECLARE_INTERFACE_(IDirectPlayLobby, IUnknown)
{
    STDMETHOD(Connect)(DWORD flags, LPDIRECTPLAY2* ppDP, IUnknown* outer) PURE;
    STDMETHOD(CreateAddress)(REFGUID sp, REFGUID dataType, LPCVOID pData, DWORD size, LPVOID pAddress, LPDWORD pAddressSize) PURE;
    STDMETHOD(EnumAddress)(LPDPENUMADDRESSCALLBACK callback, LPCVOID pAddress, DWORD size, LPVOID context) PURE;
    STDMETHOD(EnumAddressTypes)(LPDPLENUMADDRESSTYPESCALLBACK callback, REFGUID sp, LPVOID context, DWORD flags) PURE;
    STDMETHOD(EnumLocalApplications)(LPDPLENUMLOCALAPPLICATIONSCALLBACK callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(GetConnectionSettings)(DWORD appId, LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(ReceiveLobbyMessage)(DWORD flags, DWORD appId, LPDWORD pMessageFlags, LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(RunApplication)(DWORD flags, LPDWORD pAppId, LPDPLCONNECTION connection, HANDLE event) PURE;
    STDMETHOD(SendLobbyMessage)(DWORD flags, DWORD appId, LPVOID pData, DWORD size) PURE;
    STDMETHOD(SetConnectionSettings)(DWORD flags, DWORD appId, LPDPLCONNECTION connection) PURE;
    STDMETHOD(SetLobbyMessageEvent)(DWORD flags, DWORD appId, HANDLE event) PURE;
};
typedef IDirectPlayLobby* LPDIRECTPLAYLOBBY;

DECLARE_INTERFACE_(IDirectPlayLobby2, IDirectPlayLobby)
{
    STDMETHOD(CreateCompoundAddress)(LPCDPCOMPOUNDADDRESSELEMENT elements, DWORD count, LPVOID pAddress, LPDWORD pSize) PURE;
};

extern "C" HRESULT WINAPI DirectPlayLobbyCreateW(LPGUID guid, LPDIRECTPLAYLOBBY* ppLobby, IUnknown* outer, LPVOID pData, DWORD size);
