// Port/net (docs/netcode.md, "Rollback"): a client saves the game after every tick and, when a snapshot for an
// older tick arrives, restores that tick, applies the snapshot and resimulates up to now with its own inputs.
#include "internal.hpp"
#include "world.hpp"
#include "Source/Game.hpp"
#include "Source/Player.hpp"
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
    // The sound's channel bookkeeping (its sample queues and the 16 channels' samples) mirrors the mixer, which isn't
    // rolled back (world.cpp): it stays as it is. Restored, a loop started since the snapshot's tick (a machine gun)
    // played on with nothing left to stop it. Its audio entities, which follow game objects, are restored.
    const size_t from = offsetof(sound_obj, field_30_sQueueSample), to = offsetof(sound_obj, field_1440_unused);
    static unsigned char sLive[offsetof(sound_obj, field_1440_unused) - offsetof(sound_obj, field_30_sQueueSample)];
    unsigned char* channels = (unsigned char*)&gSound_obj_66F680 + from;
    memcpy(sLive, channels, to - from);
    const u8 active = gSound_obj_66F680.field_10_nActiveSamples;
    RestoreWorld(Blocks(), in.mImpl->bytes);
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
    g->UpdateAllPlayersControls_4B9380();
    if (g->field_0_game_state == GameState::Running_1)
    {
        g->UpdateGame_4B9410(); // the tick without the sound service (Game::ExecuteGame_4B9640 without sound)
    }
    // What the sound service takes from the sim each tick, taken here as it doesn't run: a resimulated sound was heard
    // when it was predicted. Left there, it went into the saves and every restore played it again (a gun that went on
    // firing, a laugh or a step heard over and over). The sim's word to the sound: a weapon's shot, the impacts of the
    // tick (a landing, a crash) and a voice line.
    if (gCollisionSoundQueue_66AFE0)
    {
        gCollisionSoundQueue_66AFE0->Reset_40BB90();
    }
    gSound_obj_66F680.field_544C[0].field_18 = 0; // sound_obj::PlayVocal_418C80's slot (0: none)
    for (Weapon* w = gWeapon_30_Pool_707014->field_0_pool.field_4_pPrev; w; w = w->mpNext)
    {
        w->TakeF2C_41CC80();
    }
    for (int i = 0; i < (int)g->field_23_num_players; i++) // the players' own weapons aren't on the pool's list
    {
        if (Player* p = g->field_4_players[i])
        {
            for (int w = 0; w < 28; w++)
            {
                if (p->field_718_weapons[w])
                {
                    p->field_718_weapons[w]->TakeF2C_41CC80();
                }
            }
        }
    }
}

void net::engine::DebugSoundChannels(uint32_t frame)
{
    static const bool on = getenv("GTA2_NET_SOUNDLOG") != NULL;
    static std::string sLast;
    if (!on)
    {
        return;
    }
    std::string line;
    char buf[96];
    for (int i = 0; i < 16; i++)
    {
        const sound_0x68& a = gSound_obj_66F680.field_DC0_ActiveSamples[i];
        if (a.field_2C_bIsBeingPlayed)
        {
            snprintf(buf, sizeof(buf), " ch%d:ent%d/samp%d/loops%u/type%d/rel%u/ftp%d", i, a.field_0_EntityIndex, a.field_14_samp_idx, a.field_30_loop_count,
                     a.field_58_type, a.field_4C_releasing_volume_divider, a.field_44_frames_to_play);
            line += buf;
            const int ent = a.field_0_EntityIndex;
            const AudioEntity* ae = ent >= 0 && ent < 1020 ? &gSound_obj_66F680.field_147C_audio_entities[ent] : NULL;
            if (ae && ae->field_0_bUsed && ae->field_4_pObj && ae->field_4_pObj->field_0_object_type == 7)
            {
                const Weapon* w = ae->field_4_pObj->field_C_pAny.pWeapon_30;
                const Ped* ped = w ? w->field_24_pPed : NULL;
                snprintf(buf, sizeof(buf), "[weapon %d ammo %d reload %d shot %d ped %d player %d]", w ? w->field_1C_idx : -1, w ? w->field_0_ammo : -1, w ? w->field_2_reload_timer : -1,
                         w ? w->field_2C_shot_fired : -1, ped ? ped->field_200_id : -1, ped && ped->field_15C_player ? ped->field_15C_player->field_2E_idx : -1);
                line += buf;
            }
        }
    }
    if (line != sLast)
    {
        printf("net: frame %u, sound channels:%s (init %d)\n", frame, line.c_str(), gSound_obj_66F680.field_0_bSoundInitialized);
        sLast = line;
    }
}
