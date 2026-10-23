// Port/net (docs/netcode.md, "Client"): a snapshot's cars and peds in a client's game. Each server entity gets a
// local one: adopted if the client's game already has the same one (the map script made it on both sides at the
// start), else created; moved to the server's state every snapshot; removed when the server no longer sends it.
// Local cars and peds the server doesn't know (other than the players' own) are removed.
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/char.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#include <set>
#include <unordered_map>

struct net::engine::ReplicaState
{
    std::unordered_map<uint32_t, Car*> cars;
    std::unordered_map<uint32_t, Ped*> peds;
};

namespace
{
std::unordered_map<uint32_t, Car*> gCars; // NetId -> this game's car
std::unordered_map<uint32_t, Ped*> gPeds;

Fix16 Raw(int32_t v)
{
    return Fix16(v, 0);
}

Ang16 Angle(int32_t v)
{
    return Ang16((s16)v, (u8)0);
}

bool Near(Sprite* s, int32_t x, int32_t y)
{
    const int32_t dx = s->field_14_xy.x.mValue - x, dy = s->field_14_xy.y.mValue - y;
    return dx < 4096 && dx > -4096 && dy < 4096 && dy > -4096; // a quarter block
}

// The players' peds and the cars they sit in: the players' own (access.cpp), never adopted or removed here
void PlayerOwned(std::set<const void*>& out)
{
    Game* g = gGame_0x40_67E008;
    for (u32 i = 0; g && i < g->field_23_num_players; i++)
    {
        Ped* ped = g->field_4_players[i] ? g->field_4_players[i]->field_2C4_player_ped : NULL;
        if (ped)
        {
            out.insert(ped);
            if (ped->field_16C_car)
            {
                out.insert(ped->field_16C_car);
            }
        }
    }
}
} // namespace

void net::engine::ApplyCarsAndPeds(const Snapshot& snap)
{
    // This game's live cars and peds; a mapped one the game removed by itself (a wreck, a death) is forgotten
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        liveCars.insert(c);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        livePeds.insert(p);
    }
    std::set<const void*> owned;
    PlayerOwned(owned);
    std::set<const void*> mapped(owned);
    for (auto it = gCars.begin(); it != gCars.end();)
    {
        it = liveCars.count(it->second) && !it->second->IsDespawning_4215B0() ? (mapped.insert(it->second), ++it) : gCars.erase(it);
    }
    for (auto it = gPeds.begin(); it != gPeds.end();)
    {
        it = livePeds.count(it->second) ? (mapped.insert(it->second), ++it) : gPeds.erase(it);
    }

    std::set<uint32_t> seen;
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        if (e.type == kEntityCar)
        {
            seen.insert(e.id);
            auto it = gCars.find(e.id);
            Car* car = it != gCars.end() ? it->second : NULL;
            if (car && car->field_84_car_info_idx != e.f[kCarModel])
            {
                car->DespawnSoon_421490(); // another model under this id: should not happen, ids aren't reused
                gCars.erase(it);
                car = NULL;
            }
            if (!car) // adopt the same car if this game has it, else create it
            {
                for (Car* c : liveCars)
                {
                    if (!mapped.count(c) && c->field_50_car_sprite && c->field_84_car_info_idx == e.f[kCarModel] &&
                        Near(c->field_50_car_sprite, e.f[kCarX], e.f[kCarY]))
                    {
                        car = c;
                        break;
                    }
                }
                if (!car)
                {
                    car = gCarManager_6C_677930->SpawnCarAt_446230(Raw(e.f[kCarX]), Raw(e.f[kCarY]), Raw(e.f[kCarZ]), Angle(e.f[kCarRot]),
                                                                    e.f[kCarModel], Fix16(1));
                }
                if (!car)
                {
                    continue;
                }
                gCars[e.id] = car;
                mapped.insert(car);
            }
            if (!owned.count(car)) // a player's car follows the player (access.cpp)
            {
                MoveCar(car, Raw(e.f[kCarX]), Raw(e.f[kCarY]), Raw(e.f[kCarZ]), Angle(e.f[kCarRot]));
            }
        }
        else if (e.type == kEntityPed)
        {
            seen.insert(e.id);
            auto it = gPeds.find(e.id);
            Ped* ped = it != gPeds.end() ? it->second : NULL;
            if (!ped)
            {
                for (Ped* p : livePeds)
                {
                    Sprite* s = p->field_168_game_object ? p->field_168_game_object->field_80_sprite_ptr : NULL;
                    if (!mapped.count(p) && s && !p->field_16C_car && (u8)p->field_244_remap == (u8)e.f[kPedRemap] &&
                        Near(s, e.f[kPedX], e.f[kPedY]))
                    {
                        ped = p;
                        break;
                    }
                }
                if (!ped)
                {
                    ped = gPedManager_6787BC->SpawnPedAt(Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), (u8)e.f[kPedRemap], Angle(e.f[kPedRot]));
                }
                if (!ped)
                {
                    continue;
                }
                gPeds[e.id] = ped;
                mapped.insert(ped);
            }
            if (!ped->field_16C_car)
            {
                MovePed(ped, Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), Angle(e.f[kPedRot]));
            }
        }
    }

    // What the server no longer sends goes; so do this game's own cars and peds nobody mapped
    for (auto it = gCars.begin(); it != gCars.end();)
    {
        if (!seen.count(it->first))
        {
            it->second->DespawnSoon_421490();
            it = gCars.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = gPeds.begin(); it != gPeds.end();)
    {
        if (!seen.count(it->first))
        {
            it->second->Deallocate_45EB60();
            it = gPeds.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (Car* c : liveCars)
    {
        if (!owned.count(c) && !mapped.count(c) && !c->IsDespawning_4215B0())
        {
            c->DespawnSoon_421490();
        }
    }
    for (Ped* p : livePeds)
    {
        // Peds in cars (drivers, passengers) aren't sent yet: they stay with their car
        if (!owned.count(p) && !mapped.count(p) && !p->field_16C_car && p->field_168_game_object)
        {
            p->Deallocate_45EB60();
        }
    }
}

void net::engine::ReplicaStats(const Snapshot& snap, int* pCars, int* pPeds, float* pMaxDrift)
{
    *pCars = (int)gCars.size();
    *pPeds = (int)gPeds.size();
    float drift = 0;
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        Sprite* s = NULL;
        if (e.type == kEntityCar && gCars.count(e.id))
        {
            s = gCars[e.id]->field_50_car_sprite;
        }
        else if (e.type == kEntityPed && gPeds.count(e.id))
        {
            Ped* p = gPeds[e.id];
            s = p->field_168_game_object ? p->field_168_game_object->field_80_sprite_ptr : NULL;
        }
        if (s)
        {
            const float dx = (s->field_14_xy.x.mValue - e.f[0]) / 16384.0f, dy = (s->field_14_xy.y.mValue - e.f[1]) / 16384.0f;
            const float d = dx * dx + dy * dy;
            drift = d > drift ? d : drift;
        }
    }
    *pMaxDrift = sqrtf(drift);
}

net::engine::ReplicaState* net::engine::SaveReplicas(ReplicaState* reuse)
{
    ReplicaState* state = reuse ? reuse : new ReplicaState();
    state->cars = gCars;
    state->peds = gPeds;
    return state;
}

void net::engine::RestoreReplicas(const ReplicaState* state)
{
    gCars = state->cars;
    gPeds = state->peds;
}

void net::engine::FreeReplicas(ReplicaState* state)
{
    delete state;
}
