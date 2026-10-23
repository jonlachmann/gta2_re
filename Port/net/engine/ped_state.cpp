// Port/net (docs/netcode.md, "Peds"): a ped's AI state as the server has it. A client runs every ped's AI in its
// resimulations; with only a position and a heading to go on, a ped made here (or one adopted) decided its own way and
// every snapshot pulled it back. Now a ped entity carries what the AI runs on: the plain values (objectives and their
// status, targets, speeds, timers, states, occupation, type, flags, health) and the references (the peds and cars it
// is after, its gang), as net ids. Local to each side and left alone: group, patrol route, pathfinder list, the ped
// reference table, weapons and objects (not synced).
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/Gang.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/Game.hpp"
#include <stddef.h>
#include <string.h>

namespace
{
struct Member
{
    const char* name;
    size_t offset;
    uint8_t size; // bytes: 1, 2 or 4
    bool isSigned;
};
#define PM(member, sz, sg) {#member, offsetof(Ped, member), sz, sg}
const Member kValues[] = {
    PM(field_12C_facing_angle, 2, true), PM(field_12E_aim_angle, 2, true), PM(field_130_target_facing_angle, 2, true),
    PM(field_132_follow_car_offset_angle, 2, true), PM(field_134_rotation, 2, true), PM(field_1B8_target_x, 4, true),
    PM(field_1BC_target_y, 4, true), PM(field_1C0_target_z, 4, true), PM(field_1C4_move_target_x, 4, true),
    PM(field_1C8_move_target_y, 4, true), PM(field_1CC_move_target_z, 4, true), PM(field_1D0_internal_target_x, 4, true),
    PM(field_1D4_internal_target_y, 4, true), PM(field_1D8_internal_target_z, 4, true), PM(field_1DC_objective_target_x, 4, true),
    PM(field_1E0_objective_target_y, 4, true), PM(field_1E4_objective_target_z, 4, true), PM(field_1E8_target_distance_limit, 4, true),
    PM(field_1F0_max_speed, 4, true), PM(field_1F4_walk_speed, 4, true), PM(field_1F8_run_speed, 4, true),
    PM(field_1FC_follow_car_offset_distance, 4, true), PM(field_208_invulnerability, 2, false), PM(field_20A_wanted_points, 2, true),
    PM(field_20C_target_unseen_counter, 2, false), PM(field_210_shock_counter, 2, false), PM(field_212_electrocution_threshold, 2, false),
    PM(field_218_objective_timer, 2, false), PM(field_21A_internal_objective_timer, 2, false), PM(field_220_last_voice_rng, 4, true),
    PM(field_224_bf, 1, false), PM(field_225_objective_status, 1, false), PM(field_226_internal_objective_status, 1, true),
    PM(field_228_hit_count, 1, true), PM(field_229_mug_count, 1, false), PM(field_22C_ped_class, 4, true), PM(field_230_jump_over_mode, 4, true),
    PM(field_234_lifetime_timer, 1, true), PM(field_238_ped_type, 4, true), PM(field_240_occupation, 4, true),
    PM(field_248_enter_car_as_passenger, 4, true), PM(field_24C_target_car_door, 1, false), PM(field_258_objective, 4, true),
    PM(field_25C_internal_objective, 4, true), PM(field_260_wait_counter, 1, true), PM(field_262_attackers_count, 1, false),
    PM(field_263_prev_attackers_count, 1, false), PM(field_264_killer_id_timer, 1, false), PM(field_268_electrocution_timer, 1, true),
    PM(field_26A_recent_crime_timer, 1, false), PM(field_26C_graphic_type, 4, true), PM(field_270_fire_mode, 4, true),
    PM(field_274_gang_car_model, 4, true), PM(field_288_threat_search, 4, true), PM(field_28C_threat_reaction, 4, true),
    PM(field_290_death_cause, 4, true)};
#undef PM
const size_t kValueCount = sizeof(kValues) / sizeof(kValues[0]);

// The flags that are the ped's own behaviour; the others say where it is in this game's structures (active, scheduled for
// removal, in the pathfinder's list, on fire with a flame object of this game's)
const u32 kFlagsSent = ~((1u << 0) | (1u << 10) | (1u << 14) | (1u << 24));

// Its states (Ped::field_278 ..): set only when both sides have it on foot and alive; in, into and out of cars and
// deaths go their own ways (replica.cpp, below)
enum
{
    kState1,
    kState2,
    kStored1,
    kStored2,
    kStateCount
};

// References, as net ids (0: none)
enum
{
    kRefAttacker,
    kRefObjectivePed,
    kRefInternalPed,
    kRefCarThief,
    kRefLastPunched,
    kRefHitTarget,
    kRefKiller,
    kPedRefCount
};
enum
{
    kRefStolenCar,
    kRefObjectiveCar,
    kRefTargetToEnter,
    kRefEscapeCar,
    kCarRefCount
};

// The field layout after kPedState: values, flags, health, states, ped refs, car refs, gangs
const int kFirst = net::engine::kPedAi;
const int kFlags = kFirst + (int)kValueCount;
const int kHealth = kFlags + 1;
const int kStates = kHealth + 1;
const int kPedRefs = kStates + kStateCount;
const int kCarRefs = kPedRefs + kPedRefCount;
const int kGangs = kCarRefs + kCarRefCount; // its gang, a dummy's gang
const int kEnd = kGangs + 2;
static_assert(kEnd <= net::kMaxFields, "a ped's fields");

int32_t Get(const Ped* p, const Member& d)
{
    const unsigned char* a = (const unsigned char*)p + d.offset;
    if (d.size == 1)
    {
        return d.isSigned ? (int32_t) * (const int8_t*)a : (int32_t) * (const uint8_t*)a;
    }
    if (d.size == 2)
    {
        int16_t s;
        uint16_t u;
        memcpy(&s, a, 2);
        memcpy(&u, a, 2);
        return d.isSigned ? s : u;
    }
    int32_t v;
    memcpy(&v, a, 4);
    return v;
}

void Set(Ped* p, const Member& d, int32_t v)
{
    unsigned char* a = (unsigned char*)p + d.offset;
    if (d.size == 1)
    {
        *a = (unsigned char)v;
    }
    else if (d.size == 2)
    {
        const int16_t s = (int16_t)v;
        memcpy(a, &s, 2);
    }
    else
    {
        memcpy(a, &v, 4);
    }
}

Player* PlayerAt(int i)
{
    Game* g = gGame_0x40_67E008;
    return g && i >= 0 && i < (int)g->field_23_num_players ? g->field_4_players[i] : NULL;
}

uint32_t PedId(const Ped* p) // the server's side
{
    if (!p)
    {
        return 0;
    }
    if (p->field_15C_player)
    {
        return 1 + p->field_15C_player->field_2E_idx; // a player entity
    }
    return net::engine::kPedIdBase + p->field_200_id;
}

uint32_t CarId(const Car* c)
{
    return c ? net::engine::kCarIdBase + c->field_6C_car_id : 0;
}

int GangIdx(const Gang* g)
{
    const Gang* first = gGangPool_CA8_67E274 ? &gGangPool_CA8_67E274->field_0_gang_list[0] : NULL;
    return g && first && g >= first && g < first + 10 ? (int)(g - first) + 1 : 0;
}

Gang* GangAt(int idx)
{
    return idx >= 1 && idx <= 10 && gGangPool_CA8_67E274 ? &gGangPool_CA8_67E274->field_0_gang_list[idx - 1] : NULL;
}

Ped* const* PedRefs(const Ped* p, Ped** out) // in kRef order
{
    out[kRefAttacker] = p->field_144_attacker;
    out[kRefObjectivePed] = p->field_148_objective_target_ped;
    out[kRefInternalPed] = p->field_14C_internal_target_ped;
    out[kRefCarThief] = p->field_180_car_thief;
    out[kRefLastPunched] = p->field_188_last_char_punched;
    out[kRefHitTarget] = p->field_198_hit_target_ped;
    out[kRefKiller] = p->field_1A8_ped_killer;
    return out;
}
} // namespace

