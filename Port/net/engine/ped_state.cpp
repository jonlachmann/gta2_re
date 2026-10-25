// Port/net (docs/netcode.md, "Peds"): a ped's AI state as the server has it. A client runs every ped's AI in its
// resimulations; with only a position and a heading to go on, a ped made here (or one adopted) decided its own way and
// every snapshot pulled it back. Now a ped entity carries what the AI runs on: the plain values (objectives and their
// status, targets, speeds, timers, states, occupation, type, flags, health) and the references (the peds and cars it
// is after, its gang), as net ids. Local to each side and left alone: group, patrol route, pathfinder list, the ped
// reference table, weapons and objects (not synced).
#include "internal.hpp"
#include "members.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Gang.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/char.hpp"
#include "Source/Weapon.hpp"
#include "Source/PedGroup.hpp"
#include "Source/Game.hpp"
#include <stddef.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace
{
using net::engine::Member;
#define PM(member, sz, sg) NET_MEMBER(Ped, member, sz, sg)
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

const int kObjective = net::engine::MemberIndex(kValues, kValueCount, offsetof(Ped, field_258_objective));
const int kPassenger = net::engine::MemberIndex(kValues, kValueCount, offsetof(Ped, field_248_enter_car_as_passenger));
const int kDoor = net::engine::MemberIndex(kValues, kValueCount, offsetof(Ped, field_24C_target_car_door));

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
const int kChar = kGangs + 2; // its char's movement and animation (0 without a char)
#define CM(member, sz, sg) NET_MEMBER(Char, member, sz, sg)
const Member kCharValues[] = {CM(field_10_char_state, 4, true),      CM(field_14_target_rotation, 2, true),  CM(field_38_velocity, 4, true),
                              CM(field_3C_run_or_jump_speed, 4, true), CM(field_98_velocity_vector.x, 4, true), CM(field_98_velocity_vector.y, 4, true),
                              CM(field_6C_animation_state, 4, true), CM(field_68_animation_frame, 1, false), CM(field_70_frame_timer, 1, true),
                              CM(field_71_frame_delay, 1, true),
                              // its wander: the next idle behaviour's timer and the turn it takes (Char::SelectRandomIdleBehavior_54C580,
                              // ApplyRandomRotationJitter_54C6C0), its jumps, and the push off another ped (Char::state_1_5504F0)
                              CM(field_46_timer, 2, false), CM(field_42_rotation_jitter, 2, true), CM(field_54_jump_scale_counter, 1, true),
                              CM(field_6A_repulsion_timer, 1, false), CM(field_74_repulsion_rotation, 2, true)};
#undef CM
const size_t kCharCount = sizeof(kCharValues) / sizeof(kCharValues[0]);
const int kCharState = net::engine::MemberIndex(kCharValues, kCharCount, offsetof(Char, field_10_char_state));
const int kCharAnimation = net::engine::MemberIndex(kCharValues, kCharCount, offsetof(Char, field_6C_animation_state));
const int kGroup = kChar + (int)kCharCount; // its group: leader (net id), its index (99: the leader), type, counts
const int kWeapons = kGroup + 5; // its weapon and its second one: weapon_type + 1, 0 none
const int kEnd = kWeapons + 2;
static_assert(kEnd + 1 <= net::kMaxFields, "a ped's fields and its age (access.cpp)");

int32_t WeaponRef(const Weapon* w) // weapon_type + 1, 0 none
{
    return w ? w->field_1C_idx + 1 : 0;
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
    AppendFields(kValues, kValueCount, out);
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
    AppendFields(kCharValues, kCharCount, out);
    out.push_back(FieldDesc{"group_leader", 32, false});
    out.push_back(FieldDesc{"group_idx", 8, false});
    out.push_back(FieldDesc{"group_type", 8, true});
    out.push_back(FieldDesc{"group_count", 8, false});
    out.push_back(FieldDesc{"group_count2", 8, false});
    out.push_back(FieldDesc{"weapon", 6, false});
    out.push_back(FieldDesc{"weapon2", 6, false});
}

void net::engine::PedStateRefFields(std::vector<int>* peds, std::vector<int>* cars)
{
    for (int i = 0; i < kPedRefCount; i++)
    {
        peds->push_back(kPedRefs + i);
    }
    peds->push_back(kGroup);
    for (int i = 0; i < kCarRefCount; i++)
    {
        cars->push_back(kCarRefs + i);
    }
}

void net::engine::CapturePedState(const Ped* p, EntityState& e)
{
    CaptureMembers(p, kValues, kValueCount, e, kFirst);
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
    if (const Char* c = p->field_168_game_object)
    {
        CaptureMembers(c, kCharValues, kCharCount, e, kChar);
    }
    e.f[kWeapons] = WeaponRef(p->field_170_selected_weapon);
    e.f[kWeapons + 1] = WeaponRef(p->field_174_pWeapon);
    if (const PedGroup* g = p->field_164_ped_group)
    {
        e.f[kGroup] = (int32_t)PedId(g->field_2C_ped_leader);
        e.f[kGroup + 1] = p->field_23C_group_idx;
        e.f[kGroup + 2] = g->field_38_group_type;
        e.f[kGroup + 3] = g->field_34_count;
        e.f[kGroup + 4] = (u8)g->field_36_count;
    }
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
        Ped* p = net::engine::InGamePed((int)id - 1);
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

// A char's states that go with those: not jumping or colliding with a car (Char_B4_state 15, 27), not jumping, at a car
// door or falling (animations 5-8, 11, 12)
bool CharPlain(int32_t charState, int32_t anim)
{
    return charState != Char_B4_state::Jumping_15 && charState != Char_B4_state::Colliding_With_Car_27 && !(anim >= 5 && anim <= 8) && anim != 11 &&
           anim != 12;
}

// States that stand on their own: walking, running (or fleeing), standing. The others go with something only the side
// that played them has (a ped pulled out of a car, 8/17, uses its char's car: Char::state_8_5520A0), so they stay here
bool Plain(int32_t state1, int32_t state2)
{
    return (state1 == ped_state_1::walking_0 || state1 == ped_state_1::flee_or_running_1 || state1 == ped_state_1::standing_still_7) &&
           (state2 == ped_state_2::ped2_walking_0 || state2 == ped_state_2::ped2_staying_14);
}

// The stored states (Ped::ChangeNextPedState1_45C500's): plain ones, or none yet (11/28, as Ped's constructor sets them)
bool PlainStored(int32_t state1, int32_t state2)
{
    return Plain(state1, state2) || (state1 == 11 && state2 == ped_state_2::Unknown_28);
}

// Its weapons, the game's way of arming a ped (Ped::ForceWeapon_46F600 drops the one it had): a gang member here had
// none and never showed the shots the server's fired
void ApplyWeapons(Ped* p, const net::EntityState& e)
{
    if (WeaponRef(p->field_170_selected_weapon) != e.f[kWeapons])
    {
        if (e.f[kWeapons])
        {
            p->ForceWeapon_46F600(e.f[kWeapons] - 1);
        }
        else
        {
            p->RemovePedWeapons_462510();
        }
    }
    if (WeaponRef(p->field_174_pWeapon) != e.f[kWeapons + 1])
    {
        if (e.f[kWeapons + 1])
        {
            p->GiveWeapon_46F650(e.f[kWeapons + 1] - 1);
        }
        else
        {
            p->RemoveSecondaryWeapon_462550();
        }
    }
}
} // namespace

void net::engine::ApplyPedGroups(const std::vector<std::pair<Ped*, const EntityState*>>& peds)
{
    // The members each leader has here, in the server's order: the game walks a group's list up to its count
    // (field_34_count) without looking for gaps, so it is made dense with only the members that are here
    std::map<Ped*, std::vector<std::pair<int, Ped*>>> byLeader;
    std::set<Ped*> placed;
    for (size_t k = 0; k < peds.size(); k++)
    {
        Ped* p = peds[k].first;
        const EntityState& e = *peds[k].second;
        bool ok;
        Ped* leader = e.f[kGroup] ? LocalPed((uint32_t)e.f[kGroup], &ok) : NULL;
        const int idx = e.f[kGroup + 1];
        if (leader && leader != p && idx != 99 && !p->isDead_403B60() && !leader->isDead_403B60() && !p->field_16C_car)
        {
            byLeader[leader].push_back(std::make_pair(idx, p));
            placed.insert(p);
        }
        else if (p->field_164_ped_group && p->field_164_ped_group->field_2C_ped_leader != p && !p->field_16C_car)
        {
            p->field_164_ped_group->RemovePed_4C9970(p); // in no group there (or its leader isn't here)
        }
    }
    for (auto& g : byLeader)
    {
        Ped* leader = g.first;
        std::vector<std::pair<int, Ped*>>& members = g.second;
        std::sort(members.begin(), members.end());
        PedGroup* group = leader->field_164_ped_group;
        if (!group)
        {
            group = PedGroup::New_4CB0D0(); // as the game makes one (SpawnPedChainGroupAt_46DB90)
            if (!group)
            {
                continue;
            }
            group->add_ped_leader_4C9B10(leader);
        }
        // Out of any other group first, then the list as it should be
        for (size_t m = 0; m < members.size(); m++)
        {
            PedGroup* other = members[m].second->field_164_ped_group;
            if (other && other != group && other->field_2C_ped_leader != members[m].second)
            {
                other->RemovePed_4C9970(members[m].second);
            }
        }
        std::set<Ped*> now;
        for (size_t m = 0; m < members.size(); m++)
        {
            now.insert(members[m].second);
        }
        for (int m = 0; m < 9; m++)
        {
            Ped* was = group->field_4_ped_list[m];
            if (was && !now.count(was) && was->field_164_ped_group == group)
            {
                was->reset_ped_group(); // listed here before, not any more
            }
            group->field_4_ped_list[m] = NULL;
        }
        const size_t n = members.size() < 9 ? members.size() : 9;
        for (size_t m = 0; m < n; m++)
        {
            group->add_ped_to_list_4C9B30(members[m].second, (u8)m);
        }
        group->field_34_count = (u8)n;
        group->field_36_count = (char_type)n;
    }
    // A member whose leader isn't here is in no group here: its objective to follow the group goes too (the server's,
    // copied with the AI state; Ped::UpdateGroupMemberFollow_469BD0 assumes a group)
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext) // every ped: one taken out above too
    {
        if (!p->field_164_ped_group &&
            (p->field_258_objective == objectives_enum::objective_8 || p->field_25C_internal_objective == objectives_enum::objective_8))
        {
            p->field_258_objective = objectives_enum::no_obj_0;
            p->field_25C_internal_objective = objectives_enum::no_obj_0;
        }
    }
}


