// Port/net (docs/netcode.md, "Code layout"): the game world as a list of memory blocks: what a client saves every
// tick and restores to resimulate. Shared with the sim bench (Port/net/eval/sim_bench.cpp), whose rollback check
// proves the list complete.
#pragma once
#include <stddef.h>
#include <vector>

namespace net
{
namespace engine
{
struct Block
{
    unsigned char* addr;
    size_t size;
    const char* name;
};

// Every registered global and what it points to (Port/port_globals), the players, the light grid and the sound
// pool. Pointees can change (a new map), so ask again after loading.
std::vector<Block> WorldBlocks();

size_t SaveWorld(const std::vector<Block>& blocks, std::vector<unsigned char>& out);
void RestoreWorld(const std::vector<Block>& blocks, const std::vector<unsigned char>& in);
unsigned long long HashWorld(const std::vector<Block>& blocks);
} // namespace engine
} // namespace net
