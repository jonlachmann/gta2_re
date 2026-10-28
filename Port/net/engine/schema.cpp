// Port/net (docs/netcode.md, "Entities"): players, cars, peds, pickups and the world as plain numbers. Positions are
// Fix16 raw values (14 fraction bits): x and y below 256 blocks fit 22 bits, z below 8 blocks 17 bits; angles are
// Ang16 (0..1439), 11 bits. So what a client gets is exactly the server's value.
#include "internal.hpp"
#include "Source/Player.hpp"
#include <deque>
#include <stdio.h>
#include <string>

namespace
{
using net::FieldDesc;

// Fields named <prefix>0 .. <prefix>n-1
void AppendNumbered(std::vector<FieldDesc>& out, const char* prefix, int count, uint8_t bits)
{
    static std::deque<std::string> sNames; // the schema keeps the pointers (a deque doesn't move them)
    for (int i = 0; i < count; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "%s%d", prefix, i);
        sNames.push_back(name);
        out.push_back(FieldDesc{sNames.back().c_str(), bits, false});
    }
}

net::Schema BuildSchema()
{
    using namespace net::engine;
    net::Schema s;
    net::EntityType player = {"player", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"in_car", 1, false}, {"buttons", 22, false},
                                         {"health", 10, true}, {"deaths", 8, false}, {"killer", 6, false}, {"weapon", 5, false},
                                         {"frags", 12, true}, {"points", 32, true}}};
    AppendNumbered(player.fields, "ammo", kPlayerWeapons, 16);
    player.fields.push_back(FieldDesc{"car", 25, false}); // kPlayerCar
    player.fields.push_back(FieldDesc{"state", 3, false}); // kPlayerState
    player.fields.push_back(FieldDesc{"target", 25, false}); // kPlayerTarget
    AppendNumbered(player.fields, "name", kPlayerNameEnd - kPlayerName, 32);
    AppendNumbered(player.fields, "power", kPowerUps, 16);
    player.fields.push_back(FieldDesc{"chatting", 1, false});
    AppendNumbered(player.fields, "chat", kPlayerChatEnd - kPlayerChat, 32);
    player.fields.push_back(FieldDesc{"wanted", 16, true}); // kPlayerWanted
    net::EntityType car = {"car", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"model", 8, false},
                                   {"driver", 2, false}, {"damage", 16, false}, {"areas", 32, false}, {"wrecked", 1, false},
                                   {"lights", 8, false}, {"flash", 8, false}, {"horn", 8, false}, {"horn_cooldown", 8, false}, {"remap", 16, true}}};
    CarPhysicsFields(car.fields);
    CarAiFields(car.fields);
    net::EntityType ped = {"ped", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"remap", 8, false},
                                   {"car", 25, false}, {"state", 1, false}}};
    PedStateFields(ped.fields);
    // The last field of cars, peds and pickups: how many ticks old its state is (0: this snapshot's; a busy area's far
    // entities get theirs less often, AgeField)
    car.fields.push_back(FieldDesc{"age", 6, false});
    ped.fields.push_back(FieldDesc{"age", 6, false});
    net::EntityType world = {"world", {{"cycle", 32, false}, {"rand", 32, false}, {"reach", 10, false}, {"it", 3, false}}};
    AppendNumbered(world.fields, "it_time", MAX_PLAYERS, 16);
    world.fields.push_back(FieldDesc{"zone_turn", 16, true});
    world.fields.push_back(FieldDesc{"round", 16, false});
    world.fields.push_back(FieldDesc{"winner", 3, false});
    world.fields.push_back(FieldDesc{"seconds_left", 13, false});
    world.fields.push_back(FieldDesc{"game", 16, false});
    world.fields.push_back(FieldDesc{"seed", 32, false}); // kWorldSeed
    world.fields.push_back(FieldDesc{"lights", 16, false}); // kWorldLights: phase 4 bits, timer 8, the cars' light state 4
    // The round flow (core/round_state.hpp): kWorldPhase ..
    world.fields.push_back(FieldDesc{"phase", 2, false});
    world.fields.push_back(FieldDesc{"phase_left", 10, false});
    world.fields.push_back(FieldDesc{"min_players", 4, false});
    world.fields.push_back(FieldDesc{"ready", 8, false});
    world.fields.push_back(FieldDesc{"vote", 2, false});
    world.fields.push_back(FieldDesc{"vote_arg", 8, false});
    world.fields.push_back(FieldDesc{"vote_yes", 8, false});
    world.fields.push_back(FieldDesc{"vote_no", 8, false});
    world.fields.push_back(FieldDesc{"vote_left", 7, false});
    AppendNumbered(world.fields, "map_choice", net::kRoundSlots, 4);
    net::EntityType object = {"object", {{"x", 22, false}, {"y", 22, false}, {"z", 17, false}, {"rot", 11, false}, {"model", 9, false}, {"idx", 8, false},
                                         {"age", 6, false}}};
    s.types.push_back(player); // kEntityPlayer
    s.types.push_back(car); // kEntityCar
    s.types.push_back(ped); // kEntityPed
    s.types.push_back(world); // kEntityWorld
    s.types.push_back(object); // kEntityObject
    return s;
}
} // namespace

const net::Schema& net::engine::NetSchema()
{
    static const Schema s = BuildSchema();
    return s;
}

int net::engine::AgeField(uint8_t type)
{
    if (type != kEntityCar && type != kEntityPed && type != kEntityObject)
    {
        return -1;
    }
    return (int)NetSchema().types[type].fields.size() - 1;
}

int net::engine::EntityAge(const EntityState& e)
{
    const int f = AgeField(e.type);
    return f < 0 ? 0 : e.f[f];
}

const net::EntityState* net::engine::FindWorld(const Snapshot& snap)
{
    // Sorted by id, the world's (0) comes first
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityWorld)
        {
            return &e;
        }
    }
    return NULL;
}

float net::engine::SnapshotReach(const Snapshot& snap)
{
    const EntityState* w = FindWorld(snap);
    return w ? w->f[kWorldReach] / 16.0f : 0;
}

void net::engine::SetSnapshotReach(Snapshot& snap, float blocks)
{
    if (EntityState* w = const_cast<EntityState*>(FindWorld(snap)))
    {
        const int v = (int)(blocks * 16);
        w->f[kWorldReach] = v < 0 ? 0 : v > 1023 ? 1023 : v;
    }
}
