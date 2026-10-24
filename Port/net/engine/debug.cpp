// Port/net: the engine's checks, logs and statistics (GTA2_NET_* switches, docs/netcode.md). Nothing here changes the
// game, apart from the server marker drawn over the frame.
#include "internal.hpp"
#include "Source/Camera.hpp"
#include "Source/Car.hpp"
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
#include <stdio.h>
#include <stdlib.h>
#include <set>
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
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars.insert(c);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
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
// collision query: SpriteGrid::FindCollidingSpriteInRow_478880)
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
        Car* car = sp->field_8_car_bc_ptr;
        return !cars.count(car) ? "a car that is gone" : car->field_50_car_sprite != sp ? "not its car's sprite" : NULL;
    }
    if (type == sprite_types_enum::ped_3)
    {
        Char* ch = sp->field_8_char_b4_ptr;
        return !chars.count(ch) ? "a char that is gone" : ch->field_80_sprite_ptr != sp ? "not its char's sprite" : NULL;
    }
    if (type == sprite_types_enum::object_1 || type == sprite_types_enum::code_obj1_4 || type == sprite_types_enum::map_obj_5)
    {
        Object* o = sp->field_8_object_2C_ptr;
        return !o                       ? "no object"
             : o->field_4_sprite != sp ? "not its object's sprite"
             : o->field_8_definition && o->field_8_definition->field_40_collision_bucket_category != collision_bucket_category::sprite_grid_1_region_4
                 ? "an object that doesn't go in this grid (a link left from an earlier owner)"
                 : NULL;
    }
    return NULL;
}

