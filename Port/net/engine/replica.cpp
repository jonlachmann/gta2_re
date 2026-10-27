// Port/net (docs/netcode.md, "Client"): a snapshot's cars and peds in a client's game. Each server entity gets a
// local one: adopted if the client's game already has the same one (the map script made it on both sides at the
// start), else created; moved to the server's state every snapshot; removed when the server no longer sends it.
// Local cars and peds the server doesn't know (other than the players' own) are removed.
#include "internal.hpp"
#include "Port/port_rand_owner.h"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/char.hpp"
#include "Source/sprite.hpp"
#include <stdio.h>
#include <set>
#include <unordered_map>

struct net::engine::ReplicaState
{
    std::unordered_map<uint32_t, Car*> cars;
    std::unordered_map<uint32_t, Ped*> peds;
    std::unordered_map<uint32_t, Object*> objects;
};

namespace
{
using namespace net::engine;
using net::EntityState;
using net::Snapshot;

std::unordered_map<uint32_t, Car*> gCars; // NetId -> this game's car
std::unordered_map<uint32_t, Ped*> gPeds;
const int32_t kAdoptRadius = 4096; // a quarter block: the same car or ped as the server's

// For the log (PrintReplicaLog)
int gRefusedPeds[3]; // why: no ped, no char, no sprite
int gOccupiedKept;
int gPedsRemade;

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

// What one apply of a snapshot works with
struct Pass
{
    std::set<Car*> liveCars; // this game's live cars and peds; a mapped one the game removed by itself (a wreck, a death, or a
    std::set<Ped*> livePeds; // resimulation since the map was saved) is forgotten
    // The players' peds, the cars they sit in and their corpses (Player::Wasted_567130 leaves a clone, the aux ped): the
    // players' own (players.cpp), never adopted or removed here
    std::set<const void*> owned;
    std::set<const void*> mapped; // owned, or a snapshot entity's
    std::set<const Car*> playerIn; // the cars players sit in (not the ones they are getting into: those stay the server's)
    std::set<Car*> referenced; // cars a live ped points to
    std::set<uint32_t> seen; // the snapshot's cars and peds
};

void Gather(Pass& a)
{
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        a.liveCars.insert(c);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        a.livePeds.insert(p);
        if (p->field_16C_car && !IsSpareDriver(p))
        {
            a.referenced.insert(p->field_16C_car);
        }
    }
    for (int i = 0; i < PlayerCount(); i++)
    {
        Player* player = GetPlayer(i);
        if (!player || !player->field_8E_in_use)
        {
            continue; // out of the game: its last ped is a pedestrian like any other
        }
        Ped* peds[2] = {player->field_2C4_player_ped, player->field_2C8_aux_ped};
        for (int k = 0; k < 2; k++)
        {
            if (peds[k])
            {
                a.owned.insert(peds[k]);
                if (peds[k]->field_16C_car)
                {
                    a.owned.insert(peds[k]->field_16C_car);
                }
                if (peds[k]->field_150_target_objective_car) // the car it is getting into
                {
                    a.owned.insert(peds[k]->field_150_target_objective_car);
                }
            }
        }
        if (player->field_2C4_player_ped && player->field_2C4_player_ped->field_16C_car)
        {
            a.playerIn.insert(player->field_2C4_player_ped->field_16C_car);
        }
    }
    a.mapped = a.owned;
}

// The maps without what the game removed since
void Prune(Pass& a)
{
    for (auto it = gCars.begin(); it != gCars.end();)
    {
        it = a.liveCars.count(it->second) && !it->second->IsDespawning_4215B0() ? (a.mapped.insert(it->second), ++it) : gCars.erase(it);
    }
    for (auto it = gPeds.begin(); it != gPeds.end();)
    {
        // A player's ped is never a snapshot ped's: one mapped before it was reused for a player's (the pool hands the
        // slot out again) would have the server's ped steer the player (out of its car, the car left driverless)
        // (a passenger isn't "active" either, as a driver isn't, but no car re-adopts it: kept while it sits in one)
        Ped* p = it->second;
        const bool riding = p->field_16C_car && p->field_248_enter_car_as_passenger;
        it = a.livePeds.count(p) && (p->IsActive_433B40() || riding) && !a.owned.count(p) ? (a.mapped.insert(p), ++it) : gPeds.erase(it);
    }
}

