// Port/net (docs/netcode.md, "Rollback"): a client saves the game after every tick and, when a snapshot for an
// older tick arrives, restores that tick, applies the snapshot and resimulates up to now with its own inputs.
#include "internal.hpp"
#include "world.hpp"
#include "Source/Game.hpp"
#include "Source/Player.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/CollisionSoundQueue.hpp"
#include "Source/Weapon.hpp"
#include "Source/WeaponManager.hpp"
#include "Source/sound_obj.hpp"
#include "Source/Ped.hpp"
#include "Source/SoundObject.hpp"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

struct net::engine::SavedWorld::Impl
{
    std::vector<unsigned char> bytes;
    ReplicaState* replicas = NULL;
};

namespace
{
// The rollback set, asked once per game (its pointees are fixed once a map is loaded)
const std::vector<net::engine::Block>& RollbackSet()
{
    // Again for every game: a map change's new Game can come at the old one's address, with its pools elsewhere (the
    // old blocks then wrote over freed memory: crashes all over after the second map change)
    static std::vector<net::engine::Block> sBlocks;
    static Game* sFor;
    static int sGeneration = -1;
    if (sFor != gGame_0x40_67E008 || sGeneration != net::engine::GameGeneration() || sBlocks.empty())
    {
        sBlocks = net::engine::WorldBlocks();
        sFor = gGame_0x40_67E008;
        sGeneration = net::engine::GameGeneration();
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
    SaveWorld(RollbackSet(), out.mImpl->bytes);
    out.mImpl->replicas = SaveReplicas(out.mImpl->replicas);
    out.tick = tick;
    out.valid = true;
}

void net::engine::RestoreGame(const SavedWorld& in)
{
    // The sound's channel bookkeeping (its sample queues and the 16 channels' samples) mirrors the mixer, which isn't
    // rolled back (world.cpp): it stays as it is. Restored, a loop started since the snapshot's tick (a machine gun)
    // played on with nothing left to stop it. Its audio entities, which follow game objects, are restored.
    const size_t from = offsetof(sound_obj, field_30_sQueueSample), to = offsetof(sound_obj, field_1440_unused);
    static unsigned char sLive[offsetof(sound_obj, field_1440_unused) - offsetof(sound_obj, field_30_sQueueSample)];
    unsigned char* channels = (unsigned char*)&gSound_obj_66F680 + from;
    memcpy(sLive, channels, to - from);
    const u8 active = gSound_obj_66F680.field_10_nActiveSamples;
    RestoreWorld(RollbackSet(), in.mImpl->bytes);
    memcpy(channels, sLive, to - from);
    gSound_obj_66F680.field_10_nActiveSamples = active;
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
    GuardPeds();
    SeenByAll();
    g->UpdateAllPlayersControls_4B9380();
    if (g->field_0_game_state == GameState::Running_1)
    {
        g->UpdateGame_4B9410(); // the tick without the sound service (Game::ExecuteGame_4B9640 without sound)
    }
    GuardPeds(); // after it too: the live tick follows the last one without a frame in between
    // What the sound service takes from the sim each tick, taken here as it doesn't run: a resimulated sound was heard
    // when it was predicted. Left there, it went into the saves and every restore played it again (a gun that went on
    // firing, a laugh or a step heard over and over). The sim's word to the sound: a weapon's shot, the impacts of the
    // tick (a landing, a crash) and a voice line.
    if (gCollisionSoundQueue_66AFE0)
    {
        gCollisionSoundQueue_66AFE0->Reset_40BB90();
    }
    gSound_obj_66F680.field_544C[0].field_18 = 0; // sound_obj::PlayVocal_418C80's slot (0: none)
    // ... and what it takes from peds and cars (all the Take* accessors sound_obj.cpp uses)
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        p->TakeVoiceEvent_41B0B0(); // what it says next (a landing from a roof, a hit)
        // Its weapons, which aren't on the weapon pool's list (an instant gang's dual pistols heard for ever)
        for (Weapon* w : {p->field_170_selected_weapon, p->field_174_pWeapon})
        {
            if (w)
            {
                w->TakeShotFired_41CC80();
            }
        }
    }
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        c->TakeSoundEvent_411950();
        c->TakeWeaponKind_4119D0();
    }
    for (Weapon* w = gWeapon_30_Pool_707014->field_0_pool.field_4_pPrev; w; w = w->mpNext)
    {
        w->TakeShotFired_41CC80();
    }
    for (int i = 0; i < (int)g->field_23_num_players; i++) // the players' own weapons aren't on the pool's list
    {
        if (Player* p = g->field_4_players[i])
        {
            for (int w = 0; w < kPlayerWeapons; w++)
            {
                if (p->field_718_weapons[w])
                {
                    p->field_718_weapons[w]->TakeShotFired_41CC80();
                }
            }
        }
    }
}
