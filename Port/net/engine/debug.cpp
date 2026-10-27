// Port/net: the engine's checks, logs and statistics (GTA2_NET_* switches, docs/netcode.md). Nothing here changes the
// game, apart from the server marker drawn over the frame.
#include "internal.hpp"
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
#endif
#include "Port/port_rand_state.h"
#include "Source/rng.hpp"
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
wchar_t gStatusCorner[64], gStatusCentre[64];
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

namespace
{
void Widen(const std::string& text, wchar_t* out, size_t size)
{
    size_t n = 0;
    for (; n < text.size() && n + 1 < size; n++)
    {
        out[n] = (wchar_t)(unsigned char)text[n];
    }
    out[n] = 0;
}
} // namespace

namespace
{
wchar_t gAnnouncement[128];
long long gAnnouncementUntilMs;
long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

// Over the frame for 4 s, as the netcode's other lines: the game's own message line is in the HUD, which the rollback
// restores to before the message (it showed for a frame)
void net::engine::ShowHudMessage(const char* text)
{
    Widen(text, gAnnouncement, sizeof(gAnnouncement) / sizeof(gAnnouncement[0]));
    gAnnouncementUntilMs = NowMs() + 4000;
}

void net::engine::SetNetStatus(const std::string& corner, const std::string& centre)
{
    Widen(corner, gStatusCorner, sizeof(gStatusCorner) / sizeof(gStatusCorner[0]));
    Widen(centre, gStatusCentre, sizeof(gStatusCentre) / sizeof(gStatusCentre[0]));
}

void net::engine::DrawOverlay()
{
    Camera* cam = gViewCamera_676978;
    if (!cam)
    {
        return;
    }
    // In the HUD's 640-wide units (DrawTextScaled_5D77A0 scales them as it does the HUD's)
    const float scale = cam->field_A8_ui_scale.ToFloat() > 0 ? cam->field_A8_ui_scale.ToFloat() : 1.0f;
    const s32 w = (s32)(cam->field_68_screen_px_width / scale), h = (s32)(cam->field_6C_screen_px_height / scale);
    if (gStatusCorner[0])
    {
        DrawTextScaled_5D77A0(gStatusCorner, w - 8 - Frontend::GetMaxTextWidth_5D8990(gStatusCorner, word_703BAA), h - 24, word_703BAA);
    }
    if (gAnnouncement[0] && NowMs() < gAnnouncementUntilMs)
    {
        DrawTextScaled_5D77A0(gAnnouncement, (w - Frontend::GetMaxTextWidth_5D8990(gAnnouncement, word_703BAA)) / 2, h / 4, word_703BAA);
    }
    if (gStatusCentre[0])
    {
        DrawTextScaled_5D77A0(gStatusCentre, (w - Frontend::GetMaxTextWidth_5D8990(gStatusCentre, word_703BAA)) / 2, h / 3, word_703BAA);
    }
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

// --- The comparison check (docs/netcode.md, "Checks"): the client's prediction for a tick against the server's state
// at it, field by field. Called with the game restored to the snapshot's tick, before the snapshot is applied ---

namespace
{
struct FieldTally
{
    uint64_t compared = 0, differ = 0;
    double absSum = 0;
};
std::vector<std::vector<FieldTally>> gTally; // [type][field]
uint64_t gMissing[8]; // per type: in the snapshot, mapped, but not captured here
uint64_t gAsleepHere, gAsleepThere; // cars with physics on one side only (asleep on the other)

bool IsCop(Ped* p)
{
    const s32 o = p->field_240_occupation;
    return (o >= ped_ocupation_enum::police && o <= ped_ocupation_enum::unknown_cop_occu_31) || o == ped_ocupation_enum::roadblock_cop_37;
}

// A car reference of this game's (CarRef) as the server's
int32_t ServerCarRef(int32_t ref, const std::unordered_map<int32_t, int32_t>& toServer)
{
    auto it = toServer.find(ref);
    return it != toServer.end() ? it->second : ref ? -1 : 0;
}
} // namespace

namespace
{
// Each entity of the snapshot this game has too, with this game's state of it as captured here (its references
// into the server's ids): fn(server's, here's, a cop)
template <class Fn> void ForEachPair(const Snapshot& snap, Fn fn)
{
    using namespace net::engine;
    Snapshot here;
    Capture(here);
    std::unordered_map<uint32_t, const EntityState*> byId;
    for (const EntityState& e : here.entities)
    {
        byId[e.id] = &e;
    }
    std::unordered_map<int32_t, int32_t> carToServer; // this game's CarRef -> the server's
    std::unordered_map<int32_t, int32_t> carIdToServer, pedIdToServer; // this game's net ids -> the server's
    for (const auto& kv : ReplicaCars())
    {
        carToServer[CarRef(kv.second)] = (int32_t)(kv.first - kCarIdBase) + 1;
        carIdToServer[(int32_t)(kCarIdBase + kv.second->field_6C_car_id)] = (int32_t)kv.first;
    }
    for (const auto& kv : ReplicaPeds())
    {
        pedIdToServer[(int32_t)(kPedIdBase + kv.second->field_200_id)] = (int32_t)kv.first;
    }
    static std::vector<int> pedRefs, carRefs;
    if (pedRefs.empty())
    {
        PedStateRefFields(&pedRefs, &carRefs);
    }
    const auto toServer = [](int32_t id, const std::unordered_map<int32_t, int32_t>& map) {
        auto it = map.find(id);
        return id < (int32_t)kPedIdBase && id < (int32_t)kCarIdBase ? id : it != map.end() ? it->second : -1; // players: the same
    };
    for (const EntityState& s : snap.entities)
    {
        uint32_t local = s.id; // the world and players: the same ids
        bool cop = false;
        if (s.type == kEntityCar)
        {
            Car* c = ReplicaCar(s.id);
            local = c ? kCarIdBase + (uint32_t)c->field_6C_car_id : 0;
        }
        else if (s.type == kEntityPed)
        {
            Ped* p = ReplicaPed(s.id);
            local = p ? kPedIdBase + p->field_200_id : 0;
            cop = p && IsCop(p);
        }
        else if (s.type == kEntityObject)
        {
            continue; // ponytail: pickups are matched by place (pickups.cpp), not compared here
        }
        if (EntityAge(s))
        {
            continue; // an older state of it, sent again (a busy area's budget): not this tick's, and not applied
        }
        if (!local && s.type != kEntityWorld)
        {
            continue; // not mapped (yet): nothing to compare
        }
        auto it = byId.find(local);
        if (it == byId.end())
        {
            gMissing[s.type]++;
            continue;
        }
        EntityState h = *it->second;
        if (s.type == kEntityCar && CarHasPhysics(h) != CarHasPhysics(s))
        {
            (CarHasPhysics(s) ? gAsleepHere : gAsleepThere)++;
        }
        // References: this game's ids into the server's
        if (s.type == kEntityPlayer)
        {
            h.f[kPlayerCar] = ServerCarRef(h.f[kPlayerCar], carToServer);
            h.f[kPlayerTarget] = ServerCarRef(h.f[kPlayerTarget], carToServer);
        }
        else if (s.type == kEntityPed)
        {
            h.f[kPedCar] = ServerCarRef(h.f[kPedCar], carToServer);
            for (int f : pedRefs)
            {
                h.f[f] = h.f[f] ? toServer(h.f[f], pedIdToServer) : 0;
            }
            for (int f : carRefs)
            {
                h.f[f] = h.f[f] ? toServer(h.f[f], carIdToServer) : 0;
            }
        }
        fn(s, h, cop);
    }
}

void EnsureTally(std::vector<std::vector<FieldTally>>& tally)
{
    const net::Schema& schema = net::engine::NetSchema();
    if (tally.empty())
    {
        tally.resize(schema.types.size() + 1); // the last: cops, apart from the other peds
        for (size_t t = 0; t < schema.types.size(); t++)
        {
            tally[t].resize(schema.types[t].fields.size());
        }
        tally.back().resize(schema.types[net::engine::kEntityPed].fields.size());
    }
}
} // namespace

void net::engine::ComparePrediction(const Snapshot& snap)
{
    EnsureTally(gTally);
    ForEachPair(snap, [](const EntityState& s, const EntityState& h, bool cop) {
        std::vector<FieldTally>& tally = cop ? gTally.back() : gTally[s.type];
        for (size_t f = 0; f < tally.size(); f++)
        {
            tally[f].compared++;
            if (h.f[f] != s.f[f])
            {
                tally[f].differ++;
                tally[f].absSum += fabs((double)h.f[f] - (double)s.f[f]);
            }
        }
    });
}

// --- The divergence check (GTA2_NET_DIVERGE, docs/netcode.md "Checks"): the server's state applied once, then this
// game on its own. Per field, after how many ticks each entity first differed from the server's ---

namespace
{
const int kAgeBuckets = 9; // 0 (right after the apply), 1, 2-3, 4-7, ... 64-127, 128+
struct Divergence
{
    uint64_t first[kAgeBuckets] = {};
    bool seen = false;
    uint32_t exampleId = 0, exampleAge = 0;
    int32_t exampleThere = 0, exampleHere = 0;
};
std::vector<std::vector<Divergence>> gDiverged; // [type][field]
std::unordered_map<uint64_t, bool> gDivergedOnce; // (entity id, field): differed already
std::unordered_map<uint32_t, int> gComparedEntities; // per type, the entities compared at least once
uint64_t gDivergeMissing[8];

int AgeBucket(uint32_t age)
{
    int b = 0;
    while (b < kAgeBuckets - 1 && age >= (1u << b))
    {
        b++;
    }
    return b; // 0 -> 0, 1 -> 1, 2-3 -> 2, ...
}
} // namespace

void net::engine::CompareDivergence(const Snapshot& snap, uint32_t age)
{
    const Schema& schema = NetSchema();
    if (gDiverged.empty())
    {
        gDiverged.resize(schema.types.size() + 1);
        for (size_t t = 0; t < schema.types.size(); t++)
        {
            gDiverged[t].resize(schema.types[t].fields.size());
        }
        gDiverged.back().resize(schema.types[kEntityPed].fields.size());
    }
    const uint64_t missingBefore = gMissing[kEntityCar] + gMissing[kEntityPed];
    ForEachPair(snap, [&](const EntityState& s, const EntityState& h, bool cop) {
        const size_t t = cop ? gDiverged.size() - 1 : s.type;
        gComparedEntities[(uint32_t)t * 0x10000000u + (s.id & 0x0FFFFFFF)] = 1;
        std::vector<Divergence>& d = gDiverged[t];
        for (size_t f = 0; f < d.size(); f++)
        {
            const uint64_t key = ((uint64_t)s.id << 16) | f;
            if (h.f[f] != s.f[f] && !gDivergedOnce.count(key))
            {
                gDivergedOnce[key] = true;
                Divergence& x = d[f];
                if (!x.seen || age < x.exampleAge)
                {
                    x.seen = true, x.exampleId = s.id, x.exampleAge = age, x.exampleThere = s.f[f], x.exampleHere = h.f[f];
                }
                x.first[AgeBucket(age)]++;
            }
        }
    });
    gDivergeMissing[kEntityCar] += gMissing[kEntityCar] + gMissing[kEntityPed] - missingBefore; // ponytail: cars and peds together
    gMissing[kEntityCar] = gMissing[kEntityPed] = 0;
}

void net::engine::DumpPeds(uint32_t tick, bool server)
{
    static FILE* f = NULL;
    static uint32_t from = 0, to = 0xFFFFFFFF;
    static bool opened = false;
    if (!opened)
    {
        opened = true;
        if (const char* spec = getenv("GTA2_NET_DUMPPEDS"))
        {
            std::string path = spec;
            const size_t colon = path.find(':');
            if (colon != std::string::npos)
            {
                sscanf(path.c_str() + colon + 1, "%u:%u", &from, &to);
                path.resize(colon);
            }
            f = fopen(path.c_str(), "wb");
        }
    }
    if (!f || tick < from || tick > to)
    {
        return;
    }
    std::unordered_map<const Ped*, uint32_t> toServer;
    if (!server)
    {
        for (const auto& kv : ReplicaPeds())
        {
            toServer[kv.second] = kv.first;
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        uint32_t id = p->field_15C_player ? 1 + p->field_15C_player->field_2E_idx : server ? kPedIdBase + p->field_200_id : 0;
        if (!id)
        {
            auto it = toServer.find(p);
            id = it != toServer.end() ? it->second : 0;
        }
        if (!id)
        {
            continue; // one of this game's own
        }
        const Char* c = p->field_168_game_object;
        const Sprite* s = c ? c->field_80_sprite_ptr : NULL;
        const uint32_t head[5] = {tick, id, (uint32_t)sizeof(Ped), c ? (uint32_t)sizeof(Char) : 0, s ? (uint32_t)sizeof(Sprite) : 0};
        fwrite(head, sizeof(head), 1, f);
        fwrite(p, sizeof(Ped), 1, f);
        if (c)
        {
            fwrite(c, sizeof(Char), 1, f);
        }
        if (s)
        {
            fwrite(s, sizeof(Sprite), 1, f);
        }
    }
    fflush(f);
}

namespace
{
FILE* gRandLog;
uint32_t gRandFrom, gRandTo;
void LogRand(void* caller)
{
    const uint32_t cycle = (uint32_t)gpRng_67AB34->get_cur_rng_41CFE0();
    if (cycle >= gRandFrom && cycle <= gRandTo)
    {
        uintptr_t base = 0;
#if !defined(_WIN32)
        Dl_info info;
        base = dladdr(caller, &info) ? (uintptr_t)info.dli_fbase : 0;
#endif
        // the caller, and two above it (past the rng wrappers: rng::get_int_4F7AE0 and the like)
        fprintf(gRandLog, "%u 0x%lx 0x%lx 0x%lx\n", cycle, (unsigned long)((uintptr_t)caller - base),
                (unsigned long)((uintptr_t)__builtin_return_address(2) - base), (unsigned long)((uintptr_t)__builtin_return_address(3) - base));
    }
}
} // namespace

void net::engine::StartRandLog()
{
    const char* spec = getenv("GTA2_NET_RANDLOG");
    if (!spec || gRandLog)
    {
        return;
    }
    std::string path = spec;
    const size_t colon = path.find(':');
    if (colon != std::string::npos)
    {
        sscanf(path.c_str() + colon + 1, "%u:%u", &gRandFrom, &gRandTo);
        path.resize(colon);
    }
    gRandLog = fopen(path.c_str(), "w");
    gPortRandLog = gRandLog ? LogRand : NULL;
}

void net::engine::PrintDivergence()
{
    const Schema& schema = NetSchema();
    int counts[16] = {};
    for (const auto& kv : gComparedEntities)
    {
        counts[kv.first / 0x10000000u]++;
    }
    printf("net: divergence, entities compared:");
    for (size_t t = 0; t < gDiverged.size(); t++)
    {
        printf(" %s %d", t == schema.types.size() ? "cop" : schema.types[t].name, counts[t]);
    }
    printf("; mapped but gone here %llu times\n", (unsigned long long)gDivergeMissing[kEntityCar]);
    printf("net: divergence, per field the entities that first differed after 0 | 1 | 2-3 | 4-7 | 8-15 | 16-31 | 32-63 | 64-127 | 128+ ticks:\n");
    for (size_t t = 0; t < gDiverged.size(); t++)
    {
        const bool cop = t == schema.types.size();
        for (size_t f = 0; f < gDiverged[t].size(); f++)
        {
            const Divergence& x = gDiverged[t][f];
            if (!x.seen)
            {
                continue;
            }
            printf("  %s.%s:", cop ? "cop" : schema.types[t].name, schema.types[cop ? kEntityPed : t].fields[f].name);
            for (int b = 0; b < kAgeBuckets; b++)
            {
                printf(" %llu", (unsigned long long)x.first[b]);
            }
            printf("  (first: entity %u after %u ticks, server %d, here %d)\n", x.exampleId, x.exampleAge, x.exampleThere, x.exampleHere);
        }
    }
    fflush(stdout);
}

void net::engine::PrintPredictionStats(uint32_t frame)
{
    const Schema& schema = NetSchema();
    struct Row
    {
        double share;
        std::string line;
    };
    std::vector<Row> rows;
    for (size_t t = 0; t < gTally.size(); t++)
    {
        for (size_t f = 0; f < gTally[t].size(); f++)
        {
            const FieldTally& x = gTally[t][f];
            if (x.differ)
            {
                char line[160];
                const bool cop = t == schema.types.size();
                snprintf(line, sizeof(line), "%s.%s %.1f%% (%llu of %llu, %.1f off on average)", cop ? "cop" : schema.types[t].name,
                         schema.types[cop ? kEntityPed : t].fields[f].name,
                         100.0 * x.differ / x.compared, (unsigned long long)x.differ, (unsigned long long)x.compared, x.absSum / x.differ);
                rows.push_back(Row{(double)x.differ / x.compared, line});
            }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.share > b.share; });
    printf("net: frame %u, prediction against the server, the fields that differ most:", frame);
    for (size_t i = 0; i < rows.size() && i < 12; i++)
    {
        printf("%s %s", i ? ";" : "", rows[i].line.c_str());
    }
    printf("%s; not captured here: %llu cars, %llu peds; car physics asleep here only %llu, there only %llu\n", rows.empty() ? " none" : "",
           (unsigned long long)gMissing[kEntityCar], (unsigned long long)gMissing[kEntityPed], (unsigned long long)gAsleepHere, (unsigned long long)gAsleepThere);
    gTally.clear();
    memset(gMissing, 0, sizeof(gMissing));
    gAsleepHere = gAsleepThere = 0;
}