void net::engine::PedStateFields(std::vector<FieldDesc>& out)
{
    for (size_t i = 0; i < kValueCount; i++)
    {
        out.push_back(FieldDesc{kValues[i].name, (uint8_t)(kValues[i].size * 8), kValues[i].isSigned});
    }
    out.push_back(FieldDesc{"flags", 32, false});
    out.push_back(FieldDesc{"health", 16, true});
    static const char* const kStateNames[kStateCount] = {"state1", "state2", "stored1", "stored2"};
    for (int i = 0; i < kStateCount; i++)
    {
        out.push_back(FieldDesc{kStateNames[i], 5, false});
    }
    static const char* const kPedRefNames[kPedRefCount] = {"attacker", "objective_ped", "internal_ped", "car_thief", "last_punched", "hit_target", "killer"};
    for (int i = 0; i < kPedRefCount; i++)
    {
        out.push_back(FieldDesc{kPedRefNames[i], 32, false});
    }
    static const char* const kCarRefNames[kCarRefCount] = {"stolen_car", "objective_car", "target_to_enter", "escape_car"};
    for (int i = 0; i < kCarRefCount; i++)
    {
        out.push_back(FieldDesc{kCarRefNames[i], 32, false});
    }
    out.push_back(FieldDesc{"gang", 4, false});
    out.push_back(FieldDesc{"dummy_gang", 4, false});
}

