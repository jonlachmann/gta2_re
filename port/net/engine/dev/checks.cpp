// port/net engine dev (docs/netcode.md, "Checks"): the engine's checks and logs, each off unless its GTA2_NET_* switch is
// set: the collision grid and the players' cars, the crash state, the self, animation, sound and gang logs, the server
// marker. Nothing here changes the game.
#include "../internal.hpp"
#include "dev.hpp"
#include "../world.hpp"
#include <chrono>
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
#include "Source/Draw.hpp"
#include "Source/Frontend.hpp"
#include "Source/gtx_0x106C.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Game.hpp"
#include "Source/ObjectDefinitions.hpp"
#include "Source/Object_5C.hpp"
#include "Source/ObjectPool.hpp"
#include "Source/Ped.hpp"
#include "Source/Player.hpp"
#include "Source/SoundObject.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/Weapon.hpp"
#include "Source/char.hpp"
#include "Source/collide.hpp"
#include "Source/gbh_graphics.hpp"
#include "Source/sound_obj.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#include <execinfo.h>
#endif
#include "port/src/net_hooks.hpp"
#include "port/src/rand/owner.hpp"
#include "port/src/rand/state.hpp"
#include "Source/rng.hpp"
#include "Source/PedGroup.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include <string.h>
#include <string>
#include <unordered_map>

const net::engine::DebugFlags& net::engine::Debug()
{
    static const DebugFlags sFlags = {getenv("GTA2_NET_CARDIFF") != NULL, getenv("GTA2_NET_SELFLOG") != NULL, getenv("GTA2_NET_ANIMLOG") != NULL,
                                      getenv("GTA2_NET_SOUNDLOG") != NULL, getenv("GTA2_NET_CHECKGRID") != NULL, getenv("GTA2_NET_PICKUPLOG") != NULL};
    return sFlags;
}

