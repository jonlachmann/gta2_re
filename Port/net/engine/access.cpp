// Port/net (docs/netcode.md, "Net schema"): the game as the session sees it. A snapshot is captured from and applied to
// the game here; the players (players.cpp), cars (cars.cpp), a client's replicas (replica.cpp) and pickups
// (pickups.cpp) do the parts. Also the helpers the engine's files share.
#include "internal.hpp"
#include "Port/port_hooks.hpp"
#include "Port/port_rand_state.h"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/char.hpp"
#include "Source/network_data.hpp"
#include "Source/rng.hpp"
#include "Source/sprite.hpp"
#include "Source/GameSession.hpp"
#include <math.h>
#include <string.h>
#include <unordered_map>

EXTERN_GLOBAL(u32, gNetworkPlayerIdx_6F56C8);
EXTERN_GLOBAL(Network_PlayerInputs, gCurrentNetInputs_6F57D8);

// --- Shared helpers ---

Player* net::engine::GetPlayer(int slot)
{
    Game* g = gGame_0x40_67E008;
    return g && slot >= 0 && slot < (int)g->field_23_num_players ? g->field_4_players[slot] : NULL;
}

Ped* net::engine::InGamePed(int slot)
{
    Player* p = GetPlayer(slot);
    return p && p->field_8E_in_use ? p->field_2C4_player_ped : NULL;
}

int32_t net::engine::CarRef(const Car* car)
{
    return car ? car->field_6C_car_id + 1 : 0;
}

uint32_t net::engine::CarRefNetId(int32_t ref)
{
    return ref ? kCarIdBase + (uint32_t)(ref - 1) : 0;
}

Car* net::engine::RefCar(int32_t ref)
{
    return ref ? ReplicaCar(CarRefNetId(ref)) : NULL;
}

Sprite* net::engine::CharSprite(const Ped* ped)
{
    return ped->field_168_game_object ? ped->field_168_game_object->field_80_sprite_ptr : NULL;
}

Sprite* net::engine::PedSprite(Ped* ped)
{
    return ped->field_16C_car ? ped->field_16C_car->field_50_car_sprite : CharSprite(ped);
}

net::EntityState net::engine::Entity(uint32_t id, uint8_t type)
{
    EntityState e;
    memset(&e, 0, sizeof(e));
    e.id = id;
    e.type = type;
    return e;
}

static_assert(net::engine::kPlayerX == 0 && net::engine::kCarX == 0 && net::engine::kPedX == 0 && net::engine::kObjX == 0 && net::engine::kPlayerRot == 3 &&
                  net::engine::kCarRot == 3 && net::engine::kPedRot == 3 && net::engine::kObjRot == 3,
              "every entity with a place has x, y, z, rot first");

void net::engine::PutPosition(EntityState& e, const Sprite* s)
{
    e.f[0] = s->field_14_xy.x.mValue;
    e.f[1] = s->field_14_xy.y.mValue;
    e.f[2] = s->field_1C_zpos.mValue;
    e.f[3] = s->field_0_rotation.rValue;
}

bool net::engine::Near(const Sprite* s, int32_t x, int32_t y, int32_t radius)
{
    const int32_t dx = s->field_14_xy.x.mValue - x, dy = s->field_14_xy.y.mValue - y;
    return dx < radius && dx > -radius && dy < radius && dy > -radius;
}

net::engine::ReachArea::ReachArea(const Snapshot& snap, float margin) : mReach(SnapshotReach(snap) - margin)
{
    mLocated = PlayerPosition(LocalPlayer(), &mX, &mY);
}

float net::engine::ReachArea::DistanceTo(const Sprite* s) const
{
    const float dx = s->field_14_xy.x.ToFloat() - mX, dy = s->field_14_xy.y.ToFloat() - mY;
    return sqrtf(dx * dx + dy * dy);
}

bool net::engine::ReachArea::Inside(const Sprite* s) const
{
    const float dx = s->field_14_xy.x.ToFloat() - mX, dy = s->field_14_xy.y.ToFloat() - mY;
    return mLocated && dx * dx + dy * dy < mReach * mReach;
}

bool net::engine::ReachArea::Beyond(const Sprite* s) const
{
    if (!s || !mLocated)
    {
        return false;
    }
    const float dx = s->field_14_xy.x.ToFloat() - mX, dy = s->field_14_xy.y.ToFloat() - mY;
    return dx * dx + dy * dy > mReach * mReach;
}

bool net::engine::IsSpareDriver(const Ped* ped)
{
    return ped && gCarManager_6C_677930 && ped == gCarManager_6C_677930->GetSpareDriver_421960();
}

void net::engine::MovePed(Ped* ped, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    Sprite* s = CharSprite(ped);
    if (!s || AtCarDoor(ped))
    {
        return; // getting in or out at the door, the game keeps its char's place in the collision grid itself: re-added, a link
                // stays behind (the walk to the door is plain walking: corrected, it no longer drifted from the server's)
    }
    gSpriteGrid_1_679208->RemoveFromRegionBuckets_477B60(s);
    s->set_xyz_lazy_420600(x, y, z);
    ped->SetRotation_433C00(rot);
    gSpriteGrid_1_679208->AddToRegionBuckets_477B20(s);
    ped->field_1AC_cam.x = x;
    ped->field_1AC_cam.y = y;
    ped->field_1AC_cam.z = z;
}

