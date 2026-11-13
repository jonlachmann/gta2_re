// port/net (port/docs/netcode.md, "Code layout"): the rollback set, see world.hpp.
#include "world.hpp"
#include "internal.hpp"
#include "Source/Game.hpp"
#include "Source/Light.hpp"
#include "Source/Player.hpp"
#include "port/src/port_globals.hpp"
#include <string.h>

EXTERN_GLOBAL(Light**, gLightGrid);
void PortRandParticle_Memory(unsigned char** ppAddr, unsigned int* pSize); // port/src/rand/rand.cpp

namespace
{
// winmain.cpp's frame loop around the tick: input buffers, network frame and time counters, pacing. The frame code
// keeps them and a resimulation doesn't run it, so a restore must leave them alone (10.5.exe addresses). The keyboard
// too: the input recorder (0x67F8B0) holds a key's bit from its key-down to its key-up, and restored it went back
// to before the press (a held key moved the player only a little). And the audio driver layer (0x6FFF00, the
// sample manager): its stream handles restored were ones already closed, freed again on the next vocal. And the
// DirectInput devices: the mouse's is freed when the window loses focus (Input_ReleaseMouse), and the next
// save read it (a crash on alt-tab). And the police radio's crime reports and callout cooldown: only the sound reads
// them (sound_obj::PoliceRadioMessageGeneration_426790), and restored the radio called out the same crime again.
// And file.cpp's open file (ghFile, sbGlobalFileOpen): closed after loading, its FILE freed (glibc's
// heap: a restore wrote the old FILE over freed memory, and free() aborted on the broken heap; macOS keeps FILEs apart).
const unsigned kFrameLoop[] = {0x67A4B8, 0x6758A8, 0x67B804, 0x67B5C0, 0x67B6C0, 0x706C60, 0x67F8B0, 0x67B990, 0x6FFF00, 0x6F56B8, 0x6F56C8, 0x6F5AEC, 0x6F5A28, 0x6F5868, 0x6F593C, 0x6F57D8, 0x6F5B18, 0x6F5B28,
                               0x6F58C0, 0x6F58C4, 0x6F5980, 0x6F573C, 0x6F5860, 0x6F58A0, 0x6F5858, 0x6F5B74, 0x6F5944,
                               0x707098, 0x7081F0, 0x6F58D8, 0x6F5760, 0x6F56E0, 0x6F5798, 0x626A08, 0x706C50, 0x67CFEC, 0x67D160};

// Pointers whose target isn't game state of its own, so not in the set: a loader's cursor into file data freed after
// loading (CarInfoTable.cpp's sInputData), and one left past the end of the array it filled (sModelPhyArrPtr:
// a restore wrote a byte into the next heap block's header, which glibc's malloc noticed later and macOS's didn't),
// the sprite mask cache (gSpriteMaskCache: which drawn texture holds a car with its deltas, lights and damage; a
// restore put back entries for textures since drawn again, so a car showed an old image, a police car's lights lit
// and not flashing, until the entry went), and the game's scratch pointers into its pools, map and tables (the
// last ped processed, a collision's sprite, the blocks around one, a car's info: Ped*, Car*, Sprite*, Object*,
// gmp_*, CarInfo_2C*, ModelPhysics* globals), and the script interpreter's cursor (gBasePtr: the command it is
// at, anywhere in the script's data; taken when the set was, it was the last game's script, freed, and a restore wrote
// 12 bytes there each frame, into whatever had the memory since: a snapshot's entity, its peds running 900 blocks a tick). What they point into is in the set through the pools; the pointers
// themselves are kept. With their targets in, a restore wrote into freed memory: the loader's buffer, and after a
// map change the old game's pools and map (found with Guard Malloc, DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib,
// on a replay: it stops at the first read of freed memory; MALLOC_STRICT_SIZE=1 also at a read past an allocation)
const unsigned kNoPointee[] = {0x676170, 0x675F98, 0x67AF1C, 0x675C30, 0x677938, 0x6779B0, 0x678558, 0x6787C0, 0x6787DC, 0x6787E8,
                               0x678E40, 0x678F40, 0x678F64, 0x678F84, 0x6F5EB0, 0x6F5EC8, 0x6F5F54, 0x6F5F90,
                               0x6F5F98, 0x6F5FB0, 0x6F6054, 0x6F6060, 0x6F606C, 0x6F6070, 0x6F6078, 0x6F6080,
                               0x6F6084, 0x6F6088, 0x6F608C, 0x6F61E8, 0x6F6478, 0x6F8F8C, 0x6FE0E4, 0x6FE258,
                               0x6FEDDC, 0x6FF53C, 0x703814, 0x7041A8, 0x704274, 0x7043D4, 0x7043F0, 0x704410,
                               0x704418, 0x6F8070};

bool NoPointee(unsigned og)
{
    for (size_t i = 0; i < sizeof(kNoPointee) / sizeof(kNoPointee[0]); i++)
    {
        if (kNoPointee[i] == og)
        {
            return true;
        }
    }
    return false;
}

bool FrameLoop(unsigned og)
{
    for (size_t i = 0; i < sizeof(kFrameLoop) / sizeof(kFrameLoop[0]); i++)
    {
        if (kFrameLoop[i] == og)
        {
            return true;
        }
    }
    return false;
}
} // namespace

