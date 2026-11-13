// port/net (port/docs/netcode.md, "Entities"): a ped's AI state as the server has it. A client runs every ped's AI in its
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
    PM(mFacingAngle, 2, true), PM(mAimAngle, 2, true), PM(mTargetFacingAngle, 2, true),
    PM(mFollowCarOffsetAngle, 2, true), PM(mRotation, 2, true), PM(mTargetX, 4, true),
    PM(mTargetY, 4, true), PM(mTargetZ, 4, true), PM(mMoveTargetX, 4, true),
    PM(mMoveTargetY, 4, true), PM(mMoveTargetZ, 4, true), PM(mInternalTargetX, 4, true),
    PM(mInternalTargetY, 4, true), PM(mInternalTargetZ, 4, true), PM(mObjectiveTargetX, 4, true),
    PM(mObjectiveTargetY, 4, true), PM(mObjectiveTargetZ, 4, true), PM(mTargetDistanceLimit, 4, true),
    PM(mMaxSpeed, 4, true), PM(mWalkSpeed, 4, true), PM(mRunSpeed, 4, true),
    PM(mFollowCarOffsetDistance, 4, true), PM(mInvulnerability, 2, false), PM(mWantedPoints, 2, true),
    PM(mTargetUnseenCounter, 2, false), PM(mShockCounter, 2, false), PM(mElectrocutionThreshold, 2, false),
    PM(mObjectiveTimer, 2, false), PM(mInternalObjectiveTimer, 2, false), PM(mLastVoiceRng, 4, true),
    PM(mBf224, 1, false), PM(mObjectiveStatus, 1, false), PM(mInternalObjectiveStatus, 1, true),
    PM(mHitCount, 1, true), PM(mMugCount, 1, false), PM(mPedClass, 4, true), PM(mJumpOverMode, 4, true),
    PM(mLifetimeTimer, 1, true), PM(mPedType, 4, true), PM(mOccupation, 4, true),
    PM(mEnterCarAsPassenger, 4, true), PM(mTargetCarDoor, 1, false), PM(mObjective, 4, true),
    PM(mInternalObjective, 4, true), PM(mWaitCounter, 1, true), PM(mAttackersCount, 1, false),
    PM(mPrevAttackersCount, 1, false), PM(mKillerIdTimer, 1, false), PM(mElectrocutionTimer, 1, true),
    PM(mRecentCrimeTimer, 1, false), PM(mGraphicType, 4, true), PM(mFireMode, 4, true),
    PM(mGangCarModel, 4, true), PM(mThreatSearch, 4, true), PM(mThreatReaction, 4, true),
    PM(mDeathCause, 4, true)};
#undef PM
const size_t kValueCount = sizeof(kValues) / sizeof(kValues[0]);

const int kObjective = net::engine::MemberIndex(kValues, kValueCount, offsetof(Ped, mObjective));
const int kPassenger = net::engine::MemberIndex(kValues, kValueCount, offsetof(Ped, mEnterCarAsPassenger));
const int kDoor = net::engine::MemberIndex(kValues, kValueCount, offsetof(Ped, mTargetCarDoor));

// The flags that are the ped's own behaviour; the others say where it is in this game's structures (active, scheduled for
// removal, in the pathfinder's list, on fire with a flame object of this game's)
const u32 kFlagsSent = ~((1u << 0) | (1u << 10) | (1u << 14) | (1u << 24));

// Its states (Ped::mPedState1 ..): set only when both sides have it on foot and alive; in, into and out of cars and
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
const Member kCharValues[] = {CM(mCharState, 4, true),      CM(mTargetRotation, 2, true),  CM(mVelocity, 4, true),
                              CM(mRunOrJumpSpeed, 4, true), CM(mVelocityVector.x, 4, true), CM(mVelocityVector.y, 4, true),
                              CM(mAnimationState, 4, true), CM(mAnimationFrame, 1, false), CM(mFrameTimer, 1, true),
                              CM(mFrameDelay, 1, true),
                              // its wander: the next idle behaviour's timer and the turn it takes (Char::SelectRandomIdleBehavior,
                              // ApplyRandomRotationJitter), its jumps, and the push off another ped (Char::UpdateFleeOrRunning)
                              CM(mTimer, 2, false), CM(mRotationJitter, 2, true), CM(mJumpScaleCounter, 1, true),
                              CM(mRepulsionTimer, 1, false), CM(mRepulsionRotation, 2, true)};
