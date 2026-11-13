// port/net (port/docs/netcode.md, "The rollback set"): a client saves the game after every tick and, when a snapshot for an
// older tick arrives, restores that tick, applies the snapshot and resimulates up to now with its own inputs.
#include "internal.hpp"
#include "world.hpp"
#include "dev/dev.hpp"
#include "Source/Game.hpp"
#include "Source/Player.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/CollisionSoundQueue.hpp"
#include "Source/CrimeReportQueue.hpp"
#include "Source/Weapon.hpp"
#include "Source/WeaponManager.hpp"
#include "Source/sound_obj.hpp"
#include "Source/Ped.hpp"
#include "Source/SoundObject.hpp"
#include <algorithm>
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
    static size_t sGlobals;
    if (sFor != gGame_0x40 || sGeneration != net::engine::GameGeneration() || sBlocks.empty() ||
        sGlobals != net::engine::RegisteredGlobals()) // a function's static came: at the end (WorldBlocks)
    {
        sBlocks = net::engine::WorldBlocks();
        sFor = gGame_0x40;
        sGeneration = net::engine::GameGeneration();
        sGlobals = net::engine::RegisteredGlobals();
        if (net::engine::Debug().checkGrid)
        {
            // GTA2_NET_CHECKGRID: a block that runs past the start of the next into memory beyond it (one inside another,
            // a camera inside its player, is fine). A pointee bigger than what its pointer points to is restored over
            // whatever lies after it
            std::vector<net::engine::Block> by = sBlocks;
            std::sort(by.begin(), by.end(), [](const net::engine::Block& a, const net::engine::Block& b)
                      { return a.addr != b.addr ? a.addr < b.addr : a.size > b.size; }); // at one address, the outer first
            for (size_t i = 1; i < by.size(); i++)
            {
                const unsigned char* end = by[i - 1].addr + by[i - 1].size;
                if (by[i].addr < end && by[i].addr + by[i].size > end)
                {
                    printf("net: CHECK the rollback block %s (%p, %zu bytes) runs into %s (%p, %zu bytes)\n", by[i - 1].name, (void*)by[i - 1].addr,
                           by[i - 1].size, by[i].name, (void*)by[i].addr, by[i].size);
                }
            }
        }
    }
    else if (net::engine::Debug().checkGrid)
    {
        // GTA2_NET_CHECKGRID: the set as it would be now. A pointee that moved since is copied from (and restored into)
        // memory it no longer owns: a scratch pointer for kNoPointee (world.cpp), or one to take again
        static int sSaid;
        const std::vector<net::engine::Block> now = net::engine::WorldBlocks();
        for (size_t i = 0; i < now.size() && i < sBlocks.size() && sSaid < 10; i++)
        {
            if (now[i].addr != sBlocks[i].addr || now[i].size != sBlocks[i].size)
            {
                sSaid++;
                printf("net: CHECK the rollback block %s moved since the set was taken: %p (%zu bytes), now %p (%zu)\n", sBlocks[i].name,
                       (void*)sBlocks[i].addr, sBlocks[i].size, (void*)now[i].addr, now[i].size);
            }
        }
        if (now.size() != sBlocks.size() && sSaid++ < 10)
        {
            printf("net: CHECK the rollback set has %zu blocks now, %zu when taken\n", now.size(), sBlocks.size());
        }
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
    const size_t from = offsetof(sound_obj, msQueueSample), to = offsetof(sound_obj, mUnused1440);
    static unsigned char sLive[offsetof(sound_obj, mUnused1440) - offsetof(sound_obj, msQueueSample)];
    unsigned char* channels = (unsigned char*)&gSound_obj + from;
    memcpy(sLive, channels, to - from);
    const u8 active = gSound_obj.mnActiveSamples;
    // So does the police radio and the car radio (from field_5504 on): restored, the stars it last called out went
    // back too, and every correction started its call again over the one it was saying
    const size_t radio = offsetof(sound_obj, mRadioStationChangeMode);
    static unsigned char sRadio[sizeof(sound_obj) - offsetof(sound_obj, mRadioStationChangeMode)];
    memcpy(sRadio, (unsigned char*)&gSound_obj + radio, sizeof(sRadio));
    // The window's size is this machine's, not the world's: a resize since the save (its cameras' screen size, set only
    // on a change) would otherwise go back to the old one for good
    const Camera* view = gGame_0x40 && gGame_0x40->mLocalPlayer ? &gGame_0x40->mLocalPlayer->mViewCamera : NULL;
    const s32 screenW = view ? view->mScreenPxWidth : 0, screenH = view ? view->mScreenPxHeight : 0;
    RestoreWorld(RollbackSet(), in.mImpl->bytes);
    if (view && (view->mScreenPxWidth != screenW || view->mScreenPxHeight != screenH))
    {
        gGame_0x40->SetScreenSize(screenW, screenH);
    }
    memcpy(channels, sLive, to - from);
    memcpy((unsigned char*)&gSound_obj + radio, sRadio, sizeof(sRadio));
    gSound_obj.mnActiveSamples = active;
    RestoreReplicas(in.mImpl->replicas);
}

