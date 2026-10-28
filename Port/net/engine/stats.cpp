// Port/net engine: the statistics in the session's periodic log lines: pool use, the replicas, the drivers.
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/Char_Pool.hpp"
#include "Source/Ped.hpp"
#include "Source/sprite.hpp"
#include <math.h>
#include <set>

namespace
{
using namespace net::engine;
using net::EntityState;
using net::Snapshot;

// This game's live cars and peds (a mapped one may have been removed by the game since)
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
    *pCars = (int)ReplicaCars().size();
    *pPeds = (int)ReplicaPeds().size();
    float drift = 0;
    for (const EntityState& e : snap.entities)
    {
        Sprite* s = NULL;
        if (e.type == kEntityCar)
        {
            Car* c = ReplicaCar(e.id);
            s = c && liveCars.count(c) ? c->field_50_car_sprite : NULL;
        }
        else if (e.type == kEntityPed)
        {
            Ped* p = ReplicaPed(e.id);
            s = p && livePeds.count(p) ? CharSprite(p) : NULL;
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

void net::engine::DriverStats(const Snapshot& snap, int* pSent, int* pDriven, int* pSame)
{
    *pSent = *pDriven = *pSame = 0;
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    for (const EntityState& e : snap.entities)
    {
        if (e.type != kEntityPed || !e.f[kPedCar])
        {
            continue;
        }
        ++*pSent;
        Car* car = RefCar(e.f[kPedCar]);
        if (car && liveCars.count(car) && car->field_54_driver)
        {
            ++*pDriven;
            *pSame += ReplicaPed(e.id) == car->field_54_driver;
        }
    }
}

void net::engine::TrafficDriverStats(const Snapshot& snap, int* pSent, int* pSame)
{
    *pSent = *pSame = 0;
    for (const EntityState& e : snap.entities)
    {
        if (e.type == kEntityCar && e.f[kCarDriver] == kDriverTraffic)
        {
            ++*pSent;
            Car* car = ReplicaCar(e.id);
            *pSame += car && IsSpareDriver(car->field_54_driver);
        }
    }
}
