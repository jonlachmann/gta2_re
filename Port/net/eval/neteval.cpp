// Port (docs/netcode.md, "Evaluations"): GTA2_NETEVAL=rand,soundrollback,churn,diverge,single,explode, measurements
// for the netcode design. The sim bench (sim_bench.cpp) runs them after its fill (if any) and before the tick
// benchmark. GTA2_NETEVAL_OUT=<file>: the per-offset churn.
#include "neteval.hpp"
#include "Game.hpp"
#include "Car.hpp"
#include "CarAI.hpp"
#include "CarPhysics.hpp"
#include "Camera.hpp"
#include "Char_Pool.hpp"
#include "Globals.hpp"
#include "Object_5C.hpp"
#include "ObjectPool.hpp"
#include "Ped.hpp"
#include "Player.hpp"
#include "WeaponManager.hpp"
#include "explosion_type.hpp"
#include "root_sound.hpp"
#include "Port/port_rand_sound.h"
#include "Port/port_rand_state.h"
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using net::engine::Block;
using net::engine::RestoreWorld;
using net::engine::SaveWorld;

namespace
{
void Tick()
{
    gGame_0x40_67E008->UpdateGame_4B9410();
}

struct PoolView
{
    const char* name;
    unsigned char* base;
    size_t stride;
    size_t count;
};

std::vector<PoolView> Pools()
{
    std::vector<PoolView> v;
#define POOLVIEW(name, arr) \
    { \
        PoolView pv = {name, (unsigned char*)&(arr)[0], sizeof((arr)[0]), sizeof(arr) / sizeof((arr)[0])}; \
        v.push_back(pv); \
    }
    POOLVIEW("Car", gCar_BC_Pool_67792C->field_0_pool.field_8_pool);
    POOLVIEW("CarPhysics", gCarPhysicsPool_6FE3E0->field_4_pool);
    POOLVIEW("CarAI", gCarAI_78_Pool_677CF8->field_0_pool.field_4_pool);
    POOLVIEW("Ped", gPedPool_6787B8->field_0_pool.field_8_pool);
    POOLVIEW("Char", gChar_B4_Pool_6FDB44->field_0_pool.field_4_pool);
    POOLVIEW("Object", gObject_2C_Pool_6F8F80->field_8_pool);
    POOLVIEW("Weapon", gWeapon_30_Pool_707014->field_0_pool.field_8_pool);
    POOLVIEW("Sprite", gSpritePool_703818->field_0_pool.field_4_pool);
#undef POOLVIEW
    if (Player* p = gGame_0x40_67E008->field_4_players[0])
    {
        PoolView pv = {"Player", (unsigned char*)p, sizeof(Player), 1};
        v.push_back(pv);
    }
    return v;
}

// How many bytes of each entity type change per tick, and which offsets ever change (an upper bound for the
// net schema: derived and scratch fields change too)
void Churn(int ticks)
{
    std::vector<PoolView> pools = Pools();
    std::vector<std::vector<unsigned char> > prev(pools.size());
    std::vector<std::vector<unsigned> > perOffset(pools.size());
    std::vector<std::vector<char> > slotChanged(pools.size());
    std::vector<double> bytes(pools.size()), entities(pools.size());
    for (size_t i = 0; i < pools.size(); i++)
    {
        prev[i].assign(pools[i].base, pools[i].base + pools[i].stride * pools[i].count);
        perOffset[i].assign(pools[i].stride, 0);
        slotChanged[i].assign(pools[i].count, 0);
    }
    for (int t = 0; t < ticks; t++)
    {
        Tick();
        for (size_t i = 0; i < pools.size(); i++)
        {
            const PoolView& pv = pools[i];
            for (size_t slot = 0; slot < pv.count; slot++)
            {
                unsigned char* now = pv.base + slot * pv.stride;
                unsigned char* was = &prev[i][slot * pv.stride];
                if (!memcmp(now, was, pv.stride))
                {
                    continue;
                }
                entities[i]++;
                slotChanged[i][slot] = 1;
                for (size_t o = 0; o < pv.stride; o++)
                {
                    if (now[o] != was[o])
                    {
                        perOffset[i][o]++;
                        bytes[i]++;
                    }
                }
                memcpy(was, now, pv.stride);
            }
        }
    }
    FILE* out = getenv("GTA2_NETEVAL_OUT") ? fopen(getenv("GTA2_NETEVAL_OUT"), "w") : NULL;
    for (size_t i = 0; i < pools.size(); i++)
    {
        int offsets = 0, slots = 0;
        for (size_t o = 0; o < pools[i].stride; o++)
        {
            offsets += perOffset[i][o] != 0;
            if (out && perOffset[i][o])
            {
                fprintf(out, "%s %zu %u\n", pools[i].name, o, perOffset[i][o]);
            }
        }
        for (size_t s = 0; s < pools[i].count; s++)
        {
            slots += slotChanged[i][s];
        }
        fprintf(stderr, "neteval churn: %-10s size %4zu: %4d slots changed; per tick %6.1f entities change, %7.0f bytes (%.1f per changed entity); %4d offsets ever change\n",
                pools[i].name, pools[i].stride, slots, entities[i] / ticks, bytes[i] / ticks, entities[i] ? bytes[i] / entities[i] : 0.0, offsets);
    }
    if (out)
    {
        fclose(out);
    }
}

// Entities within the first player's camera view, padded by half its size on every side (a snapshot's area)
void InterestCounts()
{
    Camera* cam = gGame_0x40_67E008->IteratePlayerCamera_4B9BC0();
    const WorldRect r = cam->field_20_padded_view_bounds;
    const Fix16 w = (r.field_4_right - r.field_0_left) / 2, h = (r.field_C_bottom - r.field_8_top) / 2;
    const Fix16 l = r.field_0_left - w, rt = r.field_4_right + w, t = r.field_8_top - h, b = r.field_C_bottom + h;
#define INSIDE(xy) ((xy).x >= l && (xy).x <= rt && (xy).y >= t && (xy).y <= b)
    int cars = 0, peds = 0, objects = 0;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars += c->field_50_car_sprite && INSIDE(c->field_50_car_sprite->field_14_xy);
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        peds += p->field_168_game_object && p->field_168_game_object->field_80_sprite_ptr && INSIDE(p->field_168_game_object->field_80_sprite_ptr->field_14_xy);
    }
    for (Object* o = gObject_2C_Pool_6F8F80->field_4_pPrev; o; o = o->mpNext)
    {
        objects += o->field_4_sprite && INSIDE(o->field_4_sprite->field_14_xy);
    }
#undef INSIDE
    fprintf(stderr, "neteval interest: view %.1f x %.1f blocks, area %.1f x %.1f: %d cars, %d peds on foot, %d objects\n",
            (w * 2).ToFloat(), (h * 2).ToFloat(), (w * 4).ToFloat(), (h * 4).ToFloat(), cars, peds, objects);
}

