// Port (docs/port.md): the registry calls of the game, on a config file (gta2.ini in the current folder).
// Included last under GTA2_PORT by the files that use the registry, so their code runs unchanged.
#pragma once

#include <windows.h>

LONG WINAPI Port_RegOpenKeyExA(HKEY root, LPCSTR subKey, DWORD options, REGSAM sam, PHKEY phk);
LONG WINAPI Port_RegOpenKeyA(HKEY root, LPCSTR subKey, PHKEY phk);
LONG WINAPI Port_RegCreateKeyExA(HKEY root, LPCSTR subKey, DWORD reserved, LPSTR cls, DWORD options, REGSAM sam,
                                 LPSECURITY_ATTRIBUTES sa, PHKEY phk, LPDWORD disposition);
LONG WINAPI Port_RegCloseKey(HKEY hKey);
LONG WINAPI Port_RegQueryValueExA(HKEY hKey, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cbData);
LONG WINAPI Port_RegSetValueExA(HKEY hKey, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD cbData);
LONG WINAPI Port_RegDeleteValueA(HKEY hKey, LPCSTR name);

#define RegOpenKeyExA Port_RegOpenKeyExA
#define RegOpenKeyA Port_RegOpenKeyA
#define RegCreateKeyExA Port_RegCreateKeyExA
#define RegCloseKey Port_RegCloseKey
#define RegQueryValueExA Port_RegQueryValueExA
#define RegSetValueExA Port_RegSetValueExA
#define RegDeleteValueA Port_RegDeleteValueA
