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
//   GTA2_STATELOG_MASK=<file>         lines <name> <hex offset> <hex length> [p]: padding bytes, left out of the
//                                     hashes, or with p pointer fields, hashed as what they point to
//                                     (Scripts/port/layout.py writes it from the port's DWARF)
//   GTA2_STATELOG_DUMP=<a>-<b>:<n>,.. full bytes of those entries (names, *name for pointees) at game frames a..b
//   GTA2_STATELOG_TRACE=<a>-<b>:<label>=<hex address>,..  (Windows) calls of those functions at game frames
//                                     a..b, to <log>.trace:
//                                     C <game frame> <label> <ecx> <4 stack dwords> at the entry and
//                                     R <game frame> <label> <eax> <dword at eax> at the return; both end in ^<esp>.
//   GTA2_STATELOG_TRACE_THIS=<n>      and the first n bytes of *this (ecx) with each call, as | <dwords> int3 breakpoints, so the
//                                     same in 10.5.exe and in the port (Scripts/port/state_check.py, TRACE=)
//
// Pointer fields (marked p in GTA2_STATELOG_MASK) are hashed as what they point into: (entry, offset) when that is
// one of the logged entries, else nothing, so a pointer to the wrong object shows up. Other values that look like
// pointers (0x00400000..0x7FFEFFFF, per aligned dword) are left out: they differ between the processes, and
// positions (Fix16 up to 0x3FC000), counters and flags are below that range.
#include "state_log.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>
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

// Where the entries are this frame, sorted by address: a pointer into one of them is hashed as (entry, offset), the
// same in both processes, so a pointer to the wrong object (or to a freed one) shows up too
struct Range
{
    size_t start, end;
    unsigned entry;
    bool operator<(const Range& o) const // ties: the lowest entry last, so the search below picks it
    {
        return start < o.start || (start == o.start && entry > o.entry);
    }
};
std::vector<Range> gRanges;

unsigned Canonical(unsigned v)
{
    Range key = {v, 0, 0}; // upper_bound: past every range that starts at or below v
    std::vector<Range>::const_iterator it = std::upper_bound(gRanges.begin(), gRanges.end(), key);
    for (int back = 0; back < 4 && it != gRanges.begin(); back++) // nested entries: the innermost that holds it
    {
        --it;
        if (v < it->end)
        {
            return (it->entry << 20) | ((v - (unsigned)it->start) & 0xFFFFFu);
        }
    }
    return 0xFFFFFFFFu; // the heap, the stack, code: not comparable
}

const unsigned char kPointerMask = 0x7F; // mask bytes of a pointer field (layout.py knows the types)

