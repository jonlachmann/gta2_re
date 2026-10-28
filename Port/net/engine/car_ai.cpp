// Port/net (docs/netcode.md, "Entities"): a car's AI (CarAI: how a traffic or ped driver drives, routes, yields, chases)
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
    const CarAI* a = car->field_5C_AI;
    if (!a || gFirst < 0)
    {
        return;
    }
    e.f[gFirst] = 1;
    CaptureMembers(a, kValues, kCount, e, gFirst + 1);
    e.f[gFirst + 1 + kCount] = a->field_68_car_in_collision ? (int32_t)(kCarIdBase + a->field_68_car_in_collision->field_6C_car_id) : 0;
    e.f[gFirst + 2 + kCount] = a->field_6C_yield_to_car ? (int32_t)(kCarIdBase + a->field_6C_yield_to_car->field_6C_car_id) : 0;
}

void net::engine::ApplyCarAi(Car* car, const EntityState& e)
{
    if (gFirst < 0 || !e.f[gFirst])
    {
        return; // none on the server (a player's car): one here drives its own
    }
    if (!car->field_5C_AI)
    {
        // The server's has one (a cop got in: Car::InitCarAIControl_440590), so this one gets one too: its driver's
        // objective drives through it (Ped::SetupCarFollowTargetPed_469E50)
        car->field_5C_AI = gCarAI_78_Pool_677CF8->Allocate();
        if (!car->field_5C_AI)
        {
            return;
        }
        car->field_5C_AI->SetCar_453BF0(car);
    }
    CarAI* a = car->field_5C_AI;
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
    ApplyMembers(a, kValues, kCount, e, gFirst + 1);
    a->field_68_car_in_collision = refs[0];
    a->field_6C_yield_to_car = refs[1];
}