#undef CM
const size_t kCharCount = sizeof(kCharValues) / sizeof(kCharValues[0]);
const int kCharState = net::engine::MemberIndex(kCharValues, kCharCount, offsetof(Char, mCharState));
const int kCharAnimation = net::engine::MemberIndex(kCharValues, kCharCount, offsetof(Char, mAnimationState));
const int kGroup = kChar + (int)kCharCount; // its group: leader (net id), its index (99: the leader), type, counts
const int kWeapons = kGroup + 5; // its weapon and its second one: weapon_type + 1, 0 none
const int kEnd = kWeapons + 2;
static_assert(kEnd + 1 <= net::kMaxFields, "a ped's fields and its age (snapshot.cpp)");

int32_t WeaponRef(const Weapon* w) // weapon_type + 1, 0 none
{
    return w ? w->mWeaponType + 1 : 0;
}

uint32_t PedId(const Ped* p) // the server's side
{
    if (!p)
    {
        return 0;
    }
    if (p->mPlayer)
    {
        return 1 + p->mPlayer->mPlayerIdx; // a player entity
    }
    return net::engine::kPedIdBase + p->mId;
}

uint32_t CarId(const Car* c)
{
    return c ? net::engine::kCarIdBase + c->mCarId : 0;
}

int GangIdx(const Gang* g)
{
    const Gang* first = gGangPool_CA8 ? &gGangPool_CA8->mGangList[0] : NULL;
    return g && first && g >= first && g < first + 10 ? (int)(g - first) + 1 : 0;
}

Gang* GangAt(int idx)
{
    return idx >= 1 && idx <= 10 && gGangPool_CA8 ? &gGangPool_CA8->mGangList[idx - 1] : NULL;
}