struct Pos
{
    u32 id;
    Fix16 x, y;
};

void Positions(std::vector<Pos>& cars, std::vector<Pos>& peds)
{
    cars.clear();
    peds.clear();
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        if (c->field_50_car_sprite)
        {
            Pos p = {(u32)c->field_6C_car_id, c->field_50_car_sprite->field_14_xy.x, c->field_50_car_sprite->field_14_xy.y};
            cars.push_back(p);
        }
    }
    for (Ped* p = gPedPool_6787B8->field_0_pool.field_4_pPrev; p; p = p->mpNext)
    {
        Pos q = {p->field_200_id, p->get_cam_x(), p->get_cam_y()};
        peds.push_back(q);
    }
}

void Compare(const char* what, int k, const std::vector<Pos>& a, const std::vector<Pos>& b, u32 playerId)
{
    int n = 0, over1px = 0, overQuarter = 0, overOne = 0;
    double sum = 0, player = -1;
    for (size_t i = 0; i < a.size(); i++)
    {
        for (size_t j = 0; j < b.size(); j++)
        {
            if (a[i].id != b[j].id)
            {
                continue;
            }
            const double dx = (a[i].x - b[j].x).ToFloat(), dy = (a[i].y - b[j].y).ToFloat();
            const double d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, dist = d > e ? d : e;
            n++;
            sum += dist;
            over1px += dist > 1.0 / 64;
            overQuarter += dist > 0.25;
            overOne += dist > 1.0;
            if (a[i].id == playerId)
            {
                player = dist;
            }
            break;
        }
    }
    fprintf(stderr, "neteval diverge: %-4s after %2d ticks: %3d compared, %5.1f%% off by > 1 px, %5.1f%% > 1/4 block, %5.1f%% > 1 block, mean %.3f blocks%s",
            what, k, n, n ? 100.0 * over1px / n : 0, n ? 100.0 * overQuarter / n : 0, n ? 100.0 * overOne / n : 0, n ? sum / n : 0, player >= 0 ? "" : "\n");
    if (player >= 0)
    {
        fprintf(stderr, "; player %.3f\n", player);
    }
}