// --- The game ---

bool net::engine::GameRunning()
{
    return gGame_0x40_67E008 && gGame_0x40_67E008->field_23_num_players > 0;
}

int net::engine::PlayerCount()
{
    return gGame_0x40_67E008 ? gGame_0x40_67E008->field_23_num_players : 0;
}

int net::engine::LocalPlayer()
{
    return (int)gNetworkPlayerIdx_6F56C8;
}

uint32_t net::engine::LocalInput()
{
    return gCurrentNetInputs_6F57D8.field_0_inputs[gNetworkPlayerIdx_6F56C8].field_0_Inputs;
}

void net::engine::SetPlayerInput(int player, uint32_t buttons)
{
    if (player >= 0 && player < MAX_PLAYERS)
    {
        gCurrentNetInputs_6F57D8.field_0_inputs[player].field_0_Inputs = buttons;
    }
}

void net::engine::SetTickMs(int ms)
{
    if (gGame_0x40_67E008 && ms > 0)
    {
        gGame_0x40_67E008->field_34_network_frame_ms = ms; // Game::GetFrameDurationMs_4B8BB0 in network games
    }
}

// --- Snapshots ---

void net::engine::CapturePed(Ped* p, Car* driving, Snapshot& out)
{
    // On foot or driving (a driver is where its car is; passengers: not yet); players are their own entities
    Sprite* s = driving ? driving->field_50_car_sprite : !p->field_16C_car ? CharSprite(p) : NULL;
    if (!s || p->field_238_ped_type == ped_type::player_2 || !(driving || p->IsActive_433B40()) || IsSpareDriver(p)) // (drivers aren't "active")
    {
        return;
    }
    EntityState e = Entity(kPedIdBase + p->field_200_id, kEntityPed);
    PutPosition(e, s);
    e.f[kPedRemap] = (uint8_t)p->field_244_remap;
    e.f[kPedCar] = CarRef(driving);
    e.f[kPedState] = InCarTransition(p) ? 1 : 0;
    CapturePedState(p, e);
    out.entities.push_back(e);
}

void net::engine::Capture(Snapshot& out)
{
    out.entities.clear();
    EntityState w = Entity(kWorldId, kEntityWorld);
    w.f[kWorldCycle] = gpRng_67AB34->get_cur_rng_41CFE0();
    w.f[kWorldZoneTurn] = gPortZoneRoundRobin;
    w.f[kWorldRand] = (int32_t)PortRandState();
    w.f[kWorldIt] = gTagGame_6F8450.field_0_fugitive ? gTagGame_6F8450.field_0_fugitive->field_2E_idx + 1 : 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        w.f[kWorldItTime + i] = gTagGame_6F8450.field_4_it_time_secs[i];
    }
    out.entities.push_back(w);
    for (int i = 0; i < PlayerCount(); i++)
    {
        CapturePlayer(i, out);
    }
    // Drivers by their cars: a traffic car knows its driver (Car::field_54_driver), but the driver's own field_16C_car
    // needn't point back (measured: 1 of 12 did)
    std::unordered_map<const Ped*, Car*> driving;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_50_car_sprite)
        {
            CaptureCar(c, out);
            if (c->field_54_driver && !IsSpareDriver(c->field_54_driver))
            {
                driving[c->field_54_driver] = c;
            }
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        auto d = driving.find(p);
        CapturePed(p, d != driving.end() ? d->second : NULL, out);
    }
    CapturePickups(out);
    SortById(out);
}

void net::engine::Apply(const Snapshot& snap)
{
    if (const EntityState* w = FindWorld(snap))
    {
        ApplyWorld(*w);
    }
    ApplySlots(snap);
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityPlayer)
        {
            ApplyPlayer(e);
        }
    }
    ApplyCarsAndPeds(snap);
    ApplyPickups(snap);
}

// --- A client's view ---

void net::engine::Poses(std::vector<Pose>& out)
{
    out.clear();
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        Sprite* s = ped && !ped->field_16C_car ? CharSprite(ped) : NULL;
        if (s)
        {
            out.push_back(Pose{(uint32_t)(1 + i), s->field_14_xy.x.mValue, s->field_14_xy.y.mValue, s->field_1C_zpos.mValue, s->field_0_rotation.rValue});
        }
    }
    ReplicaPoses(out);
}

bool net::engine::PoseOf(const EntityState& e, Pose* out)
{
    if ((e.type == kEntityPlayer && !e.f[kPlayerInCar]) || e.type == kEntityCar || e.type == kEntityPed)
    {
        *out = Pose{e.id, e.f[0], e.f[1], e.f[2], e.f[3]};
        return true;
    }
    return false;
}

void net::engine::SetPose(const Pose& pose)
{
    if (pose.id >= kCarIdBase)
    {
        SetReplicaPose(pose);
        return;
    }
    Ped* ped = InGamePed((int)pose.id - 1);
    if (ped && !ped->field_16C_car && !AtCarDoor(ped) && !ped->isDead_403B60())
    {
        MovePed(ped, Raw(pose.x), Raw(pose.y), Raw(pose.z), Angle(pose.rot));
    }
}