namespace
{
using namespace net::engine;
using net::EntityState;
using net::Snapshot;

uint32_t gDebugFrame;

// This game's live cars and peds (a mapped one may have been removed by the game since)
void Live(std::set<Car*>& cars, std::set<Ped*>& peds)
{
    for (Car* c = gCar_BC_Pool->mPool.mpPrev; c; c = c->mpNext)
    {
        cars.insert(c);
    }
    for (Ped* p = gPedPool->mPool.mpPrev; p; p = p->mpNext)
    {
        peds.insert(p);
    }
}

uint32_t MappedId(const Car* car) // its net id, 0 if no snapshot car is mapped to it
{
    for (auto& c : ReplicaCars())
    {
        if (c.second == car)
        {
            return c.first;
        }
    }
    return 0;
}

// Every sprite linked into a collision grid still has its collision shape (a freed sprite left in the grid crashes the next
// collision query: SpriteGrid::FindCollidingSpriteInRow)
struct LinkWas
{
    int type;
    const void* owner;
    int carModel; // for a car owner
    int carStatus;
    uint32_t frame;
    // for a char owner: its ped as it was (id, states, objective, the snapshot ped it is (0: none), player)
    int pedId = -1, state1 = -1, state2 = -1, objective = -1, player = -1;
    uint32_t netId = 0;
};
std::unordered_map<const void*, LinkWas> gLinkWas; // every linked sprite as the last good pass saw it

// What is wrong with a grid link's sprite (NULL: nothing)
const char* WhatsWrong(Sprite* sp, int type, const std::set<Car*>& cars, const std::set<Char*>& chars)
{
    if (!sp)
    {
        return "no sprite";
    }
    if (type == sprite_types_enum::car_2)
    {
        Car* car = sp->mCarBcPtr;
        return !cars.count(car) ? "a car that is gone" : car->mCarSprite != sp ? "not its car's sprite" : NULL;
    }
    if (type == sprite_types_enum::ped_3)
    {
        Char* ch = sp->mCharB4Ptr;
        return !chars.count(ch) ? "a char that is gone" : ch->mSpritePtr != sp ? "not its char's sprite" : NULL;
    }
    if (type == sprite_types_enum::object_1 || type == sprite_types_enum::code_obj1_4 || type == sprite_types_enum::map_obj_5)
    {
        Object* o = sp->mObject2CPtr;
        return !o                       ? "no object"
             : o->mSprite != sp ? "not its object's sprite"
             : o->mDefinition && o->mDefinition->mCollisionBucketCategory != collision_bucket_category::sprite_grid_1_region_4
                 ? "an object that doesn't go in this grid (a link left from an earlier owner)"
                 : NULL;
    }
    return NULL;
}

void ReportLink(const char* where, int cellX, int y, Sprite* sp, int type, const char* wrong, const std::set<Ped*>& peds)
{
    printf("net: CHECK frame %u %s: grid 1 cell (%d, %d) links sprite %p (type %d): %s\n", gDebugFrame, where, cellX, y, (void*)sp, type, wrong);
    if (type == sprite_types_enum::ped_3 && sp && sp->mCharB4Ptr)
    {
        const Char* ch = sp->mCharB4Ptr;
        bool freed = false;
        for (Char* f = gChar_B4_Pool->mPool.mpHead; f && !freed; f = f->mpNext)
        {
            freed = f == ch;
        }
        Ped* owner = ch->mpPed;
        const bool live = owner && peds.count(owner);
        printf("net: CHECK   the char is %s, points to ped %p (in the pool %d, id %d, its char %p, states %d/%d)\n", freed ? "freed" : "allocated",
               (void*)owner, (int)live, live ? (int)owner->mId : -1, live ? (void*)owner->mGameObject : NULL,
               live ? (int)owner->mPedState1 : -1, live ? (int)owner->mPedState2 : -1);
    }
    auto was = gLinkWas.find(sp);
    if (was != gLinkWas.end())
    {
        printf("net: CHECK   frame %u it was linked as type %d, owner %p (car model %d, despawn status %d; ped %d, states %d/%d, "
               "objective %d, snapshot ped %u, player %d)\n",
               was->second.frame, was->second.type, was->second.owner, was->second.carModel, was->second.carStatus, was->second.pedId,
               was->second.state1, was->second.state2, was->second.objective, was->second.netId, was->second.player);
    }
}

LinkWas Record(Sprite* sp, int type, const std::unordered_map<const Char*, Ped*>& charPed, const std::unordered_map<const Ped*, uint32_t>& netIds)
{
    LinkWas w = {type, type == sprite_types_enum::car_2 ? (const void*)sp->mCarBcPtr : (const void*)sp->mObject2CPtr,
                 type == sprite_types_enum::car_2 ? (int)sp->mCarBcPtr->mCarInfoIdx : -1,
                 type == sprite_types_enum::car_2 ? (int)sp->mCarBcPtr->mDespawnStatus : -1, gDebugFrame};
    if (type == sprite_types_enum::ped_3)
    {
        auto cp = charPed.find(sp->mCharB4Ptr);
        if (cp != charPed.end())
        {
            Ped* p = cp->second;
            w.pedId = (int)p->mId;
            w.state1 = (int)p->mPedState1;
            w.state2 = (int)p->mPedState2;
            w.objective = (int)p->get_objective();
            w.player = p->mPlayer ? (int)p->mPlayer->mPlayerIdx : -1;
            auto n = netIds.find(p);
            w.netId = n != netIds.end() ? n->second : 0;
        }
    }
    return w;
}

// Every link in the collision grid belongs to a live owner whose sprite it is: a car in the car pool, a ped's char, an
// object (particles may be in the grid too and aren't checked). A freed sprite's collision shape is the pool's free list
// (the same field), so its shape tells nothing.
bool GridsSound(const char* where)
{
    static int sReported;
    std::set<Car*> cars;
    std::set<Ped*> peds;
    Live(cars, peds);
    std::set<Char*> chars;
    std::unordered_map<const Char*, Ped*> charPed;
    for (Ped* p : peds)
    {
        if (p->mGameObject)
        {
            chars.insert(p->mGameObject);
            charPed[p->mGameObject] = p;
        }
    }
    std::unordered_map<const Ped*, uint32_t> netIds;
    for (auto& q : ReplicaPeds())
    {
        netIds[q.second] = q.first;
    }
    std::unordered_map<const void*, LinkWas> now;
    GridCell* const* rows = reinterpret_cast<GridCell* const*>(gSpriteGrid_1); // mRows, private, first
    for (int y = 0; rows && y < 256; y++)
    {
        for (GridCell* c = rows[y]; c; c = c->mpNext)
        {
            for (GridSpriteLink* l = c->mpSpriteLinks; l; l = l->mpNext)
            {
                Sprite* sp = l->mpSprite;
                const int type = sp ? sp->GetType() : -1;
                if (const char* wrong = WhatsWrong(sp, type, cars, chars))
                {
                    if (sReported++ < 4)
                    {
                        ReportLink(where, (int)c->mX, y, sp, type, wrong, peds);
                    }
                    return false; // the last good pass stays what gLinkWas has
                }
                now[sp] = Record(sp, type, charPed, netIds);
            }
        }
    }
    gLinkWas.swap(now);
    return true;
}

bool gMarker;
Fix16 gMarkerX, gMarkerY, gMarkerZ;
} // namespace