void ReportLink(const char* where, int cellX, int y, Sprite* sp, int type, const char* wrong, const std::set<Ped*>& peds)
{
    printf("net: CHECK frame %u %s: grid 1 cell (%d, %d) links sprite %p (type %d): %s\n", gDebugFrame, where, cellX, y, (void*)sp, type, wrong);
    if (type == sprite_types_enum::ped_3 && sp && sp->field_8_char_b4_ptr)
    {
        const Char* ch = sp->field_8_char_b4_ptr;
        bool freed = false;
        for (Char* f = gChar_B4_Pool_6FDB44->field_0_pool.field_0_pHead; f && !freed; f = f->mpNext)
        {
            freed = f == ch;
        }
        Ped* owner = ch->field_7C_pPed;
        const bool live = owner && peds.count(owner);
        printf("net: CHECK   the char is %s, points to ped %p (in the pool %d, id %d, its char %p, states %d/%d)\n", freed ? "freed" : "allocated",
               (void*)owner, (int)live, live ? (int)owner->field_200_id : -1, live ? (void*)owner->field_168_game_object : NULL,
               live ? (int)owner->field_278_ped_state_1 : -1, live ? (int)owner->field_27C_ped_state_2 : -1);
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
    LinkWas w = {type, type == sprite_types_enum::car_2 ? (const void*)sp->field_8_car_bc_ptr : (const void*)sp->field_8_object_2C_ptr,
                 type == sprite_types_enum::car_2 ? (int)sp->field_8_car_bc_ptr->field_84_car_info_idx : -1,
                 type == sprite_types_enum::car_2 ? (int)sp->field_8_car_bc_ptr->field_88_despawn_status : -1, gDebugFrame};
    if (type == sprite_types_enum::ped_3)
    {
        auto cp = charPed.find(sp->field_8_char_b4_ptr);
        if (cp != charPed.end())
        {
            Ped* p = cp->second;
            w.pedId = (int)p->field_200_id;
            w.state1 = (int)p->field_278_ped_state_1;
            w.state2 = (int)p->field_27C_ped_state_2;
            w.objective = (int)p->get_objective_403A80();
            w.player = p->field_15C_player ? (int)p->field_15C_player->field_2E_idx : -1;
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
        if (p->field_168_game_object)
        {
            chars.insert(p->field_168_game_object);
            charPed[p->field_168_game_object] = p;
        }
    }
    std::unordered_map<const Ped*, uint32_t> netIds;
    for (auto& q : ReplicaPeds())
    {
        netIds[q.second] = q.first;
    }
    std::unordered_map<const void*, LinkWas> now;
    GridCell* const* rows = reinterpret_cast<GridCell* const*>(gSpriteGrid_1_679208); // field_0_rows, private, first
    for (int y = 0; rows && y < 256; y++)
    {
        for (GridCell* c = rows[y]; c; c = c->mpNext)
        {
            for (GridSpriteLink* l = c->field_4_pSpriteLinks; l; l = l->mpNext)
            {
                Sprite* sp = l->field_0_pSprite;
                const int type = sp ? sp->GetType_416B40() : -1;
                if (const char* wrong = WhatsWrong(sp, type, cars, chars))
                {
                    if (sReported++ < 4)
                    {
                        ReportLink(where, (int)c->field_0_x, y, sp, type, wrong, peds);
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
            printf("net: CHECK %s: car %p model %d, kind %d, driver %p, despawn status %d\n", where, (void*)car, (int)car->field_84_car_info_idx,
                   (int)car->field_A0_car_kind, (void*)car->field_54_driver, (int)car->field_88_despawn_status);
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
        Car* car = ped ? ped->field_16C_car : NULL;
        if (car && (!liveCars.count(car) || !car->field_50_car_sprite))
        {
            if (sReported++ < 5)
            {
                printf("net: CHECK %s: player %d's ped is in a car that is gone (state %d/%d, despawn status %d)\n", where, i,
                       (int)ped->field_278_ped_state_1, (int)ped->field_27C_ped_state_2, liveCars.count(car) ? (int)car->field_88_despawn_status : -1);
            }
            return false;
        }
        // Driving a car that doesn't have it as its driver (Car::ClearDriver_4407F0 reads the driver when it gets out)
        if (car && ped->field_27C_ped_state_2 == ped_state_2::ped2_driving_10 && !ped->field_248_enter_car_as_passenger && car->field_54_driver != ped)
        {
            static int sDriver;
            if (sDriver++ < 10)
            {
                Ped* d = car->field_54_driver;
                printf("net: CHECK %s: player %d drives car %d, which has driver %p (spare %d, player %d), mapped %d\n", where, i,
                       (int)car->field_6C_car_id, (void*)d, (int)IsSpareDriver(d), d && d->field_15C_player ? (int)d->field_15C_player->field_2E_idx : -1,
                       (int)(MappedId(car) != 0));
            }
            return false;
        }
    }
    return true;
}

void net::engine::DebugOnCrash()
{
    Game* g = gGame_0x40_67E008;
    if (!g || !g->field_38_local_player)
    {
        return;
    }
    std::set<Car*> liveCars;
    std::set<Ped*> livePeds;
    Live(liveCars, livePeds);
    Player* p = g->field_38_local_player;
    Ped* ped = p->field_2C4_player_ped;
    Ped* camPed = p->field_90_game_camera.field_34_target_ped;
    Car* camCar = p->field_90_game_camera.field_38_target_car;
    fprintf(stderr, "net: crash state: local player %d in use %d, ped %p (live %d) car %p, camera ped %p (live %d, the player's %d) car %p (live %d)\n",
            (int)p->field_2E_idx, (int)p->field_8E_in_use, (void*)ped, (int)livePeds.count(ped), ped ? (void*)ped->field_16C_car : NULL,
            (void*)camPed, (int)livePeds.count(camPed), camPed == ped, (void*)camCar, (int)liveCars.count(camCar));
    // The sound listener's camera (sound_obj::InterrogateAudioEntities_41A730 reads its followed ped's car)
    Camera* listener = NULL;
    if (gSound_obj_66F680.field_1478_type5Idx)
    {
        SoundObject* so = gSound_obj_66F680.field_147C_audio_entities[gSound_obj_66F680.field_1478_type5Idx].field_4_pObj;
        listener = so ? so->field_C_pAny.pCamera_0xBC : NULL;
    }
    Camera* cams[3] = {&p->field_90_game_camera, &p->field_208_aux_game_camera, &p->field_14C_view_camera};
    for (int k = 0; k < 3; k++)
    {
        Ped* cp = cams[k]->field_34_target_ped;
        fprintf(stderr, "net: crash state: camera %d%s follows ped %p (live %d, in car %p live %d)\n", k, cams[k] == listener ? " (the listener)" : "",
                (void*)cp, (int)livePeds.count(cp), cp && livePeds.count(cp) ? (void*)cp->field_16C_car : NULL,
                cp && livePeds.count(cp) ? (int)liveCars.count(cp->field_16C_car) : -1);
    }
    if (listener && listener != cams[0] && listener != cams[1] && listener != cams[2])
    {
        Ped* cp = listener->field_34_target_ped;
        fprintf(stderr, "net: crash state: the listener is another camera %p, follows ped %p (live %d, aux of the player %d)\n", (void*)listener,
                (void*)cp, (int)livePeds.count(cp), cp == p->field_2C8_aux_ped);
    }
    Car* cars[2] = {ped ? ped->field_16C_car : NULL, camPed ? camPed->field_16C_car : NULL};
    for (int k = 0; k < 2; k++)
    {
        if (cars[k])
        {
            fprintf(stderr, "net: crash state: car %p live %d sprite %p despawn status %d driver %p mapped %d\n", (void*)cars[k],
                    (int)liveCars.count(cars[k]), (void*)cars[k]->field_50_car_sprite, (int)cars[k]->field_88_despawn_status,
                    (void*)cars[k]->field_54_driver, (int)(MappedId(cars[k]) != 0));
        }
    }
}

void net::engine::DebugSetFrame(uint32_t frame)
{
    gDebugFrame = frame;
}

void net::engine::DebugOwnCarDiff(const Snapshot& snap)
{
    Ped* ped = GetPlayer(LocalPlayer()) ? GetPlayer(LocalPlayer())->field_2C4_player_ped : NULL;
    Car* car = ped ? ped->field_16C_car : NULL;
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
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (!ped)
    {
        return "";
    }
    char line[256];
    int n = snprintf(line, sizeof(line), "here: states %d/%d objective %d door %d car %p%s%s", (int)ped->field_278_ped_state_1,
                     (int)ped->field_27C_ped_state_2, (int)ped->get_objective_403A80(), (int)ped->field_24C_target_car_door, (void*)ped->field_16C_car,
                     InCarTransition(ped) ? " (transition)" : "", ped->field_150_target_objective_car ? " (target car)" : "");
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
    Ped* ped = p ? p->field_2C4_player_ped : NULL;
    if (!ped)
    {
        return;
    }
    if (p->field_28_wasted_or_busted || ped->isDead_403B60())
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
           frame, s ? s->field_14_xy.x.ToFloat() : -1.0f, s ? s->field_14_xy.y.ToFloat() : -1.0f, (int)ped->field_278_ped_state_1,
           (int)ped->field_27C_ped_state_2, (int)ped->isDead_403B60(), (int)p->field_28_wasted_or_busted, (int)p->field_2C_death_countdown,
           (void*)p->field_2C8_aux_ped, (int)p->field_68_camera_mode, (int)ped->field_216_health, (void*)ped->field_168_game_object,
           (void*)ped->field_16C_car, (int)ped->get_objective_403A80());
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
        Char* c = ped ? ped->field_168_game_object : NULL;
        if (c && i != LocalPlayer())
        {
            snprintf(buf, sizeof(buf), " p%d[in 0x%x ped %d/%d char %d anim %d frame %d timer %d]", i, (unsigned)GetPlayer(i)->field_4_inputs,
                     (int)ped->field_278_ped_state_1, (int)ped->field_27C_ped_state_2, (int)c->field_10_char_state, (int)c->field_6C_animation_state,
                     (int)c->field_68_animation_frame, (int)c->field_70_frame_timer);
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
        const sound_0x68& a = gSound_obj_66F680.field_DC0_ActiveSamples[i];
        if (a.field_2C_bIsBeingPlayed)
        {
            snprintf(buf, sizeof(buf), " ch%d:ent%d/samp%d/loops%u/type%d/rel%u/ftp%d", i, a.field_0_EntityIndex, a.field_14_samp_idx, a.field_30_loop_count,
                     a.field_58_type, a.field_4C_releasing_volume_divider, a.field_44_frames_to_play);
            line += buf;
            const int ent = a.field_0_EntityIndex;
            const AudioEntity* ae = ent >= 0 && ent < 1020 ? &gSound_obj_66F680.field_147C_audio_entities[ent] : NULL;
            if (ae && ae->field_0_bUsed && ae->field_4_pObj && ae->field_4_pObj->field_0_object_type == 7)
            {
                const Weapon* w = ae->field_4_pObj->field_C_pAny.pWeapon_30;
                const Ped* ped = w ? w->field_24_pPed : NULL;
                snprintf(buf, sizeof(buf), "[weapon %d ammo %d reload %d shot %d ped %d player %d]", w ? w->field_1C_idx : -1, w ? w->field_0_ammo : -1,
                         w ? w->field_2_reload_timer : -1, w ? w->field_2C_shot_fired : -1, ped ? ped->field_200_id : -1,
                         ped && ped->field_15C_player ? ped->field_15C_player->field_2E_idx : -1);
                line += buf;
            }
        }
    }
    if (line != sLast)
    {
        printf("net: frame %u, sound channels:%s (init %d)\n", frame, line.c_str(), gSound_obj_66F680.field_0_bSoundInitialized);
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

void net::engine::DrawOverlay()
{
    Camera* cam = gViewCamera_676978;
    if (!gMarker || !cam)
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
