// Port/net (docs/netcode.md, "Cars"): a car's AI (CarAI: how a traffic or ped driver drives, routes, yields, chases)
// as the server has it. Without it a client's car decided its own way through every resimulation and each snapshot
// pulled it back (a chasing driver lurched). The plain values go member by member; the two cars it may refer to go as
// net ids; the nearest entity it caches stays local (it looks again).
#include "internal.hpp"
#include "Source/Car.hpp"
#include "Source/CarAI.hpp"
#include <stddef.h>
#include <string.h>

namespace
{
struct Member
{
    const char* name;
    size_t offset;
    uint8_t size;
    bool isSigned;
};
#define AM(member, sz, sg) {#member, offsetof(CarAI, member), sz, sg}
const Member kValues[] = {
    AM(field_8_maneuver_active, 1, true), AM(field_9_probe_x, 1, true), AM(field_A_probe_y, 1, true), AM(field_10_angle, 2, true),
    AM(field_14_speed, 4, true), AM(field_18_target_speed, 4, true), AM(field_1C_acceleration, 4, true), AM(field_24_flags, 4, false),
    AM(field_28_route_idx, 1, true), AM(field_2A_stopped_timer, 1, false), AM(field_2B_ticks_since_alloc, 1, false),
    AM(field_2C_yield_timer, 1, false), AM(field_2D_arrow_start, 1, true), AM(field_2E_arrow_cur, 1, true), AM(field_2F_arrow_end, 1, true),
    AM(field_30_forced_stop_timer, 1, false), AM(field_38_junction_turn_direction, 4, true), AM(field_3C_seeking_road, 4, true),
    AM(field_40_prev_target_direction, 4, true), AM(field_44_target_direction, 4, true), AM(field_48_probe_direction, 4, true),
    AM(field_4C_curr_direction, 4, true), AM(field_50_seek_road_turn, 4, true), AM(field_54_accel_cooldown, 2, false),
    AM(field_56_route_pos, 2, true), AM(field_58_off_road_timer, 2, false), AM(field_5A_obstacle_timer, 2, true), AM(field_74_max_speed, 4, true)};
#undef AM
const size_t kCount = sizeof(kValues) / sizeof(kValues[0]);
int gFirst = -1; // the car type's field of the first (set by CarAiFields)

int32_t Get(const CarAI* a, const Member& d)
{
    const unsigned char* p = (const unsigned char*)a + d.offset;
    if (d.size == 1)
    {
        return d.isSigned ? (int32_t) * (const int8_t*)p : (int32_t) * (const uint8_t*)p;
    }
    if (d.size == 2)
    {
        int16_t s;
        uint16_t u;
        memcpy(&s, p, 2);
        memcpy(&u, p, 2);
        return d.isSigned ? s : u;
    }
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

void Set(CarAI* a, const Member& d, int32_t v)
{
    unsigned char* p = (unsigned char*)a + d.offset;
    if (d.size == 1)
    {
        *p = (unsigned char)v;
    }
    else if (d.size == 2)
    {
        const int16_t s = (int16_t)v;
        memcpy(p, &s, 2);
    }
    else
    {
        memcpy(p, &v, 4);
    }
}
} // namespace

void net::engine::CarAiFields(std::vector<FieldDesc>& out)
{
    gFirst = (int)out.size();
    out.push_back(FieldDesc{"ai", 1, false}); // it has one
    for (size_t i = 0; i < kCount; i++)
    {
        out.push_back(FieldDesc{kValues[i].name, (uint8_t)(kValues[i].size * 8), kValues[i].isSigned});
    }
    out.push_back(FieldDesc{"ai_collided_car", 32, false});
    out.push_back(FieldDesc{"ai_yield_to_car", 32, false});
}

void net::engine::CaptureCarAi(const Car* car, EntityState& e)
{
    const CarAI* a = car->field_5C_AI;
    if (!a || gFirst < 0)
    {
        return;
    }
    e.f[gFirst] = 1;
    for (size_t i = 0; i < kCount; i++)
    {
        e.f[gFirst + 1 + i] = Get(a, kValues[i]);
    }
    e.f[gFirst + 1 + kCount] = a->field_68_car_in_collision ? (int32_t)(kCarIdBase + a->field_68_car_in_collision->field_6C_car_id) : 0;
    e.f[gFirst + 2 + kCount] = a->field_6C_yield_to_car ? (int32_t)(kCarIdBase + a->field_6C_yield_to_car->field_6C_car_id) : 0;
}

void net::engine::ApplyCarAi(Car* car, const EntityState& e)
{
    CarAI* a = car->field_5C_AI;
    if (!a || gFirst < 0 || !e.f[gFirst])
    {
        return; // an AI on one side only: each drives its own (a player's car has none)
    }
    // Its references first: one this game doesn't have keeps the AI as it is here, whole
    Car* refs[2] = {NULL, NULL};
    for (int k = 0; k < 2; k++)
    {
        const uint32_t id = (uint32_t)e.f[gFirst + 1 + kCount + k];
        if (id)
        {
            refs[k] = ReplicaCar(id);
            if (!refs[k] || !refs[k]->field_50_car_sprite)
            {
                return;
            }
        }
    }
    for (size_t i = 0; i < kCount; i++)
    {
        Set(a, kValues[i], e.f[gFirst + 1 + i]);
    }
    a->field_68_car_in_collision = refs[0];
    a->field_6C_yield_to_car = refs[1];
}