// The registry's globals from <from> to <to>, and what they point to
static void AddGlobals(std::vector<net::engine::Block>& blocks, size_t from, size_t to)
{
    using net::engine::Block;
    size_t count = 0;
    const PortGlobal* const* globals = PortGlobals(&count);
    for (size_t i = from; i < to && i < count; i++)
    {
        const PortGlobal& g = *globals[i];
        if (FrameLoop(g.mOgAddr))
        {
            continue;
        }
        Block b = {(unsigned char*)g.mVar, g.mSize, g.mName};
        blocks.push_back(b);
        if (g.mPointeeSize && !NoPointee(g.mOgAddr))
        {
            unsigned char* p;
            memcpy(&p, g.mVar, sizeof(p));
            if (p)
            {
                Block pb = {p, g.mPointeeSize, g.mName};
                blocks.push_back(pb);
            }
        }
    }
}

size_t net::engine::RegisteredGlobals()
{
    size_t count = 0;
    PortGlobals(&count);
    return count;
}

std::vector<net::engine::Block> net::engine::WorldBlocks()
{
    std::vector<Block> blocks;
    // A function's static registers when the function first runs (port/src/port_globals.hpp): those registered after the
    // first set was taken go last, so a world saved before one came is the start of the set after it (RestoreWorld)
    static const size_t sEarly = RegisteredGlobals();
    AddGlobals(blocks, 0, sEarly);
    // Heap objects no global points to directly: the players (Game::mPlayers)
    for (int i = 0; i < MAX_PLAYERS && gGame_0x40; i++)
    {
        if (Player* p = gGame_0x40->mPlayers[i])
        {
            Block b = {(unsigned char*)p, sizeof(Player), "Player"};
            blocks.push_back(b);
        }
    }
    // Globals that point to an array: the registry knows one element (found by the rollback check; add more as
    // it finds them)
    if (gLightGrid)
    {
        Block b = {(unsigned char*)gLightGrid, 0x1000 * sizeof(Light*), "gLightGrid[]"};
        blocks.push_back(b);
    }
    Block deaths = {0, 0, "net deaths"};
    NetDeaths_Memory(&deaths.addr, &deaths.size);
    blocks.push_back(deaths);
    Block particles = {0, 0, "particle random stream"};
    unsigned int size = 0;
    PortRandParticle_Memory(&particles.addr, &size);
    particles.size = size;
    blocks.push_back(particles);
    Block sound = {0, 0, "SoundPosEntry pool"};
    SoundPool_Memory(&sound.addr, &sound.size);
    blocks.push_back(sound);
    AddGlobals(blocks, sEarly, RegisteredGlobals());
    return blocks;
}

const net::engine::Block* net::engine::gCopyingBlock;

size_t net::engine::SaveWorld(const std::vector<Block>& blocks, std::vector<unsigned char>& out)
{
    size_t n = 0;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        n += blocks[i].size;
    }
    out.resize(n);
    n = 0;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        gCopyingBlock = &blocks[i];
        memcpy(&out[n], blocks[i].addr, blocks[i].size);
        n += blocks[i].size;
    }
    gCopyingBlock = NULL;
    return n;
}

void net::engine::RestoreWorld(const std::vector<Block>& blocks, const std::vector<unsigned char>& in)
{
    size_t n = 0;
    for (size_t i = 0; i < blocks.size() && n + blocks[i].size <= in.size(); i++) // a world saved before a block came: the start
    {
        gCopyingBlock = &blocks[i];
        memcpy(blocks[i].addr, &in[n], blocks[i].size);
        n += blocks[i].size;
    }
    gCopyingBlock = NULL;
}

unsigned long long net::engine::HashWorld(const std::vector<Block>& blocks)
{
    unsigned long long h = 1469598103934665603ull; // FNV-1a
    for (size_t i = 0; i < blocks.size(); i++)
    {
        for (size_t j = 0; j < blocks[i].size; j++)
        {
            h = (h ^ blocks[i].addr[j]) * 1099511628211ull;
        }
    }
    return h;
}
