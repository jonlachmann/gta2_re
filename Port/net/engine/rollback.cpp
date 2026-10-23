// Port/net (docs/netcode.md, "Rollback"): a client saves the game after every tick and, when a snapshot for an
// older tick arrives, restores that tick, applies the snapshot and resimulates up to now with its own inputs.
#include "internal.hpp"
#include "world.hpp"
#include "Source/Game.hpp"
#include "Source/Player.hpp"

struct net::engine::SavedWorld::Impl
{
    std::vector<unsigned char> bytes;
    ReplicaState* replicas = NULL;
};

namespace
{
// The rollback set, asked once per game (its pointees are fixed once a map is loaded)
const std::vector<net::engine::Block>& Blocks()
{
    static std::vector<net::engine::Block> sBlocks;
    static Game* sFor;
    if (sFor != gGame_0x40_67E008 || sBlocks.empty())
    {
        sBlocks = net::engine::WorldBlocks();
        sFor = gGame_0x40_67E008;
    }
    return sBlocks;
}
} // namespace

net::engine::SavedWorld::SavedWorld() : mImpl(new Impl())
{
}

net::engine::SavedWorld::~SavedWorld()
{
    FreeReplicas(mImpl->replicas);
    delete mImpl;
}

void net::engine::SaveGame(SavedWorld& out, uint32_t tick)
{
    SaveWorld(Blocks(), out.mImpl->bytes);
    out.mImpl->replicas = SaveReplicas(out.mImpl->replicas);
    out.tick = tick;
    out.valid = true;
}

void net::engine::RestoreGame(const SavedWorld& in)
{
    RestoreWorld(Blocks(), in.mImpl->bytes);
    RestoreReplicas(in.mImpl->replicas);
}

void net::engine::ResimTick(const uint32_t* buttons, int count)
{
    Game* g = gGame_0x40_67E008;
    for (int i = 0; i < count && i < (int)g->field_23_num_players; i++)
    {
        Player* p = g->field_4_players[i];
        if (p && p->field_8E_in_use)
        {
            p->SetInputs_565740(buttons[i] & 0xFFF); // the controls only: key events (chat) happen once, not per resim
        }
    }
    ProtectPlayers();
    g->UpdateAllPlayersControls_4B9380();
    if (g->field_0_game_state == GameState::Running_1)
    {
        g->UpdateGame_4B9410(); // the tick without the sound service (Game::ExecuteGame_4B9640 without sound)
    }
}
