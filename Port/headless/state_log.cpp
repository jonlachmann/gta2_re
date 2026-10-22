// Port (docs/port.md): a per-frame log of the game's state, to compare the port with 10.5.exe exactly.
// Built into the port and into nullvideo.dll (which runs inside 10.5.exe). Both log the same entries:
// every global the decomp defines (Port/port_globals.hpp) and, for pointers, what they point to. The port
// knows them from its registry and writes the list as <log>.globals; nullvideo.dll reads that list and finds
// them at their 10.5.exe addresses.
//
//   GTA2_STATELOG=<file>              one line per frame: F <flip> <game frame> then <chunk>:<hash> for every
//                                     64-byte chunk whose hash changed since the previous line (entries are cut
//                                     into chunks in table order: Scripts/port/state_diff.py does the same)
//   GTA2_STATELOG_TABLE=<file>        (nullvideo.dll) the list written by a port run
//   GTA2_STATELOG_MASK=<file>         lines <name> <hex offset> <hex length>: padding bytes, left out of the hashes
//                                     (Scripts/port/layout.py writes it from the port's DWARF)
//   GTA2_STATELOG_DUMP=<a>-<b>:<n>,.. full bytes of those entries (names, *name for pointees) at game frames a..b
//
// Hashes skip values that look like pointers (0x00400000..0x7FFEFFFF, per aligned dword): they always differ
// between two processes. Positions (Fix16 up to 0x3FC000), counters and flags are below that.
#include "state_log.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#if defined(STATELOG_IN_PORT)
    #include "../port_globals.hpp"
#endif
#if defined(_WIN32)
    #include <windows.h>
#endif

namespace
{
struct Entry
{
    std::string name; // "*name" for what a pointer global points to
    unsigned og; // 10.5.exe address of the global
    const unsigned char* addr; // in this process
    unsigned size;
    bool pointee;
    std::vector<unsigned char> mask; // 0xFF per byte to hash, 0 for padding; empty: all of it
};

std::vector<Entry> gEntries;
const unsigned kChunk = 64;
std::vector<unsigned> gLast; // the last hash written per chunk
FILE* gLog;
FILE* gDump;
const unsigned char* gFrameGlobal; // gpRng_67AB34: its rng's first field is the game frame log_random prints
int gDumpFrom = 1, gDumpTo = 0;
std::vector<std::string> gDumpNames;

bool LooksLikePointer(unsigned v)
{
    return v >= 0x00400000u && v <= 0x7FFEFFFFu;
}

unsigned Hash(const unsigned char* p, const unsigned char* mask, unsigned size)
{
    unsigned h = 2166136261u; // FNV-1a
    unsigned i = 0;
    for (; i + 4 <= size; i += 4)
    {
        unsigned v;
        memcpy(&v, p + i, 4);
        if (mask)
        {
            unsigned m;
            memcpy(&m, mask + i, 4);
            v &= m;
        }
        if (LooksLikePointer(v))
        {
            v = 0xFFFFFFFFu;
        }
        for (int b = 0; b < 4; b++)
        {
            h = (h ^ ((v >> (b * 8)) & 0xFF)) * 16777619u;
        }
    }
    for (; i < size; i++)
    {
        h = (h ^ (mask ? p[i] & mask[i] : p[i])) * 16777619u;
    }
    return h;
}

// What a pointer entry points to now, or 0 (also when it points to memory that can't be read)
const unsigned char* Target(const Entry& e)
{
    if (!e.pointee)
    {
        return e.addr;
    }
    const unsigned char* p;
    memcpy(&p, e.addr, sizeof(p));
#if defined(_WIN32)
    if (p && IsBadReadPtr(p, e.size))
    {
        return 0;
    }
#endif
    return p;
}

void Add(const char* name, unsigned og, const unsigned char* addr, unsigned size, unsigned pointeeSize)
{
    Entry e = {name, og, addr, size, false};
    gEntries.push_back(e);
    if (pointeeSize)
    {
        Entry p = {std::string("*") + name, og, addr, pointeeSize, true};
        gEntries.push_back(p);
    }
    if (og == 0x67AB34)
    {
        gFrameGlobal = addr;
    }
}

void ParseMask()
{
    const char* path = getenv("GTA2_STATELOG_MASK");
    FILE* f = path ? fopen(path, "r") : 0;
    if (!f)
    {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char name[400];
        unsigned off, len;
        if (sscanf(line, "%399s %x %x", name, &off, &len) != 3)
        {
            continue;
        }
        for (size_t i = 0; i < gEntries.size(); i++) // ponytail: linear search, a map if the mask grows
        {
            Entry& e = gEntries[i];
            if (e.name == name && off + len <= e.size)
            {
                e.mask.resize(e.size, 0xFF);
                memset(&e.mask[off], 0, len);
                break;
            }
        }
    }
    fclose(f);
}

void ParseDump()
{
    const char* d = getenv("GTA2_STATELOG_DUMP");
    if (!d)
    {
        return;
    }
    const char* colon = strchr(d, ':');
    if (!colon || sscanf(d, "%d-%d", &gDumpFrom, &gDumpTo) != 2)
    {
        return;
    }
    std::string names = colon + 1;
    size_t start = 0;
    while (start <= names.size())
    {
        size_t comma = names.find(',', start);
        gDumpNames.push_back(names.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
}
} // namespace

void StateLog_Init()
{
    const char* path = getenv("GTA2_STATELOG");
    if (!path || gLog)
    {
        return;
    }
#if defined(STATELOG_IN_PORT)
    size_t count = 0;
    const PortGlobal* const* globals = PortGlobals(&count);
    std::string tablePath = std::string(path) + ".globals";
    FILE* table = fopen(tablePath.c_str(), "w");
    for (size_t i = 0; i < count; i++)
    {
        const PortGlobal& g = *globals[i];
        Add(g.mName, g.mOgAddr, (const unsigned char*)g.mVar, g.mSize, g.mPointeeSize);
        if (table)
        {
            fprintf(table, "%08x %u %u %s\n", g.mOgAddr, g.mSize, g.mPointeeSize, g.mName);
        }
    }
    if (table)
    {
        fclose(table);
    }
#else
    // Inside 10.5.exe: the port's list, at the original addresses
    const char* tablePath = getenv("GTA2_STATELOG_TABLE");
    FILE* table = tablePath ? fopen(tablePath, "r") : 0;
    if (!table)
    {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), table))
    {
        unsigned og, size, pointee;
        char name[400];
        if (sscanf(line, "%x %u %u %399s", &og, &size, &pointee, name) == 4)
        {
            Add(name, og, (const unsigned char*)(size_t)og, size, pointee);
        }
    }
    fclose(table);
#endif
    gLog = fopen(path, "w");
    ParseMask();
    ParseDump();
    if (!gDumpNames.empty())
    {
        gDump = fopen((std::string(path) + ".dump").c_str(), "w");
    }
}