bool net::engine::DebugGridOk(const char* where, Car* car)
{
    if (Debug().checkGrid && !GridsSound(where))
    {
        if (car)
        {
            printf("net: CHECK %s: car %p model %d, kind %d, driver %p, despawn status %d\n", where, (void*)car, (int)car->mCarInfoIdx,
                   (int)car->mCarKind, (void*)car->mDriver, (int)car->mDespawnStatus);
        }
        return false;
    }
    return true;
}

bool net::engine::DebugCheckPlayerCars(const char* where)
{
    static int sReported;
    if (Debug().checkGrid)
    {
        GridsSound(where);
    }
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        Car* car = ped ? ped->mCar : NULL;
        if (car && (!liveCars.count(car) || !car->mCarSprite))
        {
            if (sReported++ < 5)
            {
                printf("net: CHECK %s: player %d's ped is in a car that is gone (state %d/%d, despawn status %d)\n", where, i,
                       (int)ped->mPedState1, (int)ped->mPedState2, liveCars.count(car) ? (int)car->mDespawnStatus : -1);
            }
            return false;
        }
        // Driving a car that doesn't have it as its driver (Car::ClearDriver reads the driver when it gets out)
        if (car && ped->mPedState2 == ped_state_2::ped2_driving_10 && !ped->mEnterCarAsPassenger && car->mDriver != ped)
        {
            static int sDriver;
            if (sDriver++ < 10)
            {
                Ped* d = car->mDriver;
                printf("net: CHECK %s: player %d drives car %d, which has driver %p (spare %d, player %d), mapped %d\n", where, i,
                       (int)car->mCarId, (void*)d, (int)IsSpareDriver(d), d && d->mPlayer ? (int)d->mPlayer->mPlayerIdx : -1,
                       (int)(MappedId(car) != 0));
            }
            return false;
        }
    }
    return true;
}

// The crash report's line for a ped and for a car: what they are, and whether what they point to is live
static void CrashPed(const char* what, const Ped* q, const std::set<Car*>& liveCars, const std::set<Ped*>& livePeds)
{
    if (!q)
    {
        return;
    }
    const bool live = livePeds.count(const_cast<Ped*>(q)) != 0;
    const Char* c = live ? q->mGameObject : NULL;
    const Sprite* sp = c ? c->mSpritePtr : NULL;
    fprintf(stderr, "net: crash state: %s ped %p live %d id %d states %d/%d objective %d player %p car %p (live %d) objective car %p (live %d) to enter %p (live %d) char %p state %d velocity %.3f at (%.2f, %.2f, %.2f)\n",
            what, (const void*)q, live, live ? (int)q->mId : -1, live ? (int)q->mPedState1 : -1, live ? (int)q->mPedState2 : -1,
            live ? (int)q->mObjective : -1, live ? (void*)q->mPlayer : NULL, live ? (void*)q->mCar : NULL,
            live ? (int)liveCars.count(q->mCar) : -1, live ? (void*)q->mTargetObjectiveCar : NULL,
            live ? (int)liveCars.count(q->mTargetObjectiveCar) : -1, live ? (void*)q->mTargetToEnter : NULL,
            live ? (int)liveCars.count(q->mTargetToEnter) : -1, (const void*)c, c ? (int)c->mCharState : -1,
            c ? c->mVelocity.ToFloat() : 0.f, sp ? sp->mXy.x.ToFloat() : 0.f, sp ? sp->mXy.y.ToFloat() : 0.f,
            sp ? sp->mZpos.ToFloat() : 0.f);
}

