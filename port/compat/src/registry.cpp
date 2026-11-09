// The registry as a text file in the game folder (gta2_registry.ini), one [key] section per key:
//   name = dword:<hex> | sz:<text> | hex:<bytes>
// Keys and value names ignore case, as in the Windows registry.
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#undef fopen

namespace
{
const char kFileName[] = "gta2_registry.ini";

struct Value
{
    DWORD mType;
    std::vector<BYTE> mData;
};

struct Key
{
    std::string mName; // as first written
    std::map<std::string, std::pair<std::string, Value>> mValues; // lower case name -> (name, value)
};

// Function-local: global constructors of the game read the registry.
std::map<std::string, Key>& Keys() // lower case path -> key
{
    static std::map<std::string, Key> sKeys;
    return sKeys;
}
bool sLoaded;

std::string Lower(const std::string& s)
{
    std::string r(s);
    for (char& c : r)
    {
        c = (char)tolower((unsigned char)c);
    }
    return r;
}

std::string RootName(HKEY root)
{
    if (root == HKEY_LOCAL_MACHINE)
    {
        return "HKLM";
    }
    if (root == HKEY_CURRENT_USER)
    {
        return "HKCU";
    }
    return "";
}

// Opened keys are pointers to Key; the predefined roots are not.
Key* FromHandle(HKEY h)
{
    if (!h || RootName(h) != "")
    {
        return NULL;
    }
    return reinterpret_cast<Key*>(h);
}

// While a menu is open its changes are saved as it closes: a save rewrites the file, tens of
// milliseconds on the 3DS, for each step of a setting.
bool sDeferSaves;
bool sUnsaved;

void Save()
{
    if (sDeferSaves)
    {
        sUnsaved = true;
        return;
    }
    sUnsaved = false;
    FILE* f = fopen(kFileName, "w");
    if (!f)
    {
        return;
    }
    for (auto& k : Keys())
    {
        fprintf(f, "[%s]\n", k.second.mName.c_str());
        for (auto& v : k.second.mValues)
        {
            const Value& val = v.second.second;
            fprintf(f, "%s = ", v.second.first.c_str());
            if (val.mType == REG_DWORD && val.mData.size() == 4)
            {
                DWORD d;
                memcpy(&d, val.mData.data(), 4);
                fprintf(f, "dword:%08x\n", d);
            }
            else if (val.mType == REG_SZ)
            {
                fprintf(f, "sz:%s\n", reinterpret_cast<const char*>(val.mData.data()));
            }
            else
            {
                fprintf(f, "hex(%u):", val.mType);
                for (size_t i = 0; i < val.mData.size(); i++)
                {
                    fprintf(f, "%s%02x", i ? "," : "", val.mData[i]);
                }
                fprintf(f, "\n");
            }
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

Key& GetKey(const std::string& path)
{
    Key& k = Keys()[Lower(path)];
    if (k.mName.empty())
    {
        k.mName = path;
    }
    return k;
}

void SetValue(Key& k, const std::string& name, DWORD type, const void* data, size_t size)
{
    Value v;
    v.mType = type;
    v.mData.assign(static_cast<const BYTE*>(data), static_cast<const BYTE*>(data) + size);
    k.mValues[Lower(name)] = std::make_pair(name, v);
}

// What GTA2's installer writes and the game does not create itself. Movies are off: the Bink
// library is a stub.
void SetDefaults()
{
    DWORD cities = 3;
    SetValue(GetKey("HKLM\\Software\\freeloader.com\\GTA2"), "CityInstalled", REG_DWORD, &cities, 4);
    DWORD noMovie = 0;
    SetValue(GetKey("HKLM\\SOFTWARE\\DMA Design Ltd\\GTA2\\Screen"), "do_play_movie", REG_DWORD, &noMovie, 4);
}

void Load()
{
    if (sLoaded)
    {
        return;
    }
    sLoaded = true;
    FILE* f = fopen(kFileName, "r");
    if (!f)
    {
        SetDefaults();
        Save();
        return;
    }
    char line[4096];
    Key* pKey = NULL;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[')
        {
            char* end = strrchr(line, ']');
            if (end)
            {
                *end = 0;
            }
            pKey = &GetKey(line + 1);
            continue;
        }
        char* eq = strstr(line, " = ");
        if (!pKey || !eq)
        {
            continue;
        }
        *eq = 0;
        const std::string name(line);
        const char* v = eq + 3;
        if (strncmp(v, "dword:", 6) == 0)
        {
            DWORD d = strtoul(v + 6, NULL, 16);
            SetValue(*pKey, name, REG_DWORD, &d, 4);
        }
        else if (strncmp(v, "sz:", 3) == 0)
        {
            SetValue(*pKey, name, REG_SZ, v + 3, strlen(v + 3) + 1);
        }
        else if (strncmp(v, "hex(", 4) == 0)
        {
            char* p;
            const DWORD type = strtoul(v + 4, &p, 10);
            std::vector<BYTE> bytes;
            if (*p == ')' && p[1] == ':')
            {
                p += 2;
                while (*p)
                {
                    bytes.push_back((BYTE)strtoul(p, &p, 16));
                    if (*p == ',')
                    {
                        p++;
                    }
                }
            }
            SetValue(*pKey, name, type, bytes.data(), bytes.size());
        }
    }
    fclose(f);
}

LONG Open(HKEY root, LPCSTR subKey, PHKEY pResult, bool create)
{
    Load();
    std::string path;
    if (Key* pParent = FromHandle(root))
    {
        path = pParent->mName;
    }
    else
    {
        path = RootName(root);
    }
    if (subKey && *subKey)
    {
        path += "\\";
        path += subKey;
    }
    auto it = Keys().find(Lower(path));
    if (it == Keys().end())
    {
        if (!create)
        {
            *pResult = NULL;
            return ERROR_FILE_NOT_FOUND;
        }
        *pResult = reinterpret_cast<HKEY>(&GetKey(path));
        Save();
        return ERROR_SUCCESS;
    }
    *pResult = reinterpret_cast<HKEY>(&it->second);
    return ERROR_SUCCESS;
}
void SaveUnsaved()
{
    sDeferSaves = false;
    if (sUnsaved)
    {
        Save();
    }
}
} // namespace

void port_DeferRegistrySaves(bool defer)
{
    static bool sAtExit;
    if (defer && !sAtExit)
    {
        sAtExit = true;
        atexit(SaveUnsaved);
    }
    sDeferSaves = defer;
    if (!defer)
    {
        SaveUnsaved();
    }
}

extern "C" {

LSTATUS WINAPI RegOpenKeyExA(HKEY root, LPCSTR subKey, DWORD, REGSAM, PHKEY pResult)
{
    return Open(root, subKey, pResult, false);
}

LSTATUS WINAPI RegOpenKeyA(HKEY root, LPCSTR subKey, PHKEY pResult)
{
    return Open(root, subKey, pResult, true);
}

LSTATUS WINAPI RegCreateKeyExA(HKEY root, LPCSTR subKey, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY pResult, LPDWORD pDisposition)
{
    if (pDisposition)
    {
        *pDisposition = REG_OPENED_EXISTING_KEY;
    }
    return Open(root, subKey, pResult, true);
}

LSTATUS WINAPI RegCloseKey(HKEY)
{
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegQueryValueExA(HKEY h, LPCSTR name, LPDWORD, LPDWORD pType, LPBYTE pData, LPDWORD pSize)
{
    Key* pKey = FromHandle(h);
    if (!pKey)
    {
        return ERROR_INVALID_HANDLE;
    }
    auto it = pKey->mValues.find(Lower(name ? name : ""));
    if (it == pKey->mValues.end())
    {
        return ERROR_FILE_NOT_FOUND;
    }
    const Value& v = it->second.second;
    if (pType)
    {
        *pType = v.mType;
    }
    if (pData)
    {
        if (!pSize || *pSize < v.mData.size())
        {
            if (pSize)
            {
                *pSize = (DWORD)v.mData.size();
            }
            return ERROR_MORE_DATA;
        }
        memcpy(pData, v.mData.data(), v.mData.size());
    }
    if (pSize)
    {
        *pSize = (DWORD)v.mData.size();
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegSetValueExA(HKEY h, LPCSTR name, DWORD, DWORD type, const BYTE* pData, DWORD size)
{
    Key* pKey = FromHandle(h);
    if (!pKey)
    {
        return ERROR_INVALID_HANDLE;
    }
    // The game writes some values unchanged at each start, and a save rewrites the file.
    const std::map<std::string, std::pair<std::string, Value>>::const_iterator it = pKey->mValues.find(Lower(name ? name : ""));
    if (it != pKey->mValues.end() && it->second.second.mType == type && it->second.second.mData.size() == size &&
        (!size || memcmp(it->second.second.mData.data(), pData, size) == 0))
    {
        return ERROR_SUCCESS;
    }
    SetValue(*pKey, name ? name : "", type, pData, size);
    Save();
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDeleteValueA(HKEY h, LPCSTR name)
{
    Key* pKey = FromHandle(h);
    if (!pKey || !pKey->mValues.erase(Lower(name ? name : "")))
    {
        return ERROR_FILE_NOT_FOUND;
    }
    Save();
    return ERROR_SUCCESS;
}

} // extern "C"
