// Port/net (docs/netcode.md, "Cars carry their whole physics"): a car as a snapshot entity, and the server's state
// put into this game's car: its place, its whole physics, its damage, lights, horn and colour.
#include "internal.hpp"
#include "members.hpp"
#include "Source/Car.hpp"
#include "Source/CarPhysics.hpp"
#include "Source/SpriteGrid.hpp"
#include "Source/explosion_type.hpp"
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
    PM_POINT(field_0_vel_read_only), PM(field_8_damaged_until_cycle, 4, false), PM_POINT(field_10_last_skid_pos[0]),
    PM_POINT(field_10_last_skid_pos[1]), PM_POINT(field_10_last_skid_pos[2]), PM_POINT(field_10_last_skid_pos[3]),
    PM_POINT(field_30_cm1), PM_POINT(field_38_cp1), PM_POINT(field_40_linvel_1), PM_POINT(field_48_force_accum),
    PM_POINT(field_50_linear_accel), PM(field_58_theta, 2, true), PM(field_60_gas_pedal, 4, true),
    PM(field_64_brake_pressure, 4, true), PM(field_68_air_z_vel, 4, true), PM(field_6C_cp3, 4, true), PM(field_70_z_vel, 4, true),
    PM(field_74_ang_vel_rad, 4, true), PM(field_78_pointing_ang_rad, 4, true), PM(field_7C_torque_accum, 4, true),
    PM(field_80_angular_accel, 4, true), PM(field_84_front_skid, 4, true), PM(field_88_rear_skid, 4, true), PM(field_8C_state, 4, true),
    PM(field_90_timer_since_last_move, 1, false), PM(field_91_is_foot_brake_on, 1, true), PM(field_92_is_hand_brake_on, 1, true),
    PM(field_93_is_forward_gas_on, 1, true), PM(field_94_is_backward_gas_on, 1, true), PM(field_95_is_coasting, 1, true),
    PM(field_98_surface_type, 4, true), PM(field_9C_block_spec, 4, true), PM(field_A0_oil_spin_dir, 4, true),
    PM(field_A4_oil_spin_timer, 1, true), PM(field_A5_current_slope_length, 1, false), PM(field_A6_current_slope_left_tiles, 1, true),
    PM(field_A7_current_tile_z, 1, false), PM(field_A8_hand_brake_force, 1, true), PM(field_A9_car_model, 1, true),
    PM(field_AA_sbw, 1, true), PM(field_AB_tpa, 1, true), PM(field_AC_drive_wheels_locked_q, 1, false),
    PM(field_AD_turn_direction, 1, true)};
#undef PM_POINT
#undef PM
const size_t kPhysicsCount = sizeof(kPhysics) / sizeof(kPhysics[0]);
// field_38_cp1.x: 0 only for a car without physics (nobody is at x 0)
const int kCp1X = net::engine::MemberIndex(kPhysics, kPhysicsCount, offsetof(CarPhysics, field_38_cp1.x));
static_assert(net::engine::kCarPhysics + sizeof(kPhysics) / sizeof(kPhysics[0]) <= net::kMaxFields, "a car's fields");

bool HasPhysics(const net::EntityState& e)
{
    return e.f[net::engine::kCarPhysics + kCp1X] != 0;
}
} // namespace

int net::engine::gExplosionsCarried, net::engine::gWrecksReplaced;

void net::engine::CarPhysicsFields(std::vector<FieldDesc>& out)
{
    AppendFields(kPhysics, kPhysicsCount, out);
}

void net::engine::CaptureCar(Car* c, Snapshot& out)
{
    EntityState e = Entity(kCarIdBase + (uint32_t)c->field_6C_car_id, kEntityCar);
    PutPosition(e, c->field_50_car_sprite);
    e.f[kCarModel] = c->field_84_car_info_idx;
    e.f[kCarDriver] = !c->field_54_driver ? kDriverNone : IsSpareDriver(c->field_54_driver) ? kDriverTraffic : kDriverPed;
    e.f[kCarDamage] = c->field_74_damage;
    e.f[kCarAreas] = (int32_t)c->field_8_damaged_areas.m_var;
    e.f[kCarWrecked] = IsWrecked(c);
    e.f[kCarLights] = (u8)c->field_A4_light_flags;
    e.f[kCarFlash] = (u8)c->field_A5_flash_phase_counter;
    e.f[kCarHorn] = c->field_A7_horn;
    e.f[kCarHornCooldown] = c->field_A8_horn_cooldown;
    e.f[kCarRemap] = c->field_50_car_sprite->get_remap_41C1F0();
    CaptureCarAi(c, e);
    if (const CarPhysics* m = c->field_58_physics)
    {
        CaptureMembers(m, kPhysics, kPhysicsCount, e, kCarPhysics);
    }
    out.entities.push_back(e);
}