// A car goes, unless a ped points to it (its driver, a passenger, a corpse cloned in it, Player::Wasted_567130): that
// ped would point at a removed car (the sound listener's camera follows a player's corpse and reads its car,
// sound_obj::InterrogateAudioEntities_41A730)
// A ped here going for it (to get in, after it, at its door) keeps it too: freed, the ped read it on (its sprite,
// Char::state_3_551A00, a crash). It goes on a later snapshot, once the server's ped is about something else
bool TargetedHere(Car* car)
{
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        Char* c = p->field_168_game_object;
        if (p->field_150_target_objective_car == car || p->field_154_target_to_enter == car || p->field_16C_car == car ||
            (c && c->field_84_target_car == car))
        {
            return true;
        }
    }
    return false;
}

void RemoveCar(const Pass& a, Car* car)
{
    if ((car->field_54_driver && !IsSpareDriver(car->field_54_driver)) || a.referenced.count(car) || TargetedHere(car))
    {
        gOccupiedKept++;
        return;
    }
    car->DespawnSoon_421490();
}

// A ped goes, once (Deallocate_45EB60 undoes counters and groups, and marks it inactive)
void RemovePed(Ped* p)
{
    if (p->field_16C_car && p->field_16C_car->field_54_driver != p && !p->field_248_enter_car_as_passenger)
    {
        p->field_16C_car = NULL; // not that car's driver (any more): Deallocate_45EB60 would clear whoever is (a player)
    }
    if (p->IsActive_433B40() && !p->field_16C_car)
    {
        p->Deallocate_45EB60();
    }
}

// This game's car for a snapshot car: mapped, adopted (the same car here) or made
Car* MapCar(Pass& a, const EntityState& e)
{
    auto it = gCars.find(e.id);
    Car* car = it != gCars.end() ? it->second : NULL;
    if (car && car->field_84_car_info_idx != e.f[kCarModel])
    {
        RemoveCar(a, car); // another model under this id: should not happen, ids aren't reused
        gCars.erase(it);
        car = NULL;
    }
    if (car)
    {
        return car;
    }
    for (Car* c : a.liveCars)
    {
        if (!a.mapped.count(c) && c->field_50_car_sprite && c->field_84_car_info_idx == e.f[kCarModel] &&
            Near(c->field_50_car_sprite, e.f[kCarX], e.f[kCarY], kAdoptRadius))
        {
            car = c;
            break;
        }
    }
    if (!car && RoomForCar())
    {
        car = gCarManager_6C_677930->SpawnCarAt_446230(Raw(e.f[kCarX]), Raw(e.f[kCarY]), Raw(e.f[kCarZ]), Angle(e.f[kCarRot]), e.f[kCarModel], Fix16(1));
    }
    if (car)
    {
        gCars[e.id] = car;
        a.mapped.insert(car);
    }
    return car;
}

