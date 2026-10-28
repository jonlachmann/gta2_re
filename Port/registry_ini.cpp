// Port (docs/port.md): registry keys as sections of gta2.ini. "SOFTWARE\DMA Design Ltd\GTA2\Screen" is
// [Screen]; other keys keep their full path. Values are "name=dword:<n>", "name=str:<text>" or
// "name=hex:<bytes>". The file is read once and written after every change.
// The whole file is rewritten on each set; fine for the few settings writes a session makes.
#include "registry_ini.hpp"
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
struct Value
{
    DWORD type;
    std::vector<BYTE> data;
};
typedef std::map<std::string, Value> Section;
// The HKEY handed out is a Section*. Built on first use: other globals' constructors read settings
// (cSampleManager's), and in some link orders they run first.
std::map<std::string, Section>& Sections()
{
    static std::map<std::string, Section> sSections;
    return sSections;
}
bool gLoaded;
const char* const kFile = "gta2.ini";
const char* const kGameKey = "software\\dma design ltd\\gta2\\";

std::string Lower(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
    {
        s[i] = (char)tolower((unsigned char)s[i]);
    }
    return s;
}

// Registry names are case-insensitive: keys and values are stored lower-case.
std::string SectionName(LPCSTR subKey)
{
    std::string name = Lower(subKey ? subKey : "");
    if (name.compare(0, strlen(kGameKey), kGameKey) == 0)
    {
        name = name.substr(strlen(kGameKey));
    }
    return name;
}

void Save()
{
    FILE* f = fopen(kFile, "w");
    if (!f)
    {
        return;
    }
    const std::map<std::string, Section>& sections = Sections();
    for (std::map<std::string, Section>::const_iterator s = sections.begin(); s != sections.end(); ++s)
    {
        fprintf(f, "[%s]\n", s->first.c_str());
        for (Section::const_iterator v = s->second.begin(); v != s->second.end(); ++v)
        {
            const std::vector<BYTE>& d = v->second.data;
            if (v->second.type == REG_DWORD && d.size() == 4)
            {
                DWORD n;
                memcpy(&n, &d[0], 4);
                fprintf(f, "%s=dword:%lu\n", v->first.c_str(), (unsigned long)n);
                continue;
            }
            bool text = v->second.type == REG_SZ && !d.empty() && d.back() == 0;
            for (size_t i = 0; text && i + 1 < d.size(); i++)
            {
                text = d[i] >= 0x20 && d[i] < 0x7F;
            }
            if (text)
            {
                fprintf(f, "%s=str:%s\n", v->first.c_str(), (const char*)&d[0]);
                continue;
            }
            fprintf(f, "%s=hex%lu:", v->first.c_str(), (unsigned long)v->second.type);
            for (size_t i = 0; i < d.size(); i++)
            {
                fprintf(f, "%02x", d[i]);
            }
            fprintf(f, "\n");
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

void Load()
{
    gLoaded = true;
    FILE* f = fopen(kFile, "r");
    if (!f)
    {
        return;
    }
    char line[4096];
    Section* pSection = NULL;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[')
        {
            char* end = strchr(line, ']');
            if (end)
            {
                *end = 0;
            }
            pSection = &Sections()[Lower(line + 1)];
            continue;
        }
        char* eq = strchr(line, '=');
        if (!pSection || !eq)
        {
            continue;
        }
        *eq = 0;
        const char* v = eq + 1;
        Value value;
        if (!strncmp(v, "dword:", 6))
        {
            DWORD n = (DWORD)strtoul(v + 6, NULL, 0);
            value.type = REG_DWORD;
            value.data.assign((BYTE*)&n, (BYTE*)&n + 4);
        }
        else if (!strncmp(v, "str:", 4))
        {
            value.type = REG_SZ;
            value.data.assign(v + 4, v + strlen(v) + 1);
        }
        else if (!strncmp(v, "hex", 3))
        {
            char* p;
            value.type = (DWORD)strtoul(v + 3, &p, 10);
            for (p = (*p == ':') ? p + 1 : p; p[0] && p[1]; p += 2)
            {
                char byte[3] = {p[0], p[1], 0};
                value.data.push_back((BYTE)strtoul(byte, NULL, 16));
            }
        }
        else
        {
            continue;
        }
        (*pSection)[Lower(line)] = value;
    }
    fclose(f);
}

// pExisted: whether the key was there before (a created one wasn't)
Section* Find(LPCSTR subKey, bool create, bool* pExisted = NULL)
{
    if (!gLoaded)
    {
        Load();
    }
    std::map<std::string, Section>& sections = Sections();
    const std::string name = SectionName(subKey);
    std::map<std::string, Section>::iterator it = sections.find(name);
    if (pExisted)
    {
        *pExisted = it != sections.end();
    }
    if (it != sections.end())
    {
        return &it->second;
    }
    return create ? &sections[name] : NULL;
}
} // namespace

LONG WINAPI Port_RegOpenKeyExA(HKEY, LPCSTR subKey, DWORD, REGSAM, PHKEY phk)
{
    Section* pSection = Find(subKey, false);
    *phk = (HKEY)pSection;
    return pSection ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

LONG WINAPI Port_RegOpenKeyA(HKEY root, LPCSTR subKey, PHKEY phk)
{
    return Port_RegOpenKeyExA(root, subKey, 0, 0, phk);
}

LONG WINAPI Port_RegCreateKeyExA(HKEY, LPCSTR subKey, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY phk,
                                 LPDWORD disposition)
{
    bool existed;
    *phk = (HKEY)Find(subKey, true, &existed);
    if (disposition)
    {
        *disposition = existed ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
    }
    return ERROR_SUCCESS;
}

// The game closes keys it failed to open too (an uninitialised HKEY), and treats a failure as fatal.
LONG WINAPI Port_RegCloseKey(HKEY)
{
    return ERROR_SUCCESS;
}

LONG WINAPI Port_RegQueryValueExA(HKEY hKey, LPCSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD cbData)
{
    Section* pSection = (Section*)hKey;
    Section::const_iterator it = pSection ? pSection->find(Lower(name ? name : "")) : Section::const_iterator();
    if (!pSection || it == pSection->end())
    {
        return ERROR_FILE_NOT_FOUND;
    }
    const DWORD size = (DWORD)it->second.data.size();
    if (type)
    {
        *type = it->second.type;
    }
    if (data && cbData && *cbData < size)
    {
        *cbData = size;
        return ERROR_MORE_DATA;
    }
    if (data && size)
    {
        memcpy(data, &it->second.data[0], size);
    }
    if (cbData)
    {
        *cbData = size;
    }
    return ERROR_SUCCESS;
}

LONG WINAPI Port_RegSetValueExA(HKEY hKey, LPCSTR name, DWORD, DWORD type, const BYTE* data, DWORD cbData)
{
    Section* pSection = (Section*)hKey;
    if (!pSection)
    {
        return ERROR_INVALID_HANDLE;
    }
    Value& value = (*pSection)[Lower(name ? name : "")];
    value.type = type;
    value.data.assign(data, data + cbData);
    Save();
    return ERROR_SUCCESS;
}

LONG WINAPI Port_RegDeleteValueA(HKEY hKey, LPCSTR name)
{
    Section* pSection = (Section*)hKey;
    if (!pSection || !pSection->erase(Lower(name ? name : "")))
    {
        return ERROR_FILE_NOT_FOUND;
    }
    Save();
    return ERROR_SUCCESS;
}