static void CrashCar(const char* what, const Car* car, const std::set<Car*>& liveCars, const std::set<Ped*>& livePeds)
{
    if (!car)
    {
        return;
    }
    const bool live = liveCars.count(const_cast<Car*>(car)) != 0;
    const Sprite* sp = live ? car->mCarSprite : NULL;
    fprintf(stderr, "net: crash state: %s car %p live %d id %d model %d despawn status %d driver %p (live %d) at (%.2f, %.2f, %.2f)\n", what,
            (const void*)car, live, live ? (int)car->mCarId : -1, live ? (int)car->mCarInfoIdx : -1,
            live ? (int)car->mDespawnStatus : -1, live ? (void*)car->mDriver : NULL,
            live ? (int)livePeds.count(car->mDriver) : -1, sp ? sp->mXy.x.ToFloat() : 0.f, sp ? sp->mXy.y.ToFloat() : 0.f,
            sp ? sp->mZpos.ToFloat() : 0.f);
}

void net::engine::DebugOnCrash()
{
    Game* g = gGame_0x40;
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    if (gPedPool && gCar_BC_Pool)
    {
        Live(liveCars, livePeds);
    }
    fprintf(stderr, "net: crash state: frame %u, tick %u\n", gDebugFrame, gpRng ? (uint32_t)gpRng->get_cur_rng() : 0u);
    if (gCopyingBlock)
    {
        fprintf(stderr, "net: crash state: saving or restoring the block %s at %p, %zu bytes\n", gCopyingBlock->name, (void*)gCopyingBlock->addr,
                gCopyingBlock->size);
    }
    CrashPed("the last updated", gPortLastPed, liveCars, livePeds);
    CrashCar("the last updated", gPortLastCar, liveCars, livePeds);
    if (!g || !g->mLocalPlayer)
    {
        return;
    }
    Player* p = g->mLocalPlayer;
    Ped* ped = p->mPlayerPed;
    CrashPed("the local player's", ped, liveCars, livePeds);
    Ped* camPed = p->mGameCamera.mTargetPed;
    Car* camCar = p->mGameCamera.mTargetCar;
    fprintf(stderr, "net: crash state: local player %d in use %d, ped %p (live %d) car %p, camera ped %p (live %d, the player's %d) car %p (live %d)\n",
            (int)p->mPlayerIdx, (int)p->mInUse, (void*)ped, (int)livePeds.count(ped), ped ? (void*)ped->mCar : NULL,
            (void*)camPed, (int)livePeds.count(camPed), camPed == ped, (void*)camCar, (int)liveCars.count(camCar));
    // The sound listener's camera (sound_obj::InterrogateAudioEntities reads its followed ped's car)
    Camera* listener = NULL;
    if (gSound_obj.mType5Idx)
    {
        SoundObject* so = gSound_obj.mAudioEntities[gSound_obj.mType5Idx].mpObj;
        listener = so ? so->mpAny.pCamera_0xBC : NULL;
    }
    Camera* cams[3] = {&p->mGameCamera, &p->mAuxGameCamera, &p->mViewCamera};
    for (int k = 0; k < 3; k++)
    {
        Ped* cp = cams[k]->mTargetPed;
        fprintf(stderr, "net: crash state: camera %d%s follows ped %p (live %d, in car %p live %d)\n", k, cams[k] == listener ? " (the listener)" : "",
                (void*)cp, (int)livePeds.count(cp), cp && livePeds.count(cp) ? (void*)cp->mCar : NULL,
                cp && livePeds.count(cp) ? (int)liveCars.count(cp->mCar) : -1);
    }
    if (listener && listener != cams[0] && listener != cams[1] && listener != cams[2])
    {
        Ped* cp = listener->mTargetPed;
        fprintf(stderr, "net: crash state: the listener is another camera %p, follows ped %p (live %d, aux of the player %d)\n", (void*)listener,
                (void*)cp, (int)livePeds.count(cp), cp == p->mAuxPed);
    }
    Car* cars[2] = {ped ? ped->mCar : NULL, camPed ? camPed->mCar : NULL};
    for (int k = 0; k < 2; k++)
    {
        if (cars[k])
        {
            fprintf(stderr, "net: crash state: car %p live %d sprite %p despawn status %d driver %p mapped %d\n", (void*)cars[k],
                    (int)liveCars.count(cars[k]), (void*)cars[k]->mCarSprite, (int)cars[k]->mDespawnStatus,
                    (void*)cars[k]->mDriver, (int)(MappedId(cars[k]) != 0));
        }
    }
}

void net::engine::DebugSetFrame(uint32_t frame)
{
    gDebugFrame = frame;
}