void ApplyCar(Pass& a, const EntityState& e)
{
    a.seen.insert(e.id);
    Car* car = MapCar(a, e);
    if (!car || EntityAge(e))
    {
        return; // none, or the state the client already has (a busy area's far car): predicted on here
    }
    // The shared traffic driver, as the server has it (a car with its own driver gets it from its ped entity)
    if (!a.owned.count(car))
    {
        const bool spare = IsSpareDriver(car->field_54_driver);
        if (e.f[kCarDriver] == kDriverTraffic && !car->field_54_driver)
        {
            car->SpawnDriverPed();
        }
        else if (e.f[kCarDriver] != kDriverTraffic && spare)
        {
            car->SetDriver(NULL);
        }
    }
    if (!a.playerIn.count(car)) // a car a player sits in follows the player (players.cpp); one being entered, the server
    {
        const bool before = DebugGridOk("before moving a car", car);
        MoveCar(car, Raw(e.f[kCarX]), Raw(e.f[kCarY]), Raw(e.f[kCarZ]), Angle(e.f[kCarRot]));
        if (before)
        {
            DebugGridOk("after moving a car", car);
        }
    }
    SetCarPhysics(car, e); // a player's car too: its place came with the player
    ApplyCarAi(car, e);
    if (!SetCarState(car, e) && !a.playerIn.count(car) && !a.referenced.count(car))
    {
        // A wreck here that isn't one on the server can't be unexploded: it goes, and the next snapshot makes the car
        // again as the server has it
        RemoveCar(a, car);
        gCars.erase(e.id);
        a.mapped.erase(car);
        gWrecksReplaced++;
    }
}

// This game's ped for a snapshot ped: mapped, adopted (its car's driver, or the same ped here) or made
Ped* MapPed(Pass& a, const EntityState& e, Car* serverCar)
{
    auto it = gPeds.find(e.id);
    Ped* ped = it != gPeds.end() ? it->second : NULL;
    if (!ped && serverCar && PassengerThere(e))
    {
        // A passenger: made at its car (ApplyPed seats it)
        if (Sprite* cs = serverCar->field_50_car_sprite; cs && RoomForPed())
        {
            ped = gPedManager_6787BC->SpawnPedAt(cs->field_14_xy.x, cs->field_14_xy.y, cs->field_1C_zpos, (u8)e.f[kPedRemap], cs->field_0_rotation);
        }
    }
    else if (!ped && serverCar)
    {
        // Its car's driver here, if nobody's (the map script or the game made it), else a new one
        Ped* d = serverCar->field_54_driver;
        if (d && a.livePeds.count(d) && !a.mapped.count(d) && !IsSpareDriver(d))
        {
            ped = d;
        }
        else if ((!d || IsSpareDriver(d)) && RoomForPed())
        {
            serverCar->SetDriver(NULL);
            ped = gPedManager_6787BC->SpawnDriver_470B00(serverCar);
        }
    }
    if (!ped && !serverCar)
    {
        for (Ped* p : a.livePeds)
        {
            Sprite* s = CharSprite(p);
            if (!a.mapped.count(p) && s && !p->field_16C_car && p->IsActive_433B40() && (u8)p->field_244_remap == (u8)e.f[kPedRemap] &&
                Near(s, e.f[kPedX], e.f[kPedY], kAdoptRadius))
            {
                ped = p;
                break;
            }
        }
        if (!ped && RoomForPed())
        {
            ped = gPedManager_6787BC->SpawnPedAt(Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), (u8)e.f[kPedRemap], Angle(e.f[kPedRot]));
        }
    }
    if (ped && !gPeds.count(e.id))
    {
        gPeds[e.id] = ped;
        a.mapped.insert(ped);
        ped->field_244_remap = (char_type)e.f[kPedRemap] ^ 1; // set below, with its char's
    }
    return ped;
}