unsigned Hash(const unsigned char* p, const unsigned char* mask, unsigned size)
{
    unsigned h = 2166136261u; // FNV-1a
    unsigned i = 0;
    for (; i + 4 <= size; i += 4)
    {
        unsigned v;
        memcpy(&v, p + i, 4);
        unsigned m = 0xFFFFFFFFu;
        if (mask)
        {
            memcpy(&m, mask + i, 4);
        }
        if (m == 0x7F7F7F7Fu) // a pointer field: what it points to
        {
            v = LooksLikePointer(v) ? Canonical(v) : v;
        }
        else
        {
            v &= m;
            if (LooksLikePointer(v)) // a pointer, or a value we can't tell from one: left out
            {
                v = 0xFFFFFFFFu;
            }
        }
        for (int b = 0; b < 4; b++)
        {
            h = (h ^ ((v >> (b * 8)) & 0xFF)) * 16777619u;
        }
    }
    for (; i < size; i++)
    {
        h = (h ^ (mask && mask[i] != kPointerMask ? p[i] & mask[i] : p[i])) * 16777619u;
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
        char name[400], kind[4] = "";
        unsigned off, len;
        if (sscanf(line, "%399s %x %x %3s", name, &off, &len, kind) < 3)
        {
            continue;
        }
        const unsigned char fill = kind[0] == 'p' ? kPointerMask : 0; // p: a pointer field, else padding
        for (size_t i = 0; i < gEntries.size(); i++) // ponytail: linear search, a map if the mask grows
        {
            Entry& e = gEntries[i];
            if (e.name == name && off + len <= e.size)
            {
                e.mask.resize(e.size, 0xFF);
                memset(&e.mask[off], fill, len);
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
#if defined(_WIN32)
// The call trace: an int3 at each function entry; on a hit, log, put an int3 at the return address, put the
// original byte back and single-step over it, then arm the int3 again.
struct Breakpoint
{
    unsigned char* addr;
    unsigned char orig;
    std::string label; // empty: a return breakpoint
    int returns; // return breakpoints: calls still to return here
    std::vector<std::string> pending; // return breakpoints: their labels, innermost last
    std::vector<unsigned> pendingEsp; // and esp at their entries: a tail call shares its caller's return
};
std::vector<Breakpoint> gBreakpoints;
FILE* gTrace;
unsigned char* gRearm; // the breakpoint to arm again after the single step
int gTraceFrom, gTraceTo;
unsigned gTraceThis;
bool gArmed; // the entry breakpoints are in only for the traced frames: each hit costs four exceptions

int Frame()
{
    const unsigned char* rng = 0;
    int frame = -1;
    if (gFrameGlobal)
    {
        memcpy(&rng, gFrameGlobal, sizeof(rng));
    }
    if (rng)
    {
        memcpy(&frame, rng, sizeof(frame));
    }
    return frame;
}

void Poke(unsigned char* p, unsigned char v)
{
    DWORD old;
    VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old);
    *p = v;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
}

Breakpoint* Find(unsigned char* p, bool ret)
{
    for (size_t i = 0; i < gBreakpoints.size(); i++)
    {
        if (gBreakpoints[i].addr == p && gBreakpoints[i].label.empty() == ret)
        {
            return &gBreakpoints[i];
        }
    }
    return 0;
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* ep)
{
    CONTEXT* c = ep->ContextRecord;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP && gRearm)
    {
        Poke(gRearm, 0xCC);
        gRearm = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_BREAKPOINT)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    unsigned char* at = (unsigned char*)ep->ExceptionRecord->ExceptionAddress;
    Breakpoint* ret = Find(at, true);
    // Every call that returns here and whose frame is gone (more than one after a tail call), innermost first
    while (ret && ret->returns > 0 && ret->pendingEsp.back() < c->Esp)
    {
        const int frame = Frame();
        if (frame >= gTraceFrom && frame <= gTraceTo)
        {
            // and what eax points to: VC6 returns a class through a hidden pointer, which it returns in eax
            const unsigned* p = (const unsigned*)(size_t)c->Eax;
            const unsigned deref = p && !IsBadReadPtr(p, 4) ? *p : 0;
            fprintf(gTrace, "R %d %s %08lx %08x ^%08lx\n", frame, ret->pending.back().c_str(), (unsigned long)c->Eax, deref,
                    (unsigned long)c->Esp);
        }
        ret->pending.pop_back();
        ret->pendingEsp.pop_back();
        ret->returns--;
    }
    Breakpoint* entry = Find(at, false);
    if (!entry && !ret)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (entry)
    {
        const unsigned* sp = (const unsigned*)c->Esp;
        const int frame = Frame();
        if (frame >= gTraceFrom && frame <= gTraceTo)
        {
            fprintf(gTrace, "C %d %s %08lx %08x %08x %08x %08x ^%08lx", frame, entry->label.c_str(), (unsigned long)c->Ecx, sp[1],
                    sp[2], sp[3], sp[4], (unsigned long)c->Esp);
            const unsigned* self = (const unsigned*)(size_t)c->Ecx;
            if (gTraceThis && self && !IsBadReadPtr(self, gTraceThis))
            {
                fputs(" |", gTrace);
                for (unsigned i = 0; i < gTraceThis / 4; i++)
                {
                    fprintf(gTrace, " %08x", self[i]);
                }
            }
            fputc('\n', gTrace);
        }
        unsigned char* back = (unsigned char*)(size_t)sp[0];
        Breakpoint* b = Find(back, true);
        if (!b)
        {
            Breakpoint nb = {back, *back, std::string(), 0, std::vector<std::string>(), std::vector<unsigned>()};
            gBreakpoints.push_back(nb);
            b = &gBreakpoints.back();
            entry = Find(at, false); // push_back may have moved it
            ret = Find(at, true);
        }
        if (b->returns++ == 0)
        {
            Poke(back, 0xCC);
        }
        b->pending.push_back(entry->label);
        b->pendingEsp.push_back(c->Esp); // the return pops past it
    }
    // Run the original instruction once, then arm this address again if anything still wants it
    const bool keep = entry || (ret && ret->returns > 0);
    Poke(at, entry ? entry->orig : ret->orig);
    c->Eip = (DWORD)(size_t)at;
    if (keep)
    {
        gRearm = at;
        c->EFlags |= 0x100; // trap flag
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ParseTrace(const std::string& path)
{
    const char* t = getenv("GTA2_STATELOG_TRACE");
    const char* colon = t ? strchr(t, ':') : 0;
    if (!colon || sscanf(t, "%d-%d", &gTraceFrom, &gTraceTo) != 2)
    {
        return;
    }
    t = colon + 1;
    const char* self = getenv("GTA2_STATELOG_TRACE_THIS");
    gTraceThis = self ? (unsigned)atoi(self) & ~3u : 0;
    gTrace = fopen((path + ".trace").c_str(), "w");
    setvbuf(gTrace, 0, _IONBF, 0); // unbuffered: a crash keeps the trace up to it
    std::string list = t;
    size_t start = 0;
    while (start < list.size())
    {
        size_t comma = list.find(',', start);
        std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        size_t eq = item.find('=');
        if (eq != std::string::npos)
        {
            unsigned char* p = (unsigned char*)(size_t)strtoul(item.c_str() + eq + 1, 0, 16);
            Breakpoint b = {p, *p, item.substr(0, eq), 0, std::vector<std::string>(), std::vector<unsigned>()};
            gBreakpoints.push_back(b);
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    gBreakpoints.reserve(gBreakpoints.size() + 4096); // ponytail: return breakpoints beyond that move the entries, Find() copes
    AddVectoredExceptionHandler(1, OnException);
}

// At every flip: the entry breakpoints in from the frame before the first traced one, out after the last
void ArmTrace(int frame)
{
    const bool want = frame >= gTraceFrom - 1 && frame <= gTraceTo;
    if (!gTrace || want == gArmed)
    {
        return;
    }
    gArmed = want;
    for (size_t i = 0; i < gBreakpoints.size(); i++)
    {
        if (!gBreakpoints[i].label.empty() && gBreakpoints[i].addr != gRearm)
        {
            Poke(gBreakpoints[i].addr, want ? 0xCC : gBreakpoints[i].orig);
        }
    }
}
#endif
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
#if defined(_WIN32)
    ParseTrace(path);
#endif
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
    gRanges.clear();
    for (size_t i = 0; i < gEntries.size(); i++)
    {
        const unsigned char* t = Target(gEntries[i]);
        if (t && gEntries[i].size)
        {
            Range rg = {(size_t)t, (size_t)t + gEntries[i].size, (unsigned)i};
            gRanges.push_back(rg);
        }
    }
    std::sort(gRanges.begin(), gRanges.end());
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
#if defined(_WIN32)
    if (gTrace)
    {
        fflush(gTrace);
        ArmTrace(frame);
    }
#endif

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
                        const unsigned c = LooksLikePointer(v) ? Canonical(v) : 0xFFFFFFFFu;
                        if (c != 0xFFFFFFFFu) // where it points: @<entry>+<offset>
                        {
                            fprintf(gDump, "@%s+%x", gEntries[c >> 20].name.c_str(), c & 0xFFFFFu);
                        }
                    }
                    fputc('\n', gDump);
                }
            }
        }
        fflush(gDump);
    }
}