void net::engine::DebugOwnCarDiff(const Snapshot& snap)
{
    Ped* ped = GetPlayer(LocalPlayer()) ? GetPlayer(LocalPlayer())->mPlayerPed : NULL;
    Car* car = ped ? ped->mCar : NULL;
    if (!car)
    {
        return;
    }
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityCar && ReplicaCar(e.id) == car)
        {
            DebugCarDiff(car, e, snap.tick);
        }
    }
}

std::string net::engine::DebugPlayerCar(int player, const Snapshot& snap)
{
    Player* p = GetPlayer(player);
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (!ped)
    {
        return "";
    }
    char line[256];
    int n = snprintf(line, sizeof(line), "here: states %d/%d objective %d door %d car %p%s%s", (int)ped->mPedState1,
                     (int)ped->mPedState2, (int)ped->get_objective(), (int)ped->mTargetCarDoor, (void*)ped->mCar,
                     InCarTransition(ped) ? " (transition)" : "", ped->mTargetObjectiveCar ? " (target car)" : "");
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityPlayer && e.id == (uint32_t)player + 1)
        {
            float lx = 0, ly = 0;
            PlayerPosition(LocalPlayer(), &lx, &ly);
            const float dx = e.f[kPlayerX] / 16384.0f - lx, dy = e.f[kPlayerY] / 16384.0f - ly;
            snprintf(line + n, sizeof(line) - n, "; tick %u there: car %d (%p here) state %d target %d at (%.2f, %.2f), %.1f blocks from us", snap.tick,
                     e.f[kPlayerCar], (void*)RefCar(e.f[kPlayerCar]), e.f[kPlayerState], e.f[kPlayerTarget], e.f[kPlayerX] / 16384.0f,
                     e.f[kPlayerY] / 16384.0f, sqrtf(dx * dx + dy * dy));
        }
    }
    return line;
}

void net::engine::DebugSelfLog(uint32_t frame)
{
    static uint32_t sUntil;
    Player* p = Debug().selfLog ? GetPlayer(LocalPlayer()) : NULL;
    Ped* ped = p ? p->mPlayerPed : NULL;
    if (!ped)
    {
        return;
    }
    if (p->mWastedOrBusted || ped->isDead())
    {
        sUntil = frame + 30;
    }
    if (frame > sUntil)
    {
        return;
    }
    Sprite* s = PedSprite(ped);
    printf("net: frame %u, self: at (%.2f, %.2f) states %d/%d dead %d wasted %d countdown %d aux %p camera mode %d health %d char %p car %p "
           "objective %d\n",
           frame, s ? s->mXy.x.ToFloat() : -1.0f, s ? s->mXy.y.ToFloat() : -1.0f, (int)ped->mPedState1,
           (int)ped->mPedState2, (int)ped->isDead(), (int)p->mWastedOrBusted, (int)p->mDeathCountdown,
           (void*)p->mAuxPed, (int)p->mCameraMode, (int)ped->mHealth, (void*)ped->mGameObject,
           (void*)ped->mCar, (int)ped->get_objective());
}

void net::engine::DebugAnimLog(uint32_t frame)
{
    if (!Debug().animLog || frame % 3)
    {
        return;
    }
    std::string line;
    char buf[160];
    for (int i = 0; i < PlayerCount(); i++)
    {
        Ped* ped = InGamePed(i);
        Char* c = ped ? ped->mGameObject : NULL;
        if (c && i != LocalPlayer())
        {
            snprintf(buf, sizeof(buf), " p%d[in 0x%x ped %d/%d char %d anim %d frame %d timer %d]", i, (unsigned)GetPlayer(i)->mInputs,
                     (int)ped->mPedState1, (int)ped->mPedState2, (int)c->mCharState, (int)c->mAnimationState,
                     (int)c->mAnimationFrame, (int)c->mFrameTimer);
            line += buf;
        }
    }
    printf("net: frame %u, anim:%s\n", frame, line.c_str());
}