// The same K ticks from one state, once as is and once with a different random stream: how far apart the cars and
// peds end up, i.e. how fast a client's own simulation of other entities drifts from the server's
void Diverge(const std::vector<Block>& blocks)
{
    std::vector<unsigned char> s0;
    SaveWorld(blocks, s0);
    Ped* playerPed = gGame_0x40_67E008->field_4_players[0]->field_2C4_player_ped;
    const u32 playerId = playerPed ? playerPed->field_200_id : 0;
    const int ks[] = {1, 3, 10, 30, 90};
    for (int ki = 0; ki < 5; ki++)
    {
        std::vector<Pos> c1, p1, c2, p2;
        for (int pass = 0; pass < 2; pass++)
        {
            RestoreWorld(blocks, s0);
            if (pass == 1)
            {
                PortRandSetState(PortRandState() ^ 0x5A5A5A5A);
            }
            for (int t = 0; t < ks[ki]; t++)
            {
                Tick();
            }
            Positions(pass ? c2 : c1, pass ? p2 : p1);
        }
        Compare("cars", ks[ki], c1, c2, 0);
        Compare("peds", ks[ki], p1, p2, playerId);
    }
    RestoreWorld(blocks, s0);
}

// Who draws random numbers: the sim tick against the sound service, and whether a resimulation with sound draws the same
void RandUse(int ticks)
{
    unsigned sim = 0, sound = 0;
    for (int t = 0; t < ticks; t++)
    {
        unsigned c = gPortRandCalls;
        Tick();
        sim += gPortRandCalls - c;
        c = gPortRandCalls;
        gRoot_sound_66B038.Service_40EFA0();
        sound += gPortRandCalls - c;
    }
    fprintf(stderr, "neteval rand: per tick %.1f draws in the sim, %.2f in the sound service\n", (double)sim / ticks, (double)sound / ticks);
}

// Milestone S (docs/netcode.md): the same 30 ticks twice from one state, both with the sound service, with sound's
// random numbers on their own stream: the world outside the sound layer must come out the same, and the heap intact
void SoundRollback(const std::vector<Block>& blocks)
{
    PortRandSound_Separate(true);
    std::vector<unsigned char> s0, w1, w2;
    SaveWorld(blocks, s0);
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass)
        {
            RestoreWorld(blocks, s0);
        }
        for (int t = 0; t < 30; t++)
        {
            Tick();
            gRoot_sound_66B038.Service_40EFA0();
        }
        SaveWorld(blocks, pass ? w2 : w1);
    }
    size_t n = 0, differ = 0;
    std::string names;
    for (size_t i = 0; i < blocks.size(); i++)
    {
        const std::string name = blocks[i].name;
        const bool sound = name.find("ound") != std::string::npos; // gSound_obj, gRoot_sound, the SoundPosEntry pool
        if (!sound && memcmp(&w1[n], &w2[n], blocks[i].size))
        {
            differ++;
            names += " " + name;
        }
        n += blocks[i].size;
    }
    fprintf(stderr, "neteval soundrollback: 2 x 30 ticks with sound, own sound stream: %zu blocks outside sound differ%s\n", differ, names.c_str());
    PortRandSound_Separate(false);
}