void ApplyPed(Pass& a, const EntityState& e)
{
    // A driver needs its car here first (cars come before peds: their NetIds are smaller)
    Car* serverCar = RefCar(e.f[kPedCar]);
    if (e.f[kPedCar] && (!serverCar || !a.liveCars.count(serverCar)))
    {
        return;
    }
    a.seen.insert(e.id);
    Ped* ped = MapPed(a, e, serverCar);
    if (!ped || EntityAge(e))
    {
        return; // none, or the state the client already has: predicted on here
    }
    // Its look: the remap and its char's colours (a ped made here, or adopted, had its own: a gang member's showed
    // another gang's)
    if ((u8)ped->field_244_remap != (u8)e.f[kPedRemap])
    {
        ped->field_244_remap = (char_type)e.f[kPedRemap];
        if (ped->field_168_game_object)
        {
            ped->SetRemap_433C10((u8)e.f[kPedRemap]);
        }
    }
    // In the server's car, or on foot, as the server has it (when neither side is getting in or out)
    const bool settled = !e.f[kPedState] && !InCarTransition(ped) && !ped->isDead_403B60();
    if (serverCar && PassengerThere(e))
    {
        // A passenger there (into a player's car too: an instant gang rides with its leader)
        if (!IsPassengerOf(ped, serverCar) && settled)
        {
            ForceIntoCarAsPassenger(ped, serverCar);
        }
    }
    else if (serverCar)
    {
        // (or its driver here already, but not sitting in it: a police car's driver walked, Police off here)
        if ((serverCar->field_54_driver != ped || ped->field_278_ped_state_1 != ped_state_1::in_car_10) && settled && !a.owned.count(serverCar))
        {
            ForceIntoCar(ped, serverCar);
        }
    }
    else if (ped->field_16C_car)
    {
        if (settled)
        {
            ForceOutOfCar(ped, Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), Angle(e.f[kPedRot]));
        }
    }
    else
    {
        MovePed(ped, Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), Angle(e.f[kPedRot]));
    }
    if (!ApplyPedState(ped, e))
    {
        // A corpse of this game's own (a death predicted that the server didn't have): it goes, and the next snapshot
        // makes the ped again
        RemovePed(ped);
        gPeds.erase(e.id);
        a.mapped.erase(ped);
        gPedsRemade++;
    }
}

// Groups (a gang following its leader) once every ped of the snapshot is here
void ApplyGroups(const Snapshot& snap)
{
    std::vector<std::pair<Ped*, const EntityState*>> groupPeds;
    for (const EntityState& e : snap.entities)
    {
        auto it = e.type == kEntityPed ? gPeds.find(e.id) : gPeds.end();
        if (it != gPeds.end() && (it->second->IsActive_433B40() || (it->second->field_16C_car && it->second->field_248_enter_car_as_passenger)))
        {
            groupPeds.push_back(std::make_pair(it->second, &e));
        }
    }
    ApplyPedGroups(groupPeds);
}

// What the server no longer sends goes, within what this snapshot reached (beyond it, a full snapshot was cut for size:
// the car or ped is still there); so do this game's own cars and peds nobody mapped
void RemoveUnsent(const Pass& a, const Snapshot& snap)
{
    const ReachArea area(snap, 1.0f);
    for (auto it = gCars.begin(); it != gCars.end();)
    {
        if (a.seen.count(it->first) || area.Beyond(it->second->field_50_car_sprite))
        {
            ++it;
            continue;
        }
        RemoveCar(a, it->second);
        it = gCars.erase(it);
    }
    for (auto it = gPeds.begin(); it != gPeds.end();)
    {
        if (a.seen.count(it->first) || area.Beyond(PedSprite(it->second)))
        {
            ++it;
            continue;
        }
        Ped* q = it->second;
        if (q->field_16C_car && q->field_248_enter_car_as_passenger && !q->IsActive_433B40())
        {
            // A passenger the server doesn't have: out of its car's list (Deallocate_45EB60 leaves it there), then gone
            q->field_16C_car->field_4_passengers_list.RemovePed_471240(q);
            q->field_248_enter_car_as_passenger = 0;
            q->field_16C_car = NULL;
            q->Deallocate_45EB60();
        }
        else if (it->second->IsActive_433B40())
        {
            Ped* p = it->second;
            if (p->field_16C_car && p->field_16C_car->field_54_driver != p && !p->field_248_enter_car_as_passenger)
            {
                p->field_16C_car = NULL; // not that car's driver (any more): Deallocate_45EB60 would clear whoever is (a player)
            }
            p->Deallocate_45EB60();
        }
        it = gPeds.erase(it);
    }
    for (Car* c : a.liveCars)
    {
        if (!a.owned.count(c) && !a.mapped.count(c) && !c->IsDespawning_4215B0())
        {
            RemoveCar(a, c);
        }
    }
    for (Ped* p : a.livePeds)
    {
        // Drivers are sent too; passengers (not yet) stay with their car
        const bool passenger = p->field_16C_car && p->field_16C_car->field_54_driver != p;
        if (!a.owned.count(p) && !a.mapped.count(p) && !passenger && !IsSpareDriver(p) && (p->field_168_game_object || p->field_16C_car) &&
            p->IsActive_433B40())
        {
            p->Deallocate_45EB60();
        }
    }
}
} // namespace

