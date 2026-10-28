// Port/net (docs/netcode.md, "Entities"): the server's pickups (weapons, power-ups, tokens: Object::IsPickup_421060) and
// roadblock barriers on the clients. Other objects aren't sent: a client makes its own fires, explosions and projectiles. A client adopts a
// pickup of its own at the same place (the map script made it on both sides), makes the ones it lacks
// (Object_5C::NewPhysicsObj_5299B0, as the game drops one) and removes those the server no longer has (collected, or
// its own: one it collected in a prediction comes back while the server still has it).
#include "internal.hpp"
#include "dev/dev.hpp"
#include "Source/Object_5C.hpp"
#include "Source/ObjectPool.hpp"
#include "Source/sprite.hpp"
#include <set>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unordered_map>

namespace
{
std::unordered_map<uint32_t, Object*> gObjects; // NetId -> this game's pickup

// A pickup, or a police roadblock's barrier: the police are the server's (Police_38's roadblocks make them), so a
// client has none of its own
bool Usable(const Object* o)
{
    return o && o->field_4_sprite && o->field_8_definition && !o->field_25_removal_state &&
           (const_cast<Object*>(o)->IsPickup_421060() || o->field_18_model == objects::roadblock_21);
}

// Every live object, whether on the pool's list or not: a barrier is made off it (Object_5C::CreateObject_529C00, a
// definition not in the pool list: AllocateUntracked), and only the sprite grids find it
template <typename F> void ForEachObject(F f)
{
    for (Object& o : gObject_2C_Pool_6F8F80->field_8_pool)
    {
        if (Usable(&o))
        {
            f(&o);
        }
    }
}

std::set<Object*> LivePickups()
{
    std::set<Object*> out;
    ForEachObject([&](Object* o) { out.insert(o); });
    return out;
}

const int32_t kSamePlace = 16384; // within a block: the same pickup as the server's
} // namespace

std::unordered_map<uint32_t, Object*>& net::engine::PickupMap()
{
    return gObjects;
}

void net::engine::ClearPickups()
{
    gObjects.clear();
}

void net::engine::CapturePickups(Snapshot& out)
{
    ForEachObject([&](Object* o) {
        EntityState e = Entity(kObjIdBase + (uint32_t)o->field_14_id, kEntityObject);
        PutPosition(e, o->field_4_sprite);
        e.f[kObjModel] = o->field_18_model;
        e.f[kObjIdx] = o->field_26_ped_ref_idx;
        out.entities.push_back(e);
    });
}

int net::engine::gPickupsMade, net::engine::gPickupsRemoved;

