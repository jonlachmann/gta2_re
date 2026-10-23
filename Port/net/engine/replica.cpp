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
#include "Source/collide.hpp"
#include <math.h>
#include <stdio.h>
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

// The players' peds, the cars they sit in and their corpses (Player::Wasted_567130 leaves a clone, the aux ped):
// the players' own (access.cpp), never adopted or removed here
void PlayerOwned(std::set<const void*>& out)
{
    Game* g = gGame_0x40_67E008;
    for (u32 i = 0; g && i < g->field_23_num_players; i++)
    {
        Player* player = g->field_4_players[i];
        Ped* peds[2] = {player ? player->field_2C4_player_ped : NULL, player ? player->field_2C8_aux_ped : NULL};
        for (int k = 0; k < 2; k++)
        {
            if (peds[k])
            {
                out.insert(peds[k]);
                if (peds[k]->field_16C_car)
                {
                    out.insert(peds[k]->field_16C_car);
                }
            }
        }
    }
}
} // namespace

namespace
{
template <class T> int FreeSlots(T* head)
{
    int n = 0;
    for (; head && n < 64; head = head->mpNext)
    {
        n++;
    }
    return n;
}

// Room to create a car or a ped without running a pool dry (Pool::Allocate doesn't check, and SpawnPedAt leaks its
// ped when no char is left): a margin for the game's own allocations
bool RoomForCar()
{
    return FreeSlots(gCar_BC_Pool_67792C->field_0_pool.field_0_pStart) > 16 && FreeSlots(gSpritePool_703818->field_0_pool.field_0_pHead) > 32;
}

int gRefusedPeds[3]; // why: no ped, no char, no sprite (for the log)

bool RoomForPed()
{
    const bool ped = FreeSlots(gPedPool_6787B8->field_0_pool.field_0_pStart) > 16;
    const bool chr = FreeSlots(gChar_B4_Pool_6FDB44->field_0_pool.field_0_pHead) > 16;
    const bool spr = FreeSlots(gSpritePool_703818->field_0_pool.field_0_pHead) > 32;
    gRefusedPeds[0] += !ped;
    gRefusedPeds[1] += !chr;
    gRefusedPeds[2] += !spr;
    return ped && chr && spr;
}

// This game's live cars and peds; a mapped one the game removed by itself (a wreck, a death, or a resimulation
// since the map was saved) is forgotten
void Live(std::set<Car*>& cars, std::set<Ped*>& peds)
{
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars.insert(c);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        peds.insert(p);
    }
}
} // namespace

void net::engine::ApplyCarsAndPeds(const Snapshot& snap)
{
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    std::set<const void*> owned;
    PlayerOwned(owned);
    std::set<const void*> mapped(owned);
    for (auto it = gCars.begin(); it != gCars.end();)
    {
        it = liveCars.count(it->second) && !it->second->IsDespawning_4215B0() ? (mapped.insert(it->second), ++it) : gCars.erase(it);
    }
    for (auto it = gPeds.begin(); it != gPeds.end();)
    {
        it = livePeds.count(it->second) && it->second->IsActive_433B40() ? (mapped.insert(it->second), ++it) : gPeds.erase(it);
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
                if (!car && RoomForCar())
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
                    if (!mapped.count(p) && s && !p->field_16C_car && p->IsActive_433B40() && (u8)p->field_244_remap == (u8)e.f[kPedRemap] &&
                        Near(s, e.f[kPedX], e.f[kPedY]))
                    {
                        ped = p;
                        break;
                    }
                }
                if (!ped && RoomForPed())
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
            if (it->second->IsActive_433B40()) // once: Deallocate_45EB60 undoes counters and groups, and marks it inactive
            {
                it->second->Deallocate_45EB60();
            }
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
        if (!owned.count(p) && !mapped.count(p) && !p->field_16C_car && p->field_168_game_object && p->IsActive_433B40())
        {
            p->Deallocate_45EB60();
        }
    }
}

