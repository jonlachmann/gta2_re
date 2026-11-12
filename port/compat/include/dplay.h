// The DirectPlay 3 declarations the network code compiles against; the port has no network play, so
// DirectPlayCreate and CoCreateInstance fail and none of this runs.
#pragma once

#include "windows.h"

DEFINE_GUID(IID_IDirectPlay, 0x5454E9A0, 0xDB65, 0x11CE, 0x92, 0x1C, 0x00, 0xAA, 0x00, 0x6C, 0x49, 0x72);
DEFINE_GUID(IID_IDirectPlay3, 0x133EFE40, 0x32DC, 0x11D0, 0x9C, 0xFB, 0x00, 0xA0, 0xC9, 0x0A, 0x43, 0xCB);
DEFINE_GUID(CLSID_DirectPlay, 0xD1EB6D20, 0x8923, 0x11D0, 0x9D, 0x97, 0x00, 0xA0, 0xC9, 0x0A, 0x43, 0xCB);
DEFINE_GUID(DPSPGUID_IPX, 0x685BC400, 0x9D2C, 0x11CF, 0xA9, 0xCD, 0x00, 0xAA, 0x00, 0x68, 0x86, 0xE3);
DEFINE_GUID(DPSPGUID_TCPIP, 0x36E95EE0, 0x8577, 0x11CF, 0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82);
DEFINE_GUID(DPSPGUID_SERIAL, 0x0F1D6860, 0x88D9, 0x11CF, 0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);
DEFINE_GUID(DPSPGUID_MODEM, 0x44EAA760, 0xCB68, 0x11CF, 0x9C, 0x4E, 0x00, 0xA0, 0xC9, 0x05, 0x42, 0x5E);

// The network code spells these DWORDs unsigned long, 8 bytes on LP64; none of it runs, the types only agree.
#if defined(__LP64__)
typedef unsigned long DP_ULONG;
#else
typedef DWORD DP_ULONG;
#endif
typedef DP_ULONG DPID, *LPDPID;

#define DP_OK S_OK
#define MAKE_DPHRESULT(code) MAKE_HRESULT(1, 0x877, code)
#define DPERR_BUFFERTOOSMALL MAKE_DPHRESULT(30)
#define DPERR_UNAVAILABLE MAKE_DPHRESULT(250)
#define DPERR_USERCANCEL MAKE_DPHRESULT(280)
#define DPERR_CONNECTING MAKE_DPHRESULT(350)

#define DPENUMSESSIONS_AVAILABLE 0x00000001
#define DPENUMSESSIONS_ASYNC 0x00000010
#define DPENUMSESSIONS_RETURNSTATUS 0x00000080
#define DPOPEN_JOIN 0x00000001
#define DPOPEN_CREATE 0x00000002
#define DPOPEN_RETURNSTATUS DPENUMSESSIONS_RETURNSTATUS
#define DPPLAYERTYPE_GROUP 0x00000000
#define DPPLAYERTYPE_PLAYER 0x00000001
#define DPRECEIVE_ALL 0x00000001
#define DPSEND_GUARANTEED 0x00000001
#define DPSESSION_JOINDISABLED 0x00000020
#define DPSESSION_DIRECTPLAYPROTOCOL 0x00002000
#define DPSYS_CREATEPLAYERORGROUP 0x0003
#define DPSYS_DESTROYPLAYERORGROUP 0x0005
#define DPSYS_ADDPLAYERTOGROUP 0x0007
#define DPSYS_DELETEPLAYERFROMGROUP 0x0021
#define DPSYS_SESSIONLOST 0x0031
#define DPSYS_SETPLAYERORGROUPDATA 0x0102
#define DPSYS_CHAT 0x0109

typedef struct tagDPNAME
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
typedef const DPNAME* LPCDPNAME;

typedef struct tagDPSESSIONDESC2
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
typedef const DPSESSIONDESC2* LPCDPSESSIONDESC2;

typedef struct tagDPCHAT
{
    DWORD dwSize;
    DWORD dwFlags;
    union
    {
        LPWSTR lpszMessage;
        LPSTR lpszMessageA;
    };
} DPCHAT, *LPDPCHAT;

