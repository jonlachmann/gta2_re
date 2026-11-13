// port/net (port/docs/netcode.md, "Entities"): a car's AI (CarAI: how a traffic or ped driver drives, routes, yields, chases)
// as the server has it. Without it a client's car decided its own way through every resimulation and each snapshot
// pulled it back (a chasing driver lurched). The plain values go member by member; the two cars it may refer to go as
// net ids; the nearest entity it caches stays local (it looks again).
#include "internal.hpp"
#include "members.hpp"
#include "Source/Car.hpp"
#include "Source/CarAI.hpp"
#include <stddef.h>
#include <string.h>

namespace
{
using net::engine::Member;
#define AM(member, sz, sg) NET_MEMBER(CarAI, member, sz, sg)
const Member kValues[] = {
    AM(mManeuverActive, 1, true), AM(mProbeX, 1, true), AM(mProbeY, 1, true), AM(mAngle, 2, true),
    AM(mSpeed, 4, true), AM(mTargetSpeed, 4, true), AM(mAcceleration, 4, true), AM(mFlags, 4, false),
    AM(mRouteIdx, 1, true), AM(mStoppedTimer, 1, false), AM(mTicksSinceAlloc, 1, false),
    AM(mYieldTimer, 1, false), AM(mArrowStart, 1, true), AM(mArrowCur, 1, true), AM(mArrowEnd, 1, true),
    AM(mForcedStopTimer, 1, false), AM(mJunctionTurnDirection, 4, true), AM(mSeekingRoad, 4, true),
    AM(mPrevTargetDirection, 4, true), AM(mTargetDirection, 4, true), AM(mProbeDirection, 4, true),
    AM(mCurrDirection, 4, true), AM(mSeekRoadTurn, 4, true), AM(mAccelCooldown, 2, false),
    AM(mRoutePos, 2, true), AM(mOffRoadTimer, 2, false), AM(mObstacleTimer, 2, true), AM(mMaxSpeed, 4, true)};
#undef AM
const size_t kCount = sizeof(kValues) / sizeof(kValues[0]);
int gFirst = -1; // the car type's field of the first (set by CarAiFields)
} // namespace

void net::engine::CarAiFields(std::vector<FieldDesc>& out)
{
    gFirst = (int)out.size();
    out.push_back(FieldDesc{"ai", 1, false}); // it has one
    AppendFields(kValues, kCount, out);
    out.push_back(FieldDesc{"ai_collided_car", 32, false});
    out.push_back(FieldDesc{"ai_yield_to_car", 32, false});
}

void net::engine::CaptureCarAi(const Car* car, EntityState& e)
{
    const CarAI* a = car->mAI;
    if (!a || gFirst < 0)
    {
        return;
    }
    e.f[gFirst] = 1;
    CaptureMembers(a, kValues, kCount, e, gFirst + 1);
    e.f[gFirst + 1 + kCount] = a->mCarInCollision ? (int32_t)(kCarIdBase + a->mCarInCollision->mCarId) : 0;
    e.f[gFirst + 2 + kCount] = a->mYieldToCar ? (int32_t)(kCarIdBase + a->mYieldToCar->mCarId) : 0;
}

void net::engine::ApplyCarAi(Car* car, const EntityState& e)
{
    if (gFirst < 0 || !e.f[gFirst])
    {
        return; // none on the server (a player's car): one here drives its own
    }
    if (!car->mAI)
    {
        // The server's has one (a cop got in: Car::InitCarAIControl), so this one gets one too: its driver's
        // objective drives through it (Ped::SetupCarFollowTargetPed)
        car->mAI = gCarAI_78_Pool->Allocate();
        if (!car->mAI)
        {
            return;
        }
        car->mAI->SetCar(car);
    }
    CarAI* a = car->mAI;
    // Its references first: one this game doesn't have keeps the AI as it is here, whole
    Car* refs[2] = {NULL, NULL};
    for (int k = 0; k < 2; k++)
    {
        const uint32_t id = (uint32_t)e.f[gFirst + 1 + kCount + k];
        if (id)
        {
            refs[k] = ReplicaCar(id);
            if (!refs[k] || !refs[k]->mCarSprite)
            {
                return;
            }
        }
    }
    ApplyMembers(a, kValues, kCount, e, gFirst + 1);
    a->mCarInCollision = refs[0];
    a->mYieldToCar = refs[1];
}