void net::engine::ApplyCarsAndPeds(const Snapshot& snap)
{
    Pass a;
    Gather(a);
    Prune(a);
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityCar)
        {
            ApplyCar(a, e);
        }
        else if (e.type == kEntityPed)
        {
            ApplyPed(a, e);
        }
    }
    ApplyGroups(snap);
    RemoveUnsent(a, snap);
}

void net::engine::PrintReplicaLog()
{
    if (gExplosionsCarried + gWrecksReplaced)
    {
        printf("net: %d explosions the server had carried out, %d wrecks of this game's own made again\n", gExplosionsCarried, gWrecksReplaced);
        gExplosionsCarried = gWrecksReplaced = 0;
    }
    if (gPickupsMade + gPickupsRemoved)
    {
        printf("net: %d pickups made as the server's, %d removed\n", gPickupsMade, gPickupsRemoved);
        gPickupsMade = gPickupsRemoved = 0;
    }
    if (gPedsRemade)
    {
        printf("net: %d corpses of this game's own made again\n", gPedsRemade);
        gPedsRemade = 0;
    }
    if (gOccupiedKept)
    {
        printf("net: %d removals of occupied cars skipped\n", gOccupiedKept);
        gOccupiedKept = 0;
    }
    if (gRefusedPeds[0] + gRefusedPeds[1] + gRefusedPeds[2])
    {
        printf("net: peds not created: %d for want of a ped, %d of a char, %d of a sprite (free: %d peds, %d chars, %d sprites)\n", gRefusedPeds[0],
               gRefusedPeds[1], gRefusedPeds[2], FreeSlots(gPedPool_6787B8->field_0_pool.field_0_pStart),
               FreeSlots(gChar_B4_Pool_6FDB44->field_0_pool.field_0_pHead), FreeSlots(gSpritePool_703818->field_0_pool.field_0_pHead));
        gRefusedPeds[0] = gRefusedPeds[1] = gRefusedPeds[2] = 0;
    }
}

net::engine::ReplicaState* net::engine::SaveReplicas(ReplicaState* reuse)
{
    ReplicaState* state = reuse ? reuse : new ReplicaState();
    state->cars = gCars;
    state->peds = gPeds;
    state->objects = PickupMap();
    return state;
}

void net::engine::RestoreReplicas(const ReplicaState* state)
{
    gCars = state->cars;
    gPeds = state->peds;
    PickupMap() = state->objects;
}

void net::engine::FreeReplicas(ReplicaState* state)
{
    delete state;
}

Ped* net::engine::ReplicaPed(uint32_t netId)
{
    auto it = gPeds.find(netId);
    return it != gPeds.end() ? it->second : NULL;
}

Car* net::engine::ReplicaCar(uint32_t netId)
{
    auto it = gCars.find(netId);
    return it != gCars.end() ? it->second : NULL;
}

void net::engine::ClearReplicas()
{
    gCars.clear();
    gPeds.clear();
}

// --- The owners of random draws (Port/port_rand_owner.h): an entity's key is the server's net id ---