// Can one object be advanced on its own (projectile rewind)? Rockets are fired around the player; then, per moving
// object, one full tick against that object's PoolUpdate alone, from the same state
void Single(const std::vector<Block>& blocks)
{
    Ped* ped = gGame_0x40_67E008->field_4_players[0]->field_2C4_player_ped;
    if (!ped)
    {
        return;
    }
    for (int i = 0; i < 16; i++)
    {
        const Ang16 a((s16)(i * 90), (u8)0);
        gObject_5C_6F8F84->NewMovingObj_52A240(objects::rocket_bullet_128, ped->get_cam_x() + Fix16(1), ped->get_cam_y(), ped->field_1AC_cam.z + Fix16(0.5f),
                                               a, a, Fix16(0.1f), Fix16(0), Fix16(0));
    }
    Tick();
    Tick();
    std::vector<Object*> moving;
    for (Object* o = gObject_2C_Pool_6F8F80->field_4_pPrev; o; o = o->mpNext)
    {
        if (o->field_4_sprite && o->field_8_definition)
        {
            moving.push_back(o);
        }
    }
    std::vector<unsigned char> s0;
    SaveWorld(blocks, s0);
    int same = 0, samePos = 0, moved = 0, n = 0;
    for (size_t i = 0; i < moving.size(); i++)
    {
        Object* o = moving[i];
        const Fix16 x0 = o->field_4_sprite->field_14_xy.x, y0 = o->field_4_sprite->field_14_xy.y;
        Tick();
        bool alive = false;
        for (Object* p = gObject_2C_Pool_6F8F80->field_4_pPrev; p && !alive; p = p->mpNext)
        {
            alive = p == o;
        }
        if (!alive || !o->field_4_sprite)
        {
            RestoreWorld(blocks, s0);
            continue; // removed during the tick
        }
        std::vector<unsigned char> full((unsigned char*)o, (unsigned char*)o + sizeof(Object));
        const Fix16 x1 = o->field_4_sprite->field_14_xy.x, y1 = o->field_4_sprite->field_14_xy.y;
        RestoreWorld(blocks, s0);
        if (x1 == x0 && y1 == y0)
        {
            continue; // not moving
        }
        moved++;
        const bool removed = o->PoolUpdate();
        n++;
        same += !removed && !memcmp(full.data(), o, sizeof(Object));
        samePos += !removed && o->field_4_sprite->field_14_xy.x == x1 && o->field_4_sprite->field_14_xy.y == y1;
        RestoreWorld(blocks, s0);
    }
    fprintf(stderr, "neteval single: %zu objects, %d moving; PoolUpdate alone gives the full tick's object bytes for %d, its position for %d\n",
            moving.size(), moved, same, samePos);
}

// Explosions at random cars, then the rollback check again
void Explode(int count)
{
    unsigned seed = 777;
    std::vector<Car*> cars;
    for (Car* c = gCar_BC_Pool_67792C->field_0_pool.field_4_pPrev; c; c = c->mpNext)
    {
        cars.push_back(c);
    }
    for (int i = 0; i < count && !cars.empty(); i++)
    {
        seed = seed * 1103515245u + 12345u;
        Car* c = cars[(seed >> 8) % cars.size()];
        gObject_5C_6F8F84->CreateExplosion_52A3D0(c->field_50_car_sprite->field_14_xy.x, c->field_50_car_sprite->field_14_xy.y,
                                                  c->field_50_car_sprite->field_1C_zpos, Ang16((s16)0, (u8)0), explosion_type::small_18, 0);
    }
    for (int t = 0; t < 10; t++)
    {
        Tick();
    }
    fprintf(stderr, "neteval explode: %d explosions\n", count);
}

void PlayerList()
{
    Game* g = gGame_0x40_67E008;
    fprintf(stderr, "neteval players: %d (local %d):", (int)g->field_23_num_players, (int)g->field_24_local_player_idx);
    for (u32 i = 0; i < g->field_23_num_players; i++)
    {
        Player* p = g->field_4_players[i];
        Ped* ped = p ? p->field_2C4_player_ped : NULL;
        if (ped)
        {
            fprintf(stderr, " [%u %s at %.1f,%.1f]", i, p->field_8E_in_use ? "in use" : "unused", ped->get_cam_x().ToFloat(), ped->get_cam_y().ToFloat());
        }
        else
        {
            fprintf(stderr, " [%u %s, no ped]", i, p && p->field_8E_in_use ? "in use" : "unused");
        }
    }
    fprintf(stderr, "\n");
}
} // namespace

void NetEval(const std::vector<Block>& blocks, const char* what)
{
    PlayerList();
    const std::string w = std::string(",") + what + ",";
#define HAS(name) (w.find("," name ",") != std::string::npos)
    if (HAS("explode"))
    {
        Explode(20);
    }
    InterestCounts();
    if (HAS("rand"))
    {
        RandUse(300);
    }
    if (HAS("soundrollback"))
    {
        SoundRollback(blocks);
    }
    if (HAS("diverge"))
    {
        Diverge(blocks);
    }
    if (HAS("single"))
    {
        Single(blocks);
    }
    if (HAS("churn"))
    {
        Churn(300);
    }
#undef HAS
}