void net::engine::DebugSoundChannels(uint32_t frame)
{
    static std::string sLast;
    if (!Debug().soundLog)
    {
        return;
    }
    std::string line;
    char buf[96];
    for (int i = 0; i < 16; i++)
    {
        const sound_0x68& a = gSound_obj.mActiveSamples[i];
        if (a.mbIsBeingPlayed)
        {
            snprintf(buf, sizeof(buf), " ch%d:ent%d/samp%d/loops%u/type%d/rel%u/ftp%d", i, a.mEntityIndex, a.mSampIdx, a.mLoopCount,
                     a.mType, a.mReleasingVolumeDivider, a.mFramesToPlay);
            line += buf;
            const int ent = a.mEntityIndex;
            const AudioEntity* ae = ent >= 0 && ent < 1020 ? &gSound_obj.mAudioEntities[ent] : NULL;
            if (ae && ae->mbUsed && ae->mpObj && ae->mpObj->mObjectType == 7)
            {
                const Weapon* w = ae->mpObj->mpAny.pWeapon_30;
                const Ped* ped = w ? w->mpPed : NULL;
                snprintf(buf, sizeof(buf), "[weapon %d ammo %d reload %d shot %d ped %d player %d]", w ? w->mWeaponType : -1, w ? w->mAmmo : -1,
                         w ? w->mReloadTimer : -1, w ? w->mShotFired : -1, ped ? ped->mId : -1,
                         ped && ped->mPlayer ? ped->mPlayer->mPlayerIdx : -1);
                line += buf;
            }
        }
    }
    if (line != sLast)
    {
        printf("net: frame %u, sound channels:%s (init %d)\n", frame, line.c_str(), gSound_obj.mbSoundInitialized);
        sLast = line;
    }
}

void net::engine::SetServerMarker(int32_t x, int32_t y, int32_t z)
{
    gMarker = true;
    gMarkerX = Raw(x);
    gMarkerY = Raw(y);
    gMarkerZ = Raw(z);
}

// GTA2_NET_MARKER: the square where the newest snapshot has the local player, over the frame (DrawOverlay)
void net::engine::DebugDrawMarker(Camera* cam)
{
    if (!gMarker)
    {
        return;
    }
    Fix16 sx, sy;
    cam->ProjectWorldToScreen_4B90E0(gMarkerX, gMarkerY, gMarkerZ, &sx, &sy);
    const float x = sx.ToFloat(), y = sy.ToFloat(), r = 4.0f;
    Vert v[4] = {};
    v[0].x = x - r, v[0].y = y - r;
    v[1].x = x + r, v[1].y = y - r;
    v[2].x = x + r, v[2].y = y + r;
    v[3].x = x - r, v[3].y = y + r;
    pgbh_DrawFlatRect(v, (s32)0xFFFF00FF); // magenta: where the server has us (a snapshot's tick, before the prediction)
}

// GTA2_NET_GANGLOG=1: player 0's group (an instant gang) every 15 game ticks, by the server's ids, on the server and on
// a client alike, to put the two side by side by tick
void net::engine::GangLog(bool server)
{
    static const bool on = getenv("GTA2_NET_GANGLOG") != NULL;
    Ped* leader = on ? InGamePed(0) : NULL;
    PedGroup* g = leader ? leader->mPedGroup : NULL;
    const uint32_t tick = (uint32_t)gpRng->get_cur_rng();
    if (!g || tick % 15)
    {
        return;
    }
    std::unordered_map<const Ped*, uint32_t> ids;
    for (const auto& r : ReplicaPeds())
    {
        ids[r.second] = r.first;
    }
    for (int i = -1; i < (int)g->mCount34 && i < 9; i++)
    {
        Ped* p = i < 0 ? leader : g->mPedList[i];
        if (!p)
        {
            continue;
        }
        const uint32_t id = server ? kPedIdBase + p->mId : (ids.count(p) ? ids[p] : 0);
        Car* car = p->mCar;
        printf("net: GANG %s tick %u %s %u: states %d/%d objective %d door %d car %s model %d at (%.2f, %.2f) rot %d\n", server ? "server" : "client",
               tick, i < 0 ? "leader" : "member", id, (int)p->mPedState1, (int)p->mPedState2, (int)p->mObjective,
               (int)p->mTargetCarDoor, car ? (car == leader->mCar ? "leader's" : "other") : "none", car ? (int)car->mCarInfoIdx : -1, p->get_cam_x().ToFloat(),
               p->get_cam_y().ToFloat(), (int)(PedSprite(p) ? PedSprite(p)->mRotation.rValue : -1));
    }
}


namespace
{
const char* Who(Car* c)
{
    Ped* d = c->mDriver;
    return !d ? "nobody" : !d->mPlayer ? "a ped" : InGamePed(LocalPlayer()) == d ? "the local player" : "a player";
}
} // namespace