namespace
{
std::unordered_map<const Ped*, uint32_t> gPedKeys; // a client's: this game's ped -> the server's id, made each tick
std::unordered_map<const Car*, uint32_t> gCarKeys;

unsigned int ServerPedKey(const Ped* p)
{
    return p->field_15C_player ? 1 + p->field_15C_player->field_2E_player_idx : net::engine::kPedIdBase + p->field_200_id;
}

unsigned int ServerCarKey(const Car* c)
{
    return net::engine::kCarIdBase + c->field_6C_car_id;
}

unsigned int ClientPedKey(const Ped* p)
{
    if (p->field_15C_player)
    {
        return 1 + p->field_15C_player->field_2E_player_idx;
    }
    auto it = gPedKeys.find(p);
    return it != gPedKeys.end() ? it->second : 0; // one of this game's own: the game's stream
}

unsigned int ClientCarKey(const Car* c)
{
    auto it = gCarKeys.find(c);
    return it != gCarKeys.end() ? it->second : 0;
}

void ClientTickStart()
{
    // ponytail: made again every tick (a few hundred entries); the maps change with every apply and restore
    gPedKeys.clear();
    gCarKeys.clear();
    for (const auto& kv : gPeds)
    {
        gPedKeys[kv.second] = kv.first;
    }
    for (const auto& kv : gCars)
    {
        gCarKeys[kv.second] = kv.first;
    }
}
} // namespace

void net::engine::SetRandOwners(bool client)
{
    gPortRandOwned = true;
    gPortRandPedKey = client ? ClientPedKey : ServerPedKey;
    gPortRandCarKey = client ? ClientCarKey : ServerCarKey;
    gPortRandTickHook = client ? ClientTickStart : NULL;
}

void net::engine::ClearRandOwners()
{
    gPortRandOwned = false;
    gPortRandPedKey = NULL;
    gPortRandCarKey = NULL;
    gPortRandTickHook = NULL;
}

const std::unordered_map<uint32_t, Car*>& net::engine::ReplicaCars()
{
    return gCars;
}

const std::unordered_map<uint32_t, Ped*>& net::engine::ReplicaPeds()
{
    return gPeds;
}

namespace
{
// The mapped ones still in this game (the maps are pruned only when a snapshot is applied)
template <class F> void ForLiveReplicas(F f)
{
    std::set<Car*> cars;
    std::set<Ped*> peds;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars.insert(c);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        peds.insert(p);
    }
    for (auto& c : gCars)
    {
        if (cars.count(c.second))
        {
            f(c.first, c.second, (Ped*)NULL);
        }
    }
    for (auto& q : gPeds)
    {
        if (peds.count(q.second))
        {
            f(q.first, (Car*)NULL, q.second);
        }
    }
}
} // namespace

void net::engine::ReplicaPositions(std::vector<ReplicaPos>& out)
{
    out.clear();
    ForLiveReplicas([&](uint32_t id, Car* car, Ped* ped) {
        if (Sprite* s = car ? car->field_50_car_sprite : CharSprite(ped))
        {
            out.push_back(ReplicaPos{id, s->field_14_xy.x.ToFloat(), s->field_14_xy.y.ToFloat()});
        }
    });
}

void net::engine::ReplicaPoses(std::vector<Pose>& out)
{
    ForLiveReplicas([&](uint32_t id, Car* car, Ped* ped) {
        if (Sprite* s = car ? car->field_50_car_sprite : !ped->field_16C_car ? CharSprite(ped) : NULL)
        {
            out.push_back(Pose{id, s->field_14_xy.x.mValue, s->field_14_xy.y.mValue, s->field_1C_zpos.mValue, s->field_0_rotation.rValue});
        }
    });
}

void net::engine::SetReplicaPose(const Pose& p)
{
    if (p.id >= kPedIdBase)
    {
        Ped* ped = ReplicaPed(p.id);
        if (ped && !ped->field_16C_car)
        {
            MovePed(ped, Raw(p.x), Raw(p.y), Raw(p.z), Angle(p.rot));
        }
    }
    else if (Car* car = ReplicaCar(p.id))
    {
        if (car->field_50_car_sprite)
        {
            MoveCar(car, Raw(p.x), Raw(p.y), Raw(p.z), Angle(p.rot));
        }
    }
}
