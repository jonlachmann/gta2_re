// Port/net (docs/netcode.md, "Code layout"): the rollback set, see world.hpp.
#include "world.hpp"
#include "sound_pool.hpp"
#include "Source/Game.hpp"
#include "Source/Light.hpp"
#include "Source/Player.hpp"
#include "Port/port_globals.hpp"
#include <string.h>

EXTERN_GLOBAL(Light**, gLightGrid_6F5400);

std::vector<net::engine::Block> net::engine::WorldBlocks()
{
    std::vector<Block> blocks;
    size_t count = 0;
    const PortGlobal* const* globals = PortGlobals(&count);
    for (size_t i = 0; i < count; i++)
    {
        const PortGlobal& g = *globals[i];
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