void net::engine::StackLog(bool server)
{
    static const bool on = getenv("GTA2_NET_STACKLOG") != NULL;
    static std::set<std::pair<Car*, Car*>> sSeen; // ponytail: printed once per pair, never cleared (a dev log)
    if (!on || !gCar_BC_Pool)
    {
        return;
    }
    const uint32_t tick = (uint32_t)gpRng->get_cur_rng();
    for (Car* a = gCar_BC_Pool->mPool.mpPrev; a; a = a->mpNext)
    {
        for (Car* b = a->mpNext; b; b = b->mpNext)
        {
            Sprite* sa = a->mCarSprite;
            Sprite* sb = b->mCarSprite;
            if (!sa || !sb)
            {
                continue;
            }
            const float dx = (sa->mXy.x - sb->mXy.x).ToFloat(), dy = (sa->mXy.y - sb->mXy.y).ToFloat();
            // Closer than 0.4 blocks: cars queueing are farther apart, and a queue isn't a stack
            if (dx * dx + dy * dy > 0.4f * 0.4f || !sSeen.insert(std::make_pair(a, b)).second)
            {
                continue;
            }
            printf("net: STACK %s tick %u: car %u model %d (%.2f, %.2f, %.2f) status %d physics %d, car %u model %d (%.2f, %.2f, %.2f) status %d physics %d; driven by %s, %s\n",
                   server ? "server" : "client", tick, server ? 0u : MappedId(a), (int)a->mCarInfoIdx, sa->mXy.x.ToFloat(),
                   sa->mXy.y.ToFloat(), sa->mZpos.ToFloat(), (int)a->mDespawnStatus, a->mPhysics != NULL,
                   server ? 0u : MappedId(b), (int)b->mCarInfoIdx, sb->mXy.x.ToFloat(), sb->mXy.y.ToFloat(),
                   sb->mZpos.ToFloat(), (int)b->mDespawnStatus, b->mPhysics != NULL, Who(a), Who(b));
        }
    }
}

// GTA2_NET_CHECKGRID: a sprite moved off the map (its rows and cells in the collision grid would be past the grid's),
// what it is and the code that moved it: the first few
static void SpriteMovedOff(Sprite* sp)
{
    static int sSaid;
    const int32_t kMax = 256 << 14;
    const int32_t x = sp->mXy.x.mValue, y = sp->mXy.y.mValue;
    if ((x >= 0 && x < kMax && y >= 0 && y < kMax) || sSaid >= 6)
    {
        return;
    }
    sSaid++;
    printf("net: CHECK frame %u tick %u: sprite %p type %d moved off the map to (%.2f, %.2f, %.2f); backtrace:\n", gDebugFrame,
           (uint32_t)gpRng->get_cur_rng(), (void*)sp, (int)sp->mSpriteTypeEnum, sp->mXy.x.ToFloat(),
           sp->mXy.y.ToFloat(), sp->mZpos.ToFloat());
    if (sp->mSpriteTypeEnum == sprite_types_enum::ped_3 && sp->mCharB4Ptr)
    {
        Char* c = sp->mCharB4Ptr;
        Ped* p = c->mpPed;
        uint32_t id = 0;
        for (const auto& r : ReplicaPeds())
        {
            id = r.second == p ? r.first : id;
        }
        printf("net: CHECK   char %p state %d velocity %.4f (raw %d) run speed %.4f rotation %d conveyor (%.4f, %.4f); ped %p net %u id %d states %d/%d player %p\n",
               (void*)c, (int)c->mCharState, c->mVelocity.ToFloat(), c->mVelocity.mValue, c->mRunOrJumpSpeed.ToFloat(),
               (int)c->mRotation.rValue, c->mConveyorDx.ToFloat(), c->mConveyorDy.ToFloat(), (void*)p, id,
               p ? (int)p->mId : -1, p ? (int)p->mPedState1 : -1, p ? (int)p->mPedState2 : -1,
               p ? (void*)p->mPlayer : NULL);
    }
    fflush(stdout);
#if !defined(_WIN32)
    void* frames[24];
    const int n = backtrace(frames, 24);
    backtrace_symbols_fd(frames, n, 1);
#endif
}

// GTA2_NET_CHECKGRID: the hook goes in with the first frame's checks
void net::engine::InstallSpriteCheck()
{
    if (Debug().checkGrid)
    {
        gPortSpriteMovedHook = SpriteMovedOff;
    }
}