void net::engine::CapturePedState(const Ped* p, EntityState& e)
{
    for (size_t i = 0; i < kValueCount; i++)
    {
        e.f[kFirst + i] = Get(p, kValues[i]);
    }
    u32 flags;
    memcpy(&flags, &p->field_21C_bf, sizeof(flags));
    e.f[kFlags] = (int32_t)(flags & kFlagsSent);
    e.f[kHealth] = p->field_216_health;
    e.f[kStates + kState1] = p->field_278_ped_state_1;
    e.f[kStates + kState2] = p->field_27C_ped_state_2;
    e.f[kStates + kStored1] = p->field_280_stored_ped_state_1;
    e.f[kStates + kStored2] = p->field_284_stored_ped_state_2;
    Ped* refs[kPedRefCount];
    PedRefs(p, refs);
    for (int i = 0; i < kPedRefCount; i++)
    {
        e.f[kPedRefs + i] = (int32_t)PedId(refs[i]);
    }
    e.f[kCarRefs + kRefStolenCar] = (int32_t)CarId(p->field_140_stolen_car);
    e.f[kCarRefs + kRefObjectiveCar] = (int32_t)CarId(p->field_150_target_objective_car);
    e.f[kCarRefs + kRefTargetToEnter] = (int32_t)CarId(p->field_154_target_to_enter);
    e.f[kCarRefs + kRefEscapeCar] = (int32_t)CarId(p->field_158_escape_car);
    e.f[kGangs] = GangIdx(p->field_17C_pGang);
    e.f[kGangs + 1] = GangIdx(p->field_19C_dummy_gang);
}

namespace
{
Ped* LocalPed(uint32_t id, bool* pResolved) // the client's side: a net id to this game's ped
{
    *pResolved = true;
    if (!id)
    {
        return NULL;
    }
    if (id < net::engine::kCarIdBase)
    {
        Player* pl = PlayerAt((int)id - 1);
        Ped* p = pl && pl->field_8E_in_use ? pl->field_2C4_player_ped : NULL;
        *pResolved = p != NULL;
        return p;
    }
    Ped* p = net::engine::ReplicaPed(id);
    *pResolved = p != NULL;
    return p;
}

Car* LocalCar(uint32_t id, bool* pResolved)
{
    *pResolved = true;
    if (!id)
    {
        return NULL;
    }
    Car* c = net::engine::ReplicaCar(id);
    *pResolved = c && c->field_50_car_sprite;
    return *pResolved ? c : NULL;
}

// States that stand on their own: walking, running (or fleeing), standing. The others go with something only the side
// that played them has (a ped pulled out of a car, 8/17, uses its char's car: Char::state_8_5520A0), so they stay here
bool Plain(int32_t state1, int32_t state2)
{
    return (state1 == ped_state_1::walking_0 || state1 == ped_state_1::flee_or_running_1 || state1 == ped_state_1::standing_still_7) &&
           (state2 == ped_state_2::ped2_walking_0 || state2 == ped_state_2::ped2_staying_14);
}
} // namespace