void StateLog_Frame(unsigned flip)
{
    if (!gLog)
    {
        return;
    }
    const unsigned char* rng = 0;
    if (gFrameGlobal)
    {
        memcpy(&rng, gFrameGlobal, sizeof(rng));
    }
    if (!rng)
    {
        return; // not in a game yet
    }
    int frame;
    memcpy(&frame, rng, sizeof(frame));
    fprintf(gLog, "F %u %d", flip, frame);
    size_t chunk = 0;
    for (size_t i = 0; i < gEntries.size(); i++)
    {
        const Entry& e = gEntries[i];
        const unsigned char* p = Target(e);
        for (unsigned off = 0; off < e.size; off += kChunk, chunk++)
        {
            const unsigned n = e.size - off < kChunk ? e.size - off : kChunk;
            const unsigned h = p ? Hash(p + off, e.mask.empty() ? 0 : &e.mask[off], n) : 0u;
            if (chunk >= gLast.size())
            {
                gLast.resize(chunk + 1, 0xFFFFFFFFu);
            }
            if (h != gLast[chunk])
            {
                fprintf(gLog, " %x:%08x", (unsigned)chunk, h);
                gLast[chunk] = h;
            }
        }
    }
    fputc('\n', gLog);
    fflush(gLog);

    if (gDump && frame >= gDumpFrom && frame <= gDumpTo)
    {
        for (size_t i = 0; i < gEntries.size(); i++)
        {
            const Entry& e = gEntries[i];
            for (size_t n = 0; n < gDumpNames.size(); n++)
            {
                const unsigned char* p = gDumpNames[n] == e.name ? Target(e) : 0;
                if (p)
                {
                    fprintf(gDump, "D %d %s", frame, e.name.c_str());
                    for (unsigned b = 0; b < e.size; b += 4)
                    {
                        unsigned v = 0;
                        memcpy(&v, p + b, e.size - b < 4 ? e.size - b : 4);
                        fprintf(gDump, " %08x", v);
                    }
                    fputc('\n', gDump);
                }
            }
        }
        fflush(gDump);
    }
}
