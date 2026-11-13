// port/net (docs/netcode.md, "Entities"): a car as a snapshot entity, and the server's state
// put into this game's car: its place, its whole physics, its damage, lights, horn and colour.
#include "internal.hpp"
#include "dev/dev.hpp"
#include "members.hpp"
#include "Source/Car.hpp"
#include "Source/CarPhysics.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/explosion_type.hpp"
#include "Source/gtx_0x106C.hpp"
#include "Source/sprite.hpp"
#include <stdio.h>

namespace
{
using net::engine::Member;

// A car's whole physics state on the wire: every CarPhysics member but its two pointers and the unused padding, at
// its own size. Whatever acts on a car (input, collisions, slopes, oil, skids) leaves its mark here, so a predicted car
// goes on from exactly the server's.
#define PM(member, sz, sg) NET_MEMBER(CarPhysics, member, sz, sg)
#define PM_POINT(member) PM(member.x, 4, true), PM(member.y, 4, true)
const Member kPhysics[] = {
    PM_POINT(mVelReadOnly), PM(mDamagedUntilCycle, 4, false), PM_POINT(mLastSkidPos[0]),
    PM_POINT(mLastSkidPos[1]), PM_POINT(mLastSkidPos[2]), PM_POINT(mLastSkidPos[3]),
    PM_POINT(mCm1), PM_POINT(mCp1), PM_POINT(mLinvel1), PM_POINT(mForceAccum),
    PM_POINT(mLinearAccel), PM(mTheta, 2, true), PM(mGasPedal, 4, true),
    PM(mBrakePressure, 4, true), PM(mAirZVel, 4, true), PM(mCp3, 4, true), PM(mZVel, 4, true),
    PM(mAngVelRad, 4, true), PM(mPointingAngRad, 4, true), PM(mTorqueAccum, 4, true),
    PM(mAngularAccel, 4, true), PM(mFrontSkid, 4, true), PM(mRearSkid, 4, true), PM(mState, 4, true),
    PM(mTimerSinceLastMove, 1, false), PM(mIsFootBrakeOn, 1, true), PM(mIsHandBrakeOn, 1, true),
    PM(mIsForwardGasOn, 1, true), PM(mIsBackwardGasOn, 1, true), PM(mIsCoasting, 1, true),
    PM(mSurfaceType, 4, true), PM(mBlockSpec, 4, true), PM(mOilSpinDir, 4, true),
    PM(mOilSpinTimer, 1, true), PM(mCurrentSlopeLength, 1, false), PM(mCurrentSlopeLeftTiles, 1, true),
    PM(mCurrentTileZ, 1, false), PM(mHandBrakeForce, 1, true), PM(mCarModel, 1, true),
    PM(mSbw, 1, true), PM(mTpa, 1, true), PM(mDriveWheelsLockedQ, 1, false),
    PM(mTurnDirection, 1, true)};
#undef PM_POINT
#undef PM
const size_t kPhysicsCount = sizeof(kPhysics) / sizeof(kPhysics[0]);
// mCp1.x: 0 only for a car without physics (nobody is at x 0)
const int kCp1X = net::engine::MemberIndex(kPhysics, kPhysicsCount, offsetof(CarPhysics, mCp1.x));
static_assert(net::engine::kCarPhysics + sizeof(kPhysics) / sizeof(kPhysics[0]) <= net::kMaxFields, "a car's fields");

} // namespace

bool net::engine::CarHasPhysics(const EntityState& e)
{
    return e.f[kCarPhysics + kCp1X] != 0;
}

int net::engine::gExplosionsCarried, net::engine::gWrecksReplaced;

void net::engine::CarPhysicsFields(std::vector<FieldDesc>& out)
{
    AppendFields(kPhysics, kPhysicsCount, out);
}

void net::engine::CaptureCar(Car* c, Snapshot& out)
{
    EntityState e = Entity(kCarIdBase + (uint32_t)c->mCarId, kEntityCar);
    PutPosition(e, c->mCarSprite);
    e.f[kCarModel] = c->mCarInfoIdx;
    e.f[kCarDriver] = !c->mDriver ? kDriverNone : IsSpareDriver(c->mDriver) ? kDriverTraffic : kDriverPed;
    e.f[kCarDamage] = c->mDamage;
    e.f[kCarAreas] = (int32_t)c->mDamagedAreas.m_var;
    e.f[kCarWrecked] = IsWrecked(c);
    e.f[kCarLights] = (u8)c->mLightFlags;
    e.f[kCarFlash] = (u8)c->mFlashPhaseCounter;
    e.f[kCarHorn] = c->mHorn;
    e.f[kCarHornCooldown] = c->mHornCooldown;
    // Its colour: a remap, or the model's own palette (-1: Car::AssignRandomRemap picks it as one choice, and it
    // leaves the remap as it was, so the remap alone said nothing then)
    e.f[kCarRemap] = c->mCarSprite->mPaletteType == palette_types_enum::car_remaps_3 ? c->mCarSprite->get_remap() : -1;
    CaptureCarAi(c, e);
    if (const CarPhysics* m = c->mPhysics)
    {
        CaptureMembers(m, kPhysics, kPhysicsCount, e, kCarPhysics);
    }
    out.entities.push_back(e);
}