void net::engine::ResimTick(const uint32_t* buttons, int count)
{
    Game* g = gGame_0x40;
    for (int i = 0; i < count && i < (int)g->mNumPlayers; i++)
    {
        Player* p = g->mPlayers[i];
        if (p && p->mInUse)
        {
            p->SetInputs(buttons[i] & 0xFFF); // the controls only: key events (chat) happen once, not per resim
        }
    }
    ProtectPlayers();
    GuardPeds();
    KeepPlayerArrows();
    SeenByAll();
    g->UpdateAllPlayersControls();
    if (g->mGameState == GameState::Running_1)
    {
        g->UpdateGame(); // the tick without the sound service (Game::ExecuteGame without sound)
    }
    GuardPeds(); // after it too: the live tick follows the last one without a frame in between
    // What the sound service takes from the sim each tick, taken here as it doesn't run: a resimulated sound was heard
    // when it was predicted. Left there, it went into the saves and every restore played it again (a gun that went on
    // firing, a laugh or a step heard over and over). The sim's word to the sound: a weapon's shot, the impacts of the
    // tick (a landing, a crash) and a voice line.
    if (gCollisionSoundQueue)
    {
        gCollisionSoundQueue->Reset();
    }
    gSound_obj.mVocals.mPendingVoice = 0; // sound_obj::PlayVocal's slot (0: none)
    if (gCrimeReportQueue) // the crimes the police radio calls out
    {
        gCrimeReportQueue->mReadIdx = gCrimeReportQueue->mWriteIdx;
    }
    // ... and what it takes from peds and cars (all the Take* accessors sound_obj.cpp uses)
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
    {
        p->TakeVoiceEvent(); // what it says next (a landing from a roof, a hit)
        // Its weapons, which aren't on the weapon pool's list (an instant gang's dual pistols heard for ever)
        for (Weapon* w : {p->mSelectedWeapon, p->mpWeapon})
        {
            if (w)
            {
                w->TakeShotFired();
            }
        }
    }
    for (Car* c = gCar_BC_Pool->mPool.mpPrev; c; c = c->mpNext)
    {
        c->TakeSoundEvent();
        c->TakeShopEvent();
    }
    for (Weapon* w = gWeapon_30_Pool->mPool.mpPrev; w; w = w->mpNext)
    {
        w->TakeShotFired();
    }
    for (int i = 0; i < (int)g->mNumPlayers; i++) // the players' own weapons aren't on the pool's list
    {
        if (Player* p = g->mPlayers[i])
        {
            for (int w = 0; w < kPlayerWeapons; w++)
            {
                if (p->mWeapons[w])
                {
                    p->mWeapons[w]->TakeShotFired();
                }
            }
        }
    }
}