void net::engine::ApplyPickups(const Snapshot& snap)
{
    std::set<Object*> live = LivePickups();
    std::set<Object*> mapped;
    for (auto it = gObjects.begin(); it != gObjects.end();)
    {
        // Gone, or its pool slot another object's now
        if (!live.count(it->second) && Debug().pickupLog)
        {
            static int sDropped;
            const Object* o = it->second;
            if (sDropped++ < 20)
            {
                printf("net: pickup %u dropped: sprite %p definition %p removal %d model %d pool state %d\n", it->first, (void*)o->field_4_sprite,
                       (void*)o->field_8_definition, (int)o->field_25_removal_state, (int)o->field_18_model, (int)o->field_20_pool_list_state);
            }
        }
        it = live.count(it->second) ? (mapped.insert(it->second), ++it) : gObjects.erase(it);
    }
    std::set<uint32_t> seen;
    std::set<Object*> remade; // deallocated here: not adopted or removed again
    for (size_t k = 0; k < snap.entities.size(); k++)
    {
        const EntityState& e = snap.entities[k];
        if (e.type != kEntityObject)
        {
            continue;
        }
        seen.insert(e.id);
        auto it = gObjects.find(e.id);
        Object* o = it != gObjects.end() ? it->second : NULL;
        if (o && EntityAge(e))
        {
            continue; // the state the client already has
        }
        if (o && (o->field_18_model != e.f[kObjModel] || !Near(o->field_4_sprite, e.f[kObjX], e.f[kObjY], kSamePlace)))
        {
            static int sLogged;
            if (Debug().pickupLog && sLogged++ < 20)
            {
                printf("net: pickup %u remade: model here %d there %d, at (%.2f, %.2f) here, (%.2f, %.2f) there\n", e.id, (int)o->field_18_model,
                       e.f[kObjModel], o->field_4_sprite->field_14_xy.x.ToFloat(), o->field_4_sprite->field_14_xy.y.ToFloat(), e.f[kObjX] / 16384.0f,
                       e.f[kObjY] / 16384.0f);
            }
            // Another kind here now, or moved off (a dropped one that slid): made again where the server has it
            o->Dealloc_5291B0();
            remade.insert(o);
            mapped.erase(o);
            gObjects.erase(e.id);
            o = NULL;
        }
        if (!o)
        {
            for (Object* c : live)
            {
                if (!mapped.count(c) && !remade.count(c) && c->field_18_model == e.f[kObjModel] && Near(c->field_4_sprite, e.f[kObjX], e.f[kObjY], kSamePlace))
                {
                    o = c; // this game's own, the same one
                    break;
                }
            }
        }
        if (!o && Debug().pickupLog)
        {
            static int sMade;
            if (sMade++ < 20 || e.f[kObjModel] == objects::roadblock_21) // barriers: rare, and each worth seeing
            {
                printf("net: pickup %u made: model %d at (%.2f, %.2f), %zu live pickups here\n", e.id, e.f[kObjModel], e.f[kObjX] / 16384.0f,
                       e.f[kObjY] / 16384.0f, live.size());
            }
        }
        if (!o)
        {
            o = gObject_5C_6F8F84->NewPhysicsObj_5299B0(e.f[kObjModel], Raw(e.f[kObjX]), Raw(e.f[kObjY]), Raw(e.f[kObjZ]), Angle(e.f[kObjRot]));
            if (!o)
            {
                continue;
            }
            o->field_26_ped_ref_idx = (u8)e.f[kObjIdx];
            gPickupsMade++;
        }
        gObjects[e.id] = o;
        mapped.insert(o);
    }
    // Not in the snapshot: collected on the server, or out of the area. Only well inside it (20 of the 24 blocks) is it
    // gone; nearer the edge it stays (in and out of the area as we move, it was made again and again)
    const ReachArea area(snap, 2.0f);
    std::set<Object*> gone;
    for (auto it = gObjects.begin(); it != gObjects.end();)
    {
        const Sprite* sp = it->second->field_4_sprite;
        if (!seen.count(it->first) && area.Inside(sp))
        {
            if (Debug().pickupLog)
            {
                printf("net: pickup %u removed: not in the snapshot, %.1f blocks away, model %d\n", it->first, area.DistanceTo(sp),
                       (int)it->second->field_18_model);
            }
            it->second->Dealloc_5291B0();
            gone.insert(it->second); // not this game's own below as well
            mapped.erase(it->second);
            it = gObjects.erase(it);
            gPickupsRemoved++;
        }
        else
        {
            ++it;
        }
    }
    for (Object* c : live)
    {
        if (!mapped.count(c) && !gone.count(c) && !remade.count(c) && area.Inside(c->field_4_sprite))
        {
            if (Debug().pickupLog)
            {
                printf("net: pickup of this game's own removed: model %d id %d at (%.2f, %.2f)\n", (int)c->field_18_model, (int)c->field_14_id,
                       c->field_4_sprite->field_14_xy.x.ToFloat(), c->field_4_sprite->field_14_xy.y.ToFloat());
            }
            c->Dealloc_5291B0(); // this game's own that the server doesn't have (here)
            gPickupsRemoved++;
        }
    }
}

int net::engine::PickupModel(const EntityState& e)
{
    return e.type == kEntityObject ? e.f[kObjModel] : 0;
}