typedef struct tagDPMSG_GENERIC
{
    DWORD dwType;
} DPMSG_GENERIC;

typedef struct tagDPMSG_CHAT
{
    DWORD dwType;
    DWORD dwFlags;
    DPID idFromPlayer;
    DPID idToPlayer;
    DPID idToGroup;
    LPDPCHAT lpChat;
} DPMSG_CHAT;

typedef struct tagDPMSG_CREATEPLAYERORGROUP
{
    DWORD dwType;
    DWORD dwPlayerType;
    DPID dpId;
    DWORD dwCurrentPlayers;
    LPVOID lpData;
    DWORD dwDataSize;
    DPNAME dpnName;
    DPID dpIdParent;
    DWORD dwFlags;
} DPMSG_CREATEPLAYERORGROUP;

typedef struct tagDPMSG_DESTROYPLAYERORGROUP
{
    DWORD dwType;
    DWORD dwPlayerType;
    DPID dpId;
    LPVOID lpLocalData;
    DWORD dwLocalDataSize;
    LPVOID lpRemoteData;
    DWORD dwRemoteDataSize;
    DPNAME dpnName;
    DPID dpIdParent;
    DWORD dwFlags;
} DPMSG_DESTROYPLAYERORGROUP;

typedef struct tagDPMSG_ADDPLAYERTOGROUP
{
    DWORD dwType;
    DPID dpIdGroup;
    DPID dpIdPlayer;
} DPMSG_ADDPLAYERTOGROUP, DPMSG_DELETEPLAYERFROMGROUP;

typedef struct tagDPMSG_SETPLAYERORGROUPDATA
{
    DWORD dwType;
    DWORD dwPlayerType;
    DPID dpId;
    LPVOID lpData;
    DWORD dwDataSize;
} DPMSG_SETPLAYERORGROUPDATA;

typedef struct DPCAPS* LPDPCAPS;
typedef struct DPLCONNECTION* LPDPLCONNECTION;
typedef const struct DPSECURITYDESC* LPCDPSECURITYDESC;
typedef const struct DPCREDENTIALS* LPCDPCREDENTIALS;

typedef BOOL(CALLBACK* LPDPENUMCONNECTIONSCALLBACK)(LPCGUID sp, LPVOID connection, DP_ULONG connectionSize, LPCDPNAME name, DP_ULONG flags, LPVOID context);
typedef BOOL(CALLBACK* LPDPENUMPLAYERSCALLBACK2)(DPID id, DWORD playerType, LPCDPNAME name, DWORD flags, LPVOID context);
typedef BOOL(CALLBACK* LPDPENUMSESSIONSCALLBACK2)(LPCDPSESSIONDESC2 desc, LPDWORD pTimeOut, DWORD flags, LPVOID context);

DECLARE_INTERFACE_(IDirectPlay, IUnknown){};
typedef IDirectPlay* LPDIRECTPLAY;
struct IDirectPlay2;
typedef IDirectPlay2* LPDIRECTPLAY2;