namespace
{
// The server's ped on its way into or out of a car and this game's settled: started here the way the game starts it
// (as a player's, players.cpp's StartTransition), else it went on with what it did before until both had settled
// (a cop at a police car's door on the server walked off here). Into the server's target car, with its seat and
// door; out of the car both have it in
bool StartCarTransition(Ped* p, const net::EntityState& e)
{
    using namespace net::engine;
    if (p->field_15C_player || !e.f[kPedState] || InCarTransition(p) || !p->field_168_game_object == !p->field_16C_car)
    {
        return false; // a player's (players.cpp), the server's settled, ours on its way already, or neither on foot nor in a car
    }
    const int32_t objective = e.f[kFirst + kObjective];
    if (!p->field_16C_car && !e.f[kPedCar] && objective == objectives_enum::enter_car_as_driver_35)
    {
        bool ok;
        Car* car = LocalCar((uint32_t)e.f[kCarRefs + kRefObjectiveCar], &ok);
        if (!car || !car->field_50_car_sprite)
        {
            return false; // not here (yet)
        }
        StartEntering(p, car);
        p->set_enter_car_as_passenger_4039B0((char_type)e.f[kFirst + kPassenger]);
        p->set_target_car_door_403A70((char_type)e.f[kFirst + kDoor]);
        return true;
    }
    const bool leaving = objective == objectives_enum::leave_car_36 || e.f[kStates + kState1] == ped_state_1::exiting_car_4 ||
                         e.f[kStates + kState2] == ped_state_2::ped2_getting_out_a_car_7; // as LeavingCar (vehicles.cpp)
    if (p->field_16C_car && RefCar(e.f[kPedCar]) == p->field_16C_car && leaving && p->field_16C_car->CanExitCar_43AF10())
    {
        StartLeaving(p);
        return true;
    }
    return false;
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
            ForceOutOfCar(p, Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), Angle(e.f[kPedRot]));
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
    ApplyWeapons(p, e);