Ped* const* PedRefs(const Ped* p, Ped** out) // in kRef order
{
    out[kRefAttacker] = p->mAttacker;
    out[kRefObjectivePed] = p->mObjectiveTargetPed;
    out[kRefInternalPed] = p->mInternalTargetPed;
    out[kRefCarThief] = p->mCarThief;
    out[kRefLastPunched] = p->mLastCharPunched;
    out[kRefHitTarget] = p->mHitTargetPed;
    out[kRefKiller] = p->mPedKiller;
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
    memcpy(&flags, &p->mBf21C, sizeof(flags));
    e.f[kFlags] = (int32_t)(flags & kFlagsSent);
    e.f[kHealth] = p->mHealth;
    e.f[kStates + kState1] = p->mPedState1;
    e.f[kStates + kState2] = p->mPedState2;
    e.f[kStates + kStored1] = p->mStoredPedState1;
    e.f[kStates + kStored2] = p->mStoredPedState2;
    Ped* refs[kPedRefCount];
    PedRefs(p, refs);
    for (int i = 0; i < kPedRefCount; i++)
    {
        e.f[kPedRefs + i] = (int32_t)PedId(refs[i]);
    }
    e.f[kCarRefs + kRefStolenCar] = (int32_t)CarId(p->mStolenCar);
    e.f[kCarRefs + kRefObjectiveCar] = (int32_t)CarId(p->mTargetObjectiveCar);
    e.f[kCarRefs + kRefTargetToEnter] = (int32_t)CarId(p->mTargetToEnter);
    e.f[kCarRefs + kRefEscapeCar] = (int32_t)CarId(p->mEscapeCar);
    e.f[kGangs] = GangIdx(p->mpGang);
    e.f[kGangs + 1] = GangIdx(p->mDummyGang);
    if (const Char* c = p->mGameObject)
    {
        CaptureMembers(c, kCharValues, kCharCount, e, kChar);
    }
    e.f[kWeapons] = WeaponRef(p->mSelectedWeapon);
    e.f[kWeapons + 1] = WeaponRef(p->mpWeapon);
    if (const PedGroup* g = p->mPedGroup)
    {
        e.f[kGroup] = (int32_t)PedId(g->mPedLeader);
        e.f[kGroup + 1] = p->mGroupIdx;
        e.f[kGroup + 2] = g->mGroupType;
        e.f[kGroup + 3] = g->mCount34;
        e.f[kGroup + 4] = (u8)g->mCount36;
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
    *pResolved = c && c->mCarSprite;
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
// that played them has (a ped pulled out of a car, 8/17, uses its char's car: Char::UpdateImmobilized), so they stay here
bool Plain(int32_t state1, int32_t state2)
{
    return (state1 == ped_state_1::walking_0 || state1 == ped_state_1::flee_or_running_1 || state1 == ped_state_1::standing_still_7) &&
           (state2 == ped_state_2::ped2_walking_0 || state2 == ped_state_2::ped2_staying_14);
}

// The stored states (Ped::ChangeNextPedState1's): plain ones, or none yet (11/28, as Ped's constructor sets them)
bool PlainStored(int32_t state1, int32_t state2)
{
    return Plain(state1, state2) || (state1 == 11 && state2 == ped_state_2::initial_28);
}

// Its weapons, the game's way of arming a ped (Ped::ForceWeapon drops the one it had): a gang member here had
// none and never showed the shots the server's fired
void ApplyWeapons(Ped* p, const net::EntityState& e)
{
    if (WeaponRef(p->mSelectedWeapon) != e.f[kWeapons])
    {
        if (e.f[kWeapons])
        {
            p->ForceWeapon(e.f[kWeapons] - 1);
        }
        else
        {
            p->RemovePedWeapons();
        }
    }
    if (WeaponRef(p->mpWeapon) != e.f[kWeapons + 1])
    {
        if (e.f[kWeapons + 1])
        {
            p->GiveWeapon(e.f[kWeapons + 1] - 1);
        }
        else
        {
            p->RemoveSecondaryWeapon();
        }
    }
}
} // namespace

void net::engine::ApplyPedGroups(const std::vector<std::pair<Ped*, const EntityState*>>& peds)
{
    // The members each leader has here, in the server's order: the game walks a group's list up to its count
    // (mCount34) without looking for gaps, so it is made dense with only the members that are here
    std::map<Ped*, std::vector<std::pair<int, Ped*>>> byLeader;
    std::set<Ped*> placed;
    for (size_t k = 0; k < peds.size(); k++)
    {
        Ped* p = peds[k].first;
        const EntityState& e = *peds[k].second;
        bool ok;
        Ped* leader = e.f[kGroup] ? LocalPed((uint32_t)e.f[kGroup], &ok) : NULL;
        const int idx = e.f[kGroup + 1];
        // In a car too: the game keeps a passenger in its group (an instant gang in its leader's car); left out, the
        // rebuild below took it out of the group here
        if (leader && leader != p && idx != 99 && !p->isDead() && !leader->isDead())
        {
            byLeader[leader].push_back(std::make_pair(idx, p));
            placed.insert(p);
        }
        else if (p->mPedGroup && p->mPedGroup->mPedLeader != p && !p->mCar)
        {
            p->mPedGroup->RemovePed(p); // in no group there (or its leader isn't here)
        }
    }
    for (auto& g : byLeader)
    {
        Ped* leader = g.first;
        std::vector<std::pair<int, Ped*>>& members = g.second;
        std::sort(members.begin(), members.end());
        PedGroup* group = leader->mPedGroup;
        if (!group)
        {
            group = PedGroup::New(); // as the game makes one (SpawnPedChainGroupAt)
            if (!group)
            {
                continue;
            }
            group->add_ped_leader(leader);
        }
        // Out of any other group first, then the list as it should be
        for (size_t m = 0; m < members.size(); m++)
        {
            PedGroup* other = members[m].second->mPedGroup;
            if (other && other != group && other->mPedLeader != members[m].second)
            {
                other->RemovePed(members[m].second);
            }
        }
        std::set<Ped*> now;
        for (size_t m = 0; m < members.size(); m++)
        {
            now.insert(members[m].second);
        }
        for (int m = 0; m < 9; m++)
        {
            Ped* was = group->mPedList[m];
            if (was && !now.count(was) && was->mPedGroup == group)
            {
                was->reset_ped_group(); // listed here before, not any more
            }
            group->mPedList[m] = NULL;
        }
        const size_t n = members.size() < 9 ? members.size() : 9;
        for (size_t m = 0; m < n; m++)
        {
            group->add_ped_to_list(members[m].second, (u8)m);
        }
        group->mCount34 = (u8)n;
        group->mCount36 = (char_type)n;
    }
    // A member whose leader isn't here is in no group here: its objective to follow the group goes too (the server's,
    // copied with the AI state; Ped::UpdateGroupMemberFollow assumes a group)
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext) // every ped: one taken out above too
    {
        if (!p->mPedGroup &&
            (p->mObjective == objectives_enum::left_behind_by_leader_8 || p->mInternalObjective == objectives_enum::left_behind_by_leader_8))
        {
            p->mObjective = objectives_enum::no_obj_0;
            p->mInternalObjective = objectives_enum::no_obj_0;
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
    if (p->mPlayer || !e.f[kPedState] || InCarTransition(p) || !p->mGameObject == !p->mCar)
    {
        return false; // a player's (players.cpp), the server's settled, ours on its way already, or neither on foot nor in a car
    }
    const int32_t objective = e.f[kFirst + kObjective];
    if (!p->mCar && !e.f[kPedCar] && objective == objectives_enum::enter_car_as_driver_35)
    {
        bool ok;
        Car* car = LocalCar((uint32_t)e.f[kCarRefs + kRefObjectiveCar], &ok);
        if (!car || !car->mCarSprite)
        {
            return false; // not here (yet)
        }
        StartEntering(p, car);
        p->set_enter_car_as_passenger((char_type)e.f[kFirst + kPassenger]);
        p->set_target_car_door((char_type)e.f[kFirst + kDoor]);
        return true;
    }
    const bool leaving = objective == objectives_enum::leave_car_36 || e.f[kStates + kState1] == ped_state_1::exiting_car_4 ||
                         e.f[kStates + kState2] == ped_state_2::ped2_getting_out_a_car_7; // as LeavingCar (car_entry.cpp)
    if (p->mCar && RefCar(e.f[kPedCar]) == p->mCar && leaving && p->mCar->CanExitCar())
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
    const bool deadHere = p->isDead();
    if (deadThere && !deadHere)
    {
        if (p->mCar && !p->mEnterCarAsPassenger)
        {
            ForceOutOfCar(p, Raw(e.f[kPedX]), Raw(e.f[kPedY]), Raw(e.f[kPedZ]), Angle(e.f[kPedRot]));
        }
        p->Kill(); // the server's death, the game's own way (as a player's, snapshot.cpp)
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
    const bool driving = p->mCar && e.f[kPedCar] && !e.f[kPedState] && !InCarTransition(p) &&
                         RefCar(e.f[kPedCar]) == p->mCar &&
                         p->mPedState1 == ped_state_1::in_car_10 &&
                         p->mCar->mAI && // its objectives drive through the car's AI (ApplyCarAi gives it one)
                         e.f[kStates + kState1] == ped_state_1::in_car_10 && // driving there too, not on its way out
                         e.f[kFirst + kObjective] != objectives_enum::pull_driver_out_32; // an on-foot one
    if (StartCarTransition(p, e))
    {
        return true;
    }
    if (!driving && (p->mCar || InCarTransition(p) || e.f[kPedCar] || e.f[kPedState]))
    {
        // In a car, or on its way into or out of one, on either side: that is replica.cpp's (ForceIntoCar, ForceOutOfCar
        // when both have settled). The AI's objectives and targets then belong to the entering and leaving: set from the
        // server's, the char's walk to a door read a car the client had no part in (Char::UpdateImmobilized)
        return true;
    }
    // The references first: one this game doesn't have (out of its area) keeps the ped as it is here for this snapshot,
    // whole. Its AI runs by its occupation as much as by its objective (a robbed driver reads its car thief:
    // Ped::RobbedDriver_AI), so a part of the server's state with a target missing isn't safe
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
    memcpy(&flags, &p->mBf21C, sizeof(flags));
    flags = (flags & ~kFlagsSent) | ((u32)e.f[kFlags] & kFlagsSent);
    memcpy(&p->mBf21C, &flags, sizeof(flags));
    p->mHealth = (s16)(e.f[kHealth] > 0 ? e.f[kHealth] : 1); // a death comes from the server's state, whole
    if (!driving && Plain(e.f[kStates + kState1], e.f[kStates + kState2]) && Plain(p->mPedState1, p->mPedState2) &&
        PlainStored(e.f[kStates + kStored1], e.f[kStates + kStored2]) && p->mGameObject)
    {
        p->mPedState1 = e.f[kStates + kState1];
        p->mPedState2 = e.f[kStates + kState2];
        p->mStoredPedState1 = e.f[kStates + kStored1];
        p->mStoredPedState2 = e.f[kStates + kStored2];
        // ... and its char's movement and animation, as the server's (with only the ped's states, a gang member could
        // run on the spot: the char kept its own state and speed)
        Char* c = p->mGameObject;
        if (CharPlain(e.f[kChar + kCharState], e.f[kChar + kCharAnimation]) && CharPlain(c->mCharState, c->mAnimationState))
        {
            ApplyMembers(c, kCharValues, kCharCount, e, kChar);
        }
    }
    Ped** pedMembers[kPedRefCount] = {&p->mAttacker,         &p->mObjectiveTargetPed, &p->mInternalTargetPed,
                                      &p->mCarThief,        &p->mLastCharPunched,    &p->mHitTargetPed,
                                      &p->mPedKiller};
    for (int i = 0; i < kPedRefCount; i++)
    {
        *pedMembers[i] = pedRefs[i];
    }
    Car** carMembers[kCarRefCount] = {&p->mStolenCar, &p->mTargetObjectiveCar, &p->mTargetToEnter,
                                      &p->mEscapeCar};
    for (int i = 0; i < kCarRefCount; i++)
    {
        if (i == kRefTargetToEnter && p->mCar)
        {
            continue; // in a car: what it got into is its car, and leaving it reads this (Ped::ExitCarStateMachine)
        }
        *carMembers[i] = carRefs[i];
    }
    p->mpGang = GangAt(e.f[kGangs]);
    p->mDummyGang = GangAt(e.f[kGangs + 1]);
    return true;
}

bool net::engine::PassengerThere(const EntityState& e)
{
    return e.f[kPedCar] && e.f[kFirst + kPassenger];
}