DECLARE_INTERFACE_(IDirectPlay3, IUnknown)
{
    STDMETHOD(AddPlayerToGroup)(DPID group, DPID player) PURE;
    STDMETHOD(Close)() PURE;
    STDMETHOD(CreateGroup)(LPDPID pGroup, LPDPNAME name, LPVOID pData, DWORD size, DWORD flags) PURE;
    STDMETHOD(CreatePlayer)(LPDPID pPlayer, LPDPNAME name, HANDLE event, LPVOID pData, DWORD size, DWORD flags) PURE;
    STDMETHOD(DeletePlayerFromGroup)(DPID group, DPID player) PURE;
    STDMETHOD(DestroyGroup)(DPID group) PURE;
    STDMETHOD(DestroyPlayer)(DPID player) PURE;
    STDMETHOD(EnumGroupPlayers)(DPID group, LPGUID instance, LPDPENUMPLAYERSCALLBACK2 callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(EnumGroups)(LPGUID instance, LPDPENUMPLAYERSCALLBACK2 callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(EnumPlayers)(LPGUID instance, LPDPENUMPLAYERSCALLBACK2 callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(EnumSessions)(LPDPSESSIONDESC2 desc, DWORD timeout, LPDPENUMSESSIONSCALLBACK2 callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(GetCaps)(LPDPCAPS caps, DWORD flags) PURE;
    STDMETHOD(GetGroupData)(DPID group, LPVOID pData, LPDWORD pSize, DWORD flags) PURE;
    STDMETHOD(GetGroupName)(DPID group, LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(GetMessageCount)(DPID player, LPDWORD pCount) PURE;
    STDMETHOD(GetPlayerAddress)(DPID player, LPVOID pData, DP_ULONG* pSize) PURE;
    STDMETHOD(GetPlayerCaps)(DPID player, LPDPCAPS caps, DWORD flags) PURE;
    STDMETHOD(GetPlayerData)(DPID player, LPVOID pData, LPDWORD pSize, DWORD flags) PURE;
    STDMETHOD(GetPlayerName)(DPID player, LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(GetSessionDesc)(LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(Initialize)(LPGUID guid) PURE;
    STDMETHOD(Open)(LPDPSESSIONDESC2 desc, DWORD flags) PURE;
    STDMETHOD(Receive)(LPDPID pFrom, LPDPID pTo, DWORD flags, LPVOID pData, DP_ULONG* pSize) PURE;
    STDMETHOD(Send)(DPID from, DPID to, DWORD flags, LPVOID pData, DWORD size) PURE;
    STDMETHOD(SetGroupData)(DPID group, LPVOID pData, DWORD size, DWORD flags) PURE;
    STDMETHOD(SetGroupName)(DPID group, LPDPNAME name, DWORD flags) PURE;
    STDMETHOD(SetPlayerData)(DPID player, LPVOID pData, DWORD size, DWORD flags) PURE;
    STDMETHOD(SetPlayerName)(DPID player, LPDPNAME name, DWORD flags) PURE;
    STDMETHOD(SetSessionDesc)(LPDPSESSIONDESC2 desc, DWORD flags) PURE;
    STDMETHOD(AddGroupToGroup)(DPID parent, DPID group) PURE;
    STDMETHOD(CreateGroupInGroup)(DPID parent, LPDPID pGroup, LPDPNAME name, LPVOID pData, DWORD size, DWORD flags) PURE;
    STDMETHOD(DeleteGroupFromGroup)(DPID parent, DPID group) PURE;
    STDMETHOD(EnumConnections)(LPCGUID application, LPDPENUMCONNECTIONSCALLBACK callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(EnumGroupsInGroup)(DPID group, LPGUID instance, LPDPENUMPLAYERSCALLBACK2 callback, LPVOID context, DWORD flags) PURE;
    STDMETHOD(GetGroupConnectionSettings)(DWORD flags, DPID group, LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(InitializeConnection)(LPVOID connection, DWORD flags) PURE;
    STDMETHOD(SecureOpen)(LPCDPSESSIONDESC2 desc, DWORD flags, LPCDPSECURITYDESC security, LPCDPCREDENTIALS credentials) PURE;
    STDMETHOD(SendChatMessage)(DPID from, DPID to, DWORD flags, LPDPCHAT message) PURE;
    STDMETHOD(SetGroupConnectionSettings)(DWORD flags, DPID group, LPDPLCONNECTION connection) PURE;
    STDMETHOD(StartSession)(DWORD flags, DPID group) PURE;
    STDMETHOD(GetGroupFlags)(DPID group, LPDWORD pFlags) PURE;
    STDMETHOD(GetGroupParent)(DPID group, LPDPID pParent) PURE;
    STDMETHOD(GetPlayerAccount)(DPID player, DWORD flags, LPVOID pData, LPDWORD pSize) PURE;
    STDMETHOD(GetPlayerFlags)(DPID player, LPDWORD pFlags) PURE;
};

extern "C" HRESULT WINAPI DirectPlayCreate(LPGUID guid, LPDIRECTPLAY* ppDP, IUnknown* outer);
