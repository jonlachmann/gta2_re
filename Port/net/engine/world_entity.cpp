// Port/net (docs/netcode.md, "Entities"): the world entity, one per snapshot: the game's random cycle and seed, the
// restart zones' turn, the round's state (round_state.hpp), the game's generation, the traffic lights and the tag
// game's fugitive.
#include "internal.hpp"
#include "Port/rand/owner.hpp"
#include "Port/port_hooks.hpp"
#include "Port/rand/state.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/TrafficLights.hpp"
#include "Source/char.hpp"
#include "Source/network_data.hpp"
#include "Source/rng.hpp"
#include "Source/sprite.hpp"
#include "Source/GameSession.hpp"
#include <math.h>
#include <string.h>
#include <unordered_map>

void net::engine::CaptureWorld(Snapshot& out)
{
    EntityState w = Entity(kWorldId, kEntityWorld);
    w.f[kWorldCycle] = gpRng_67AB34->get_cur_rng_41CFE0();
    w.f[kWorldZoneTurn] = gPortZoneRoundRobin;
    w.f[kWorldRound] = CurrentRound().number;
    w.f[kWorldWinner] = CurrentRound().winner;
    w.f[kWorldSecondsLeft] = CurrentRound().secondsLeft;
    const Round& r = CurrentRound();
    w.f[kWorldPhase] = r.phase;
    w.f[kWorldPhaseLeft] = r.phaseLeft < 1023 ? r.phaseLeft : 1023;
    w.f[kWorldMinPlayers] = r.minPlayers;
    w.f[kWorldReady] = r.ready;
    w.f[kWorldVote] = r.voteKind;
    w.f[kWorldVoteArg] = r.voteArg;
    w.f[kWorldVoteYes] = r.voteYes;
    w.f[kWorldVoteNo] = r.voteNo;
    w.f[kWorldVoteLeft] = r.voteLeft;
    for (int i = 0; i < kRoundSlots; i++)
    {
        w.f[kWorldMapChoice + i] = r.mapChoice[i];
    }
    w.f[kWorldGame] = GameGeneration() & 0xFFFF;
    w.f[kWorldRand] = (int32_t)PortRandState();
    w.f[kWorldSeed] = (int32_t)PortRandSeed();
    if (gTrafficLights_194_705958 && gCarManager_6C_677930)
    {
        w.f[kWorldLights] = gTrafficLights_194_705958->field_192_phase | gTrafficLights_194_705958->field_193_timer << 4 |
                            (gCarManager_6C_677930->field_20_light_state_a & 0xF) << 12;
    }
    w.f[kWorldIt] = gTagGame_6F8450.field_0_fugitive ? gTagGame_6F8450.field_0_fugitive->field_2E_player_idx + 1 : 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        w.f[kWorldItTime + i] = gTagGame_6F8450.field_4_it_time_secs[i];
    }
    out.entities.push_back(w);
}

void net::engine::ApplyWorld(const EntityState& w)
{
    gpRng_67AB34->field_0_rng = w.f[kWorldCycle];
    gPortZoneRoundRobin = (short)w.f[kWorldZoneTurn];
    Round& r = CurrentRound();
    r.number = w.f[kWorldRound];
    r.winner = w.f[kWorldWinner];
    r.secondsLeft = w.f[kWorldSecondsLeft];
    r.phase = (uint8_t)w.f[kWorldPhase];
    r.phaseLeft = w.f[kWorldPhaseLeft];
    r.minPlayers = (uint8_t)w.f[kWorldMinPlayers];
    r.ready = (uint8_t)w.f[kWorldReady];
    r.voteKind = (uint8_t)w.f[kWorldVote];
    r.voteArg = (uint8_t)w.f[kWorldVoteArg];
    r.voteYes = (uint8_t)w.f[kWorldVoteYes];
    r.voteNo = (uint8_t)w.f[kWorldVoteNo];
    r.voteLeft = (uint8_t)w.f[kWorldVoteLeft];
    for (int i = 0; i < kRoundSlots; i++)
    {
        r.mapChoice[i] = (uint8_t)w.f[kWorldMapChoice + i];
    }
    // The tag game's "it" is the server's, made the game's way (its weapons off, the arrow, "you're it")
    Player* it = GetPlayer(w.f[kWorldIt] - 1);
    if (it && it != gTagGame_6F8450.field_0_fugitive && gTagGame_6F8450.IsTagGame_434B20())
    {
        gTagGame_6F8450.SetNewFugitive_516590(it);
    }
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        gTagGame_6F8450.field_4_it_time_secs[i] = w.f[kWorldItTime + i];
    }
    PortRandSetState((unsigned int)w.f[kWorldRand]);
    PortRandSetSeed((unsigned int)w.f[kWorldSeed]);
    // The traffic lights as the server's: each game ran its own from its start, so a client's cars waited at a red
    // the server's had as green (and were put back behind it by the next snapshot)
    TrafficLights* lights = gTrafficLights_194_705958;
    if (lights && gCarManager_6C_677930 && w.f[kWorldLights])
    {
        const u8 phase = (u8)(w.f[kWorldLights] & 0xF);
        lights->field_193_timer = (u8)(w.f[kWorldLights] >> 4);
        gCarManager_6C_677930->field_20_light_state_a = (w.f[kWorldLights] >> 12) & 0xF;
        if (lights->field_192_phase != phase)
        {
            lights->field_192_phase = phase;
            for (u32 i = 0; i < lights->field_190_array_used_count; i++)
            {
                lights->field_0_traffic_lights[i]->UpdateLightsFromPhase_5C27A0(phase);
            }
        }
    }
}

int net::engine::SnapshotGame(const Snapshot& snap)
{
    const EntityState* w = FindWorld(snap);
    return w ? w->f[kWorldGame] : -1;
}