bool net::engine::ApplyPedState(Ped* p, const EntityState& e)
{
    const bool deadThere = e.f[kStates + kState1] == ped_state_1::dead_9;
    const bool deadHere = p->isDead_403B60();
    if (deadThere && !deadHere)
    {
        if (p->field_16C_car && !p->field_248_enter_car_as_passenger)
        {
            ForceOutOfCar(p, Fix16(e.f[kPedX], 0), Fix16(e.f[kPedY], 0), Fix16(e.f[kPedZ], 0), Ang16((s16)e.f[kPedRot], (u8)0));
        }
        p->Kill_46F9D0(); // the server's death, the game's own way (as a player's, access.cpp)
        return true;
    }
    if (deadHere && !deadThere)
    {
        return false; // a death of this game's own: it can't be undone, the ped is made again
    }
    if (deadHere)
    {
        return true; // a corpse on both sides
    }
    if (p->field_16C_car || InCarTransition(p) || e.f[kPedCar] || e.f[kPedState])
    {
        // In a car, or on its way into or out of one, on either side: that is replica.cpp's (ForceIntoCar, ForceOutOfCar
        // when both have settled). The AI's objectives and targets then belong to the entering and leaving: set from the
        // server's, the char's walk to a door read a car the client had no part in (Char::state_8_5520A0)
        return true;
    }
    // The references first: one this game doesn't have (out of its area) keeps the ped as it is here for this snapshot,
    // whole. Its AI runs by its occupation as much as by its objective (a robbed driver reads its car thief:
    // Ped::RobbedDriver_AI_461630), so a part of the server's state with a target missing isn't safe
    bool ok;
    Ped* pedRefs[kPedRefCount];
    for (int i = 0; i < kPedRefCount; i++)
    {
        pedRefs[i] = LocalPed((uint32_t)e.f[kPedRefs + i], &ok);
        if (!ok)
        {
            return true;
        }
    }
    Car* carRefs[kCarRefCount];
    for (int i = 0; i < kCarRefCount; i++)
    {
        carRefs[i] = LocalCar((uint32_t)e.f[kCarRefs + i], &ok);
        if (!ok)
        {
            return true;
        }
    }
    for (size_t i = 0; i < kValueCount; i++)
    {
        Set(p, kValues[i], e.f[kFirst + i]);
    }
    u32 flags;
    memcpy(&flags, &p->field_21C_bf, sizeof(flags));
    flags = (flags & ~kFlagsSent) | ((u32)e.f[kFlags] & kFlagsSent);
    memcpy(&p->field_21C_bf, &flags, sizeof(flags));
    p->field_216_health = (s16)(e.f[kHealth] > 0 ? e.f[kHealth] : 1); // a death comes from the server's state, whole
    if (Plain(e.f[kStates + kState1], e.f[kStates + kState2]) && Plain(p->field_278_ped_state_1, p->field_27C_ped_state_2) &&
        Plain(e.f[kStates + kStored1], e.f[kStates + kStored2]) && p->field_168_game_object)
    {
        p->field_278_ped_state_1 = e.f[kStates + kState1];
        p->field_27C_ped_state_2 = e.f[kStates + kState2];
        p->field_280_stored_ped_state_1 = e.f[kStates + kStored1];
        p->field_284_stored_ped_state_2 = e.f[kStates + kStored2];
    }
    Ped** pedMembers[kPedRefCount] = {&p->field_144_attacker,         &p->field_148_objective_target_ped, &p->field_14C_internal_target_ped,
                                      &p->field_180_car_thief,        &p->field_188_last_char_punched,    &p->field_198_hit_target_ped,
                                      &p->field_1A8_ped_killer};
    for (int i = 0; i < kPedRefCount; i++)
    {
        *pedMembers[i] = pedRefs[i];
    }
    Car** carMembers[kCarRefCount] = {&p->field_140_stolen_car, &p->field_150_target_objective_car, &p->field_154_target_to_enter,
                                      &p->field_158_escape_car};
    for (int i = 0; i < kCarRefCount; i++)
    {
        *carMembers[i] = carRefs[i];
    }
    p->field_17C_pGang = GangAt(e.f[kGangs]);
    p->field_19C_dummy_gang = GangAt(e.f[kGangs + 1]);
    return true;
}