void net::engine::PoolUse(int* pCars, int* pPeds, int* pChars)
{
    *pCars = *pPeds = *pChars = 0;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        ++*pCars;
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        ++*pPeds;
    }
    if (gRefusedPeds[0] + gRefusedPeds[1] + gRefusedPeds[2])
    {
        printf("net: peds not created: %d for want of a ped, %d of a char, %d of a sprite (free: %d peds, %d chars, %d sprites)\n",
               gRefusedPeds[0], gRefusedPeds[1], gRefusedPeds[2], FreeSlots(gPedPool_6787B8->field_0_pool.field_0_pStart),
               FreeSlots(gChar_B4_Pool_6FDB44->field_0_pool.field_0_pHead), FreeSlots(gSpritePool_703818->field_0_pool.field_0_pHead));
        gRefusedPeds[0] = gRefusedPeds[1] = gRefusedPeds[2] = 0;
    }
    *pChars = (int)(sizeof(gChar_B4_Pool_6FDB44->field_0_pool.field_4_pool) / sizeof(gChar_B4_Pool_6FDB44->field_0_pool.field_4_pool[0]));
    for (Char* c = gChar_B4_Pool_6FDB44->field_0_pool.field_0_pHead; c; c = c->mpNext)
    {
        --*pChars;
    }
}

void net::engine::ReplicaStats(const Snapshot& snap, int* pCars, int* pPeds, float* pMaxDrift)
{
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    *pCars = (int)gCars.size();
    *pPeds = (int)gPeds.size();
    float drift = 0;
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        Sprite* s = NULL;
        if (e.type == kEntityCar && gCars.count(e.id) && liveCars.count(gCars[e.id]))
        {
            s = gCars[e.id]->field_50_car_sprite;
        }
        else if (e.type == kEntityPed && gPeds.count(e.id) && livePeds.count(gPeds[e.id]))
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

void net::engine::Extrapolate(const Snapshot& snap, const Snapshot& prev, int ticks)
{
    const int span = (int)(snap.tick - prev.tick);
    if (span <= 0 || ticks <= 0)
    {
        return;
    }
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    size_t j = 0;
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        if (e.type != kEntityCar && e.type != kEntityPed)
        {
            continue;
        }
        while (j < prev.entities.size() && prev.entities[j].id < e.id)
        {
            j++;
        }
        if (j == prev.entities.size() || prev.entities[j].id != e.id)
        {
            continue;
        }
        const EntityState& p = prev.entities[j];
        const int32_t vx = (e.f[0] - p.f[0]) / span, vy = (e.f[1] - p.f[1]) / span;
        if (vx > 16384 || vx < -16384 || vy > 16384 || vy < -16384)
        {
            continue; // more than a block a tick: a jump, not a speed
        }
        const Fix16 x = Raw(e.f[0] + vx * ticks), y = Raw(e.f[1] + vy * ticks), z = Raw(e.f[2]);
        if (e.type == kEntityCar)
        {
            auto it = gCars.find(e.id);
            if (it != gCars.end() && liveCars.count(it->second) && it->second->field_50_car_sprite)
            {
                MoveCar(it->second, x, y, z, Angle(e.f[kCarRot]));
            }
        }
        else
        {
            auto it = gPeds.find(e.id);
            if (it != gPeds.end() && livePeds.count(it->second) && !it->second->field_16C_car)
            {
                MovePed(it->second, x, y, z, Angle(e.f[kPedRot]));
            }
        }
    }
}

void net::engine::ReplicaPositions(std::vector<ReplicaPos>& out)
{
    out.clear();
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    for (auto& c : gCars)
    {
        Sprite* s = liveCars.count(c.second) ? c.second->field_50_car_sprite : NULL;
        if (s)
        {
            ReplicaPos p = {c.first, s->field_14_xy.x.ToFloat(), s->field_14_xy.y.ToFloat()};
            out.push_back(p);
        }
    }
    for (auto& q : gPeds)
    {
        Sprite* s = livePeds.count(q.second) && q.second->field_168_game_object ? q.second->field_168_game_object->field_80_sprite_ptr : NULL;
        if (s)
        {
            ReplicaPos p = {q.first, s->field_14_xy.x.ToFloat(), s->field_14_xy.y.ToFloat()};
            out.push_back(p);
        }
    }
}