// A car or a ped moved and turned at once. The collision grid keeps a sprite in the rows of its collision rect as it
// was added (SpriteGrid::RemoveFromRegionBuckets takes it out of the rows of the stored rect, and
// AddToRegionBuckets computes a new one): so position and rotation both change between the two. Turning a
// sprite while it is in the grid, as Car::SetPosition followed by a turn did, left it in rows its next
// removal missed; once it was freed and reused (a roof light, a particle), a collision query crashed on it.
// Only a car in the collision grid is moved: one that is going away (despawning_5, despawned_6: its last update
// left it out of the grid, and freeing it doesn't look there) or carried by something (deactivated_7: in grid 3,
// Car::Deactivate, its carrier moves it) would be put into the grid by the move and left there when freed.
void net::engine::MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    const s32 status = car->mDespawnStatus;
    if (status == car_despawn_status::despawning_5 || status == car_despawn_status::despawned_6 || status == car_despawn_status::deactivated_7 ||
        !OnMap(x, y, kCarIdBase + (uint32_t)car->mCarId))
    {
        return;
    }
    Sprite* s = car->mCarSprite;
    gSpriteGrid_1->RemoveFromRegionBuckets(s);
    s->set_xyz_lazy(x, y, z);
    s->set_ang_lazy(rot);
    if (CarPhysics* m = car->mPhysics)
    {
        const Fix16 steer = m->mPointingAngRad; // SetSprite zeroes the steering
        m->SetSprite(s);
        m->mPointingAngRad = steer;
    }
    gSpriteGrid_1->AddToRegionBuckets(s);
}

void net::engine::SetCarPhysics(Car* car, const EntityState& e)
{
    if (!CarHasPhysics(e))
    {
        return; // asleep on the server: this game's goes on (or stays asleep)
    }
    // Awake there, asleep here (a car without a driver here, settled, gave its physics back: Car::ServicePhysics):
    // woken as the game wakes a car that is hit (Car::SetupCarPhysicsAndSpriteBindingWithTrailer), else it isn't
    // simulated between snapshots, only moved by each one (the comparison check: a third of the cars)
    if (!car->mPhysics)
    {
        car->SetupCarPhysicsAndSpriteBindingWithTrailer();
    }
    if (CarPhysics* m = car->mPhysics)
    {
        ApplyMembers(m, kPhysics, kPhysicsCount, e, kCarPhysics);
    }
}

bool net::engine::IsWrecked(const Car* car)
{
    return car->mWreckCountdown || car->mDamage >= 32000 || car->mEngineStatus == car_engine_status::destroyed_6;
}

bool net::engine::SetCarState(Car* car, const EntityState& e)
{
    const bool there = e.f[kCarWrecked] != 0, here = IsWrecked(car);
    if (there && !here)
    {
        car->HandleCarExplosion(explosion_type::item_19); // as a car blows up from its damage (Car::AddDamage)
        gExplosionsCarried++;
        return true;
    }
    if (!there && here)
    {
        return false; // a wreck of this game's own (a collision predicted harder than the server's)
    }
    if (!there)
    {
        car->mDamage = (s16)e.f[kCarDamage];
        car->mDamagedAreas.m_var = (u32)e.f[kCarAreas];
    }
    // Its lights and horn: a siren switched with the horn key (Car::HandleSpecialInput counts the horn down) came
    // on again here after the server had it off
    car->mLightFlags = (char_type)e.f[kCarLights];
    car->mFlashPhaseCounter = (char_type)e.f[kCarFlash];
    car->mHorn = (u8)e.f[kCarHorn];
    car->mHornCooldown = (u8)e.f[kCarHornCooldown];
    if (e.f[kCarRemap] >= 0 && e.f[kCarRemap] < 256)
    {
        if (car->mCarSprite->mPaletteType != palette_types_enum::car_remaps_3 &&
            gGtx_0x106C->get_car_info_5AA3B0(car->mCarInfoIdx)->has_remaps())
        {
            car->mCarSprite->SetRemap((s16)e.f[kCarRemap]); // on its own palette here: SetCarRemap only compares remaps
        }
        car->SetCarRemap((u8)e.f[kCarRemap]); // only a model with remaps, only when it differs (and its trailer)
    }
    else if (e.f[kCarRemap] < 0)
    {
        // The model's own palette there: a car made or adopted here kept the remap it was given (shown in another colour)
        car->mCarSprite->SetPaletteSprites();
        if (car->mpTrailer && car->mpTrailer->mTrailerCar)
        {
            car->mpTrailer->mTrailerCar->mCarSprite->SetPaletteSprites();
        }
    }
    return true;
}

void net::engine::DebugCarDiff(Car* car, const EntityState& e, uint32_t tick)
{
    const CarPhysics* m = car ? car->mPhysics : NULL;
    if (!Debug().carDiff || !m || !CarHasPhysics(e))
    {
        return;
    }
    char line[2048];
    int n = 0;
    for (size_t i = 0; i < kPhysicsCount && n < (int)sizeof(line) - 80; i++)
    {
        const int32_t here = GetMember(m, kPhysics[i]), there = e.f[kCarPhysics + i];
        if (here != there)
        {
            n += snprintf(line + n, sizeof(line) - n, " %s %d/%d", kPhysics[i].name, here, there);
        }
    }
    if (n)
    {
        printf("net: tick %u, our car predicted (here/server):%s\n", tick, line);
    }
}
