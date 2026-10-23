// Port/net (docs/netcode.md, "Code layout"): the rollback set, see world.hpp.
#include "world.hpp"
#include "internal.hpp"
#include "sound_pool.hpp"
#include "Source/Game.hpp"
#include "Source/Light.hpp"
#include "Source/Player.hpp"
#include "Port/port_globals.hpp"
#include <string.h>

EXTERN_GLOBAL(Light**, gLightGrid_6F5400);
void PortRandParticle_Memory(unsigned char** ppAddr, unsigned int* pSize); // Port/port_rand.cpp
extern short gPortZoneRoundRobin; // a function's static in the original (map_0x370.cpp): not a registered global

namespace
{
// winmain.cpp's frame loop around the tick: input buffers, network frame and time counters, pacing. The frame code
// keeps them and a resimulation doesn't run it, so a restore must leave them alone (10.5.exe addresses). The keyboard
// too: the input recorder (0x67F8B0) holds a key's bit from its key-down to its key-up, and restored it went back
// to before the press (a held key moved the player only a little). And the audio driver layer (0x6FFF00, the
// sample manager): its stream handles restored were ones already closed, freed again on the next vocal.
const unsigned kFrameLoop[] = {0x67F8B0, 0x67B990, 0x6FFF00, 0x6F56B8, 0x6F56C8, 0x6F5AEC, 0x6F5A28, 0x6F5868, 0x6F593C, 0x6F57D8, 0x6F5B18, 0x6F5B28,
                               0x6F58C0, 0x6F58C4, 0x6F5980, 0x6F573C, 0x6F5860, 0x6F58A0, 0x6F5858, 0x6F5B74, 0x6F5944,
                               0x707098, 0x7081F0, 0x6F58D8, 0x6F5760, 0x6F56E0, 0x6F5798, 0x626A08, 0x706C50};

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

std::vector<net::engine::Block> net::engine::WorldBlocks()
{
    std::vector<Block> blocks;
    size_t count = 0;
    const PortGlobal* const* globals = PortGlobals(&count);
    for (size_t i = 0; i < count; i++)
    {
        const PortGlobal& g = *globals[i];
        if (FrameLoop(g.mOgAddr))
        {
            continue;
        }
        Block b = {(unsigned char*)g.mVar, g.mSize, g.mName};
        blocks.push_back(b);
        if (g.mPointeeSize)
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
    // Heap objects no global points to directly: the players (Game::field_4_players)
    for (int i = 0; i < MAX_PLAYERS && gGame_0x40_67E008; i++)
    {
        if (Player* p = gGame_0x40_67E008->field_4_players[i])
        {
            Block b = {(unsigned char*)p, sizeof(Player), "Player"};
            blocks.push_back(b);
        }
    }
    // Globals that point to an array: the registry knows one element (found by the rollback check; add more as
    // it finds them)
    if (gLightGrid_6F5400)
    {
        Block b = {(unsigned char*)gLightGrid_6F5400, 0x1000 * sizeof(Light*), "gLightGrid_6F5400[]"};
        blocks.push_back(b);
    }
    Block deaths = {0, 0, "net deaths"};
    NetDeaths_Memory(&deaths.addr, &deaths.size);
    blocks.push_back(deaths);
    Block zones = {(unsigned char*)&gPortZoneRoundRobin, sizeof(gPortZoneRoundRobin), "restart zone round robin"};
    blocks.push_back(zones);
    Block particles = {0, 0, "particle random stream"};
    unsigned int size = 0;
    PortRandParticle_Memory(&particles.addr, &size);
    particles.size = size;
    blocks.push_back(particles);
    Block sound = {0, 0, "SoundPosEntry pool"};
    SoundPool_Memory(&sound.addr, &sound.size);
    blocks.push_back(sound);
    return blocks;
}

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
        memcpy(&out[n], blocks[i].addr, blocks[i].size);
        n += blocks[i].size;
    }
    return n;
}

void net::engine::RestoreWorld(const std::vector<Block>& blocks, const std::vector<unsigned char>& in)
{
    size_t n = 0;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        memcpy(blocks[i].addr, &in[n], blocks[i].size);
        n += blocks[i].size;
    }
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