// A car or a ped moved and turned at once. The collision grid keeps a sprite in the rows of its collision rect as it
// was added (SpriteGrid::RemoveFromRegionBuckets_477B60 takes it out of the rows of the stored rect, and
// AddToRegionBuckets_477B20 computes a new one): so position and rotation both change between the two. Turning a
// sprite while it is in the grid, as Car::SetPosition_443D00 followed by a turn did, left it in rows its next
// removal missed; once it was freed and reused (a roof light, a particle), a collision query crashed on it.
// Only a car in the collision grid is moved: one that is going away (despawning_5, despawned_6: its last update
// left it out of the grid, and freeing it doesn't look there) or carried by something (deactivated_7: in grid 3,
// Car::Deactivate_43AA60, its carrier moves it) would be put into the grid by the move and left there when freed.
void net::engine::MoveCar(Car* car, const Fix16& x, const Fix16& y, const Fix16& z, const Ang16& rot)
{
    const s32 status = car->field_88_despawn_status;
    if (status == car_despawn_status::despawning_5 || status == car_despawn_status::despawned_6 || status == car_despawn_status::deactivated_7)
    {
        return;
    }
    Sprite* s = car->field_50_car_sprite;
    gSpriteGrid_1_679208->RemoveFromRegionBuckets_477B60(s);
    s->set_xyz_lazy_420600(x, y, z);
    s->set_ang_lazy_420690(rot);
    if (CarPhysics* m = car->field_58_physics)
    {
        const Fix16 steer = m->field_78_pointing_ang_rad; // SetSprite_563560 zeroes the steering
        m->SetSprite_563560(s);
        m->field_78_pointing_ang_rad = steer;
    }
    gSpriteGrid_1_679208->AddToRegionBuckets_477B20(s);
}

void net::engine::SetCarPhysics(Car* car, const EntityState& e)
{
    if (CarPhysics* m = car->field_58_physics)
    {
        if (HasPhysics(e)) // none on the server: this game's goes on
        {
            ApplyMembers(m, kPhysics, kPhysicsCount, e, kCarPhysics);
        }
    }
}

void net::engine::DebugCarDiff(Car* car, const EntityState& e, uint32_t tick)
{
    const CarPhysics* m = car ? car->field_58_physics : NULL;
    if (!Debug().carDiff || !m || !HasPhysics(e))
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

bool net::engine::IsWrecked(const Car* car)
{
    return car->field_A9_wreck_countdown || car->field_74_damage >= 32000 || car->field_9C_engine_status == car_engine_status::destroyed_6;
}

bool net::engine::SetCarState(Car* car, const EntityState& e)
{
    const bool there = e.f[kCarWrecked] != 0, here = IsWrecked(car);
    if (there && !here)
    {
        car->HandleCarExplosion_43D840(explosion_type::item_19); // as a car blows up from its damage (Car::AddDamage)
        gExplosionsCarried++;
        return true;
    }
    if (!there && here)
    {
        return false; // a wreck of this game's own (a collision predicted harder than the server's)
    }
    if (!there)
    {
        car->field_74_damage = (s16)e.f[kCarDamage];
        car->field_8_damaged_areas.m_var = (u32)e.f[kCarAreas];
    }
    // Its lights and horn: a siren switched with the horn key (Car::HandleSpecialInput_441800 counts the horn down) came
    // on again here after the server had it off
    car->field_A4_light_flags = (char_type)e.f[kCarLights];
    car->field_A5_flash_phase_counter = (char_type)e.f[kCarFlash];
    car->field_A7_horn = (u8)e.f[kCarHorn];
    car->field_A8_horn_cooldown = (u8)e.f[kCarHornCooldown];
    if (e.f[kCarRemap] >= 0 && e.f[kCarRemap] < 256)
    {
        car->SetCarRemap((u8)e.f[kCarRemap]); // only a model with remaps, only when it differs (and its trailer)
    }
    return true;
}