    // Sitting in the same car on both sides (a driver), its AI goes too, but not its states (in and out of cars are
    // replica.cpp's): a chasing driver followed its own objectives here
    const bool driving = p->field_16C_car && e.f[kPedCar] && !e.f[kPedState] && !InCarTransition(p) &&
                         RefCar(e.f[kPedCar]) == p->field_16C_car &&
                         p->field_278_ped_state_1 == ped_state_1::in_car_10 &&
                         p->field_16C_car->field_5C_AI && // its objectives drive through the car's AI (ApplyCarAi gives it one)
                         e.f[kStates + kState1] == ped_state_1::in_car_10 && // driving there too, not on its way out
                         e.f[kFirst + kObjective] != objectives_enum::objective_32; // an on-foot one
    if (StartCarTransition(p, e))
    {
        return true;
    }
    if (!driving && (p->field_16C_car || InCarTransition(p) || e.f[kPedCar] || e.f[kPedState]))
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
    ApplyMembers(p, kValues, kValueCount, e, kFirst);
    u32 flags;
    memcpy(&flags, &p->field_21C_bf, sizeof(flags));
    flags = (flags & ~kFlagsSent) | ((u32)e.f[kFlags] & kFlagsSent);
    memcpy(&p->field_21C_bf, &flags, sizeof(flags));
    p->field_216_health = (s16)(e.f[kHealth] > 0 ? e.f[kHealth] : 1); // a death comes from the server's state, whole
    if (!driving && Plain(e.f[kStates + kState1], e.f[kStates + kState2]) && Plain(p->field_278_ped_state_1, p->field_27C_ped_state_2) &&
        PlainStored(e.f[kStates + kStored1], e.f[kStates + kStored2]) && p->field_168_game_object)
    {
        p->field_278_ped_state_1 = e.f[kStates + kState1];
        p->field_27C_ped_state_2 = e.f[kStates + kState2];
        p->field_280_stored_ped_state_1 = e.f[kStates + kStored1];
        p->field_284_stored_ped_state_2 = e.f[kStates + kStored2];
        // ... and its char's movement and animation, as the server's (with only the ped's states, a gang member could
        // run on the spot: the char kept its own state and speed)
        Char* c = p->field_168_game_object;
        if (CharPlain(e.f[kChar + kCharState], e.f[kChar + kCharAnimation]) && CharPlain(c->field_10_char_state, c->field_6C_animation_state))
        {
            ApplyMembers(c, kCharValues, kCharCount, e, kChar);
        }
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
        if (i == kRefTargetToEnter && p->field_16C_car)
        {
            continue; // in a car: what it got into is its car, and leaving it reads this (Ped::ExitCarStateMachine_46C250)
        }
        *carMembers[i] = carRefs[i];
    }
    p->field_17C_pGang = GangAt(e.f[kGangs]);
    p->field_19C_dummy_gang = GangAt(e.f[kGangs + 1]);
    return true;
}
