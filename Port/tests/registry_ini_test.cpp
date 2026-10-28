// Port/registry_ini.cpp check (ctest, Port/CMakeLists.txt; it writes gta2.ini in its working folder).
#include "../registry_ini.hpp"
#include <stdio.h>
#include <string.h>

static int gFailures;
#define CHECK(x) ((x) ? (void)0 : (void)(printf("FAILED line %d: %s\n", __LINE__, #x), gFailures++))

int main()
{
    FILE* f = fopen("gta2.ini", "w");
    fputs("[Screen]\nstart_mode=dword:1\nrendername=str:glrender.dll\n[Debug]\nmapname=hex1:77696c2e676d7000\n", f);
    fclose(f);

    HKEY hKey = 0;
    CHECK(Port_RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Screen", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS);
    DWORD value = 0, cb = sizeof(value), type = 0;
    CHECK(Port_RegQueryValueExA(hKey, "Start_Mode", 0, &type, (BYTE*)&value, &cb) == ERROR_SUCCESS);
    CHECK(type == REG_DWORD && value == 1 && cb == 4);

    char name[64];
    cb = 4;
    CHECK(Port_RegQueryValueExA(hKey, "rendername", 0, 0, (BYTE*)name, &cb) == ERROR_MORE_DATA && cb == 13);
    cb = sizeof(name);
    CHECK(Port_RegQueryValueExA(hKey, "rendername", 0, 0, (BYTE*)name, &cb) == ERROR_SUCCESS && !strcmp(name, "glrender.dll"));
    CHECK(Port_RegQueryValueExA(hKey, "missing", 0, 0, (BYTE*)name, &cb) == ERROR_FILE_NOT_FOUND);

    HKEY hDebug = 0;
    CHECK(Port_RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Debug", 0, 0, &hDebug) == ERROR_SUCCESS);
    cb = sizeof(name);
    CHECK(Port_RegQueryValueExA(hDebug, "mapname", 0, &type, (BYTE*)name, &cb) == ERROR_SUCCESS && type == REG_SZ && !strcmp(name, "wil.gmp"));
    CHECK(Port_RegDeleteValueA(hDebug, "mapname") == ERROR_SUCCESS);
    CHECK(Port_RegDeleteValueA(hDebug, "mapname") == ERROR_FILE_NOT_FOUND);

    HKEY hNew = 0;
    DWORD disposition = 0;
    CHECK(Port_RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Sound", 0, 0, &hNew) == ERROR_FILE_NOT_FOUND);
    CHECK(Port_RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Sound", 0, 0, 0, 0, 0, &hNew, &disposition) == ERROR_SUCCESS);
    CHECK(disposition == REG_CREATED_NEW_KEY);
    value = 127;
    CHECK(Port_RegSetValueExA(hNew, "CDVol", 0, REG_DWORD, (BYTE*)&value, 4) == ERROR_SUCCESS);

    char text[512] = {0};
    f = fopen("gta2.ini", "r");
    fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    CHECK(strstr(text, "[sound]\ncdvol=dword:127\n") != NULL);
    CHECK(strstr(text, "[screen]\nrendername=str:glrender.dll\nstart_mode=dword:1\n") != NULL);
    CHECK(strstr(text, "mapname") == NULL);

    printf(gFailures ? "registry_ini_test: %d failures\n" : "registry_ini_test: OK\n", gFailures);
    return gFailures != 0;
}
