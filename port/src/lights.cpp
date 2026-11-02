#include "lights.hpp"
#include "Car.hpp"
#include "CarInfoTable.hpp"
#include "Explosion.hpp"
#include "Fix16_Point.hpp"
#include "Globals.hpp"
#include "Object_5C.hpp"
#include "ParticleManager.hpp"
#include "ang16.hpp"
#include "enums.hpp"
#include "explosion_type.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "3rdParty/GTA2Hax/d3ddll/d3ddll.hpp"
#include <string.h>

#if defined(GTA2_PORT_SDL)
void port_SpotLight(f32 dx, f32 dy, f32 cosOuter, f32 cosInner);
#endif

namespace PortLights
{
namespace
{
// A car sprite's delta 6 is its lit headlight, set as delta bit 6 and, mirrored, bit 23 (Car::SetHeadlightDeltas);
// delta 5 its brake light, bits 5 and 22. Damage on a corner clears them (Car::DamageArea).
// The deltas apply to rows of 256 pixels, as for the sprite masks.
const u8 kHeadlightDelta = 6;
const u8 kBrakeLightDelta = 5;
const u32 kHalogen = 0xFFFFDCA0;
const u32 kRed = 0xFFFF5038;
const u32 kFire = 0xFFFF9030;
u8 sPixels[256 * 256];
f32 sMinX, sMinY, sMaxX, sMaxY;

// A lit lamp's centre on the ground in world units, from the pixels the game's delta draws on the sprite.
bool LampPosition(Sprite* pSprite, u8 delta, bool mirrored, f32& x, f32& y)
{
    const u16 spriteIdx = gGtx_0x106C->GetSpriteTrueIndex(pSprite->mSpriteTypeEnum, pSprite->mTypeSpriteIdx);
    sprite_delta* pDelta = gGtx_0x106C->get_delta(spriteIdx, delta);
    const sprite_index* pIndex = gGtx_0x106C->get_sprite_index(spriteIdx);
    if (!pDelta)
    {
        return false;
    }
    const s32 size = 256 * pIndex->mHeight;
    memset(sPixels, 0, size);
    mirrored ? pDelta->ApplyMirroredToPixels(sPixels, pIndex->mWidth) : pDelta->ApplyToPixels(sPixels);
    s32 sumX = 0;
    s32 sumY = 0;
    s32 count = 0;
    for (s32 i = 0; i < size; i++)
    {
        if (sPixels[i])
        {
            sumX += i & 0xFF;
            sumY += i >> 8;
            count++;
        }
    }
    if (!count)
    {
        return false;
    }
    // From the sprite's centre to the world, as Car::GetDoorWorldPos places a door.
    const s8 px = static_cast<s8>(sumX / count - pIndex->mWidth / 2);
    const s8 py = static_cast<s8>(sumY / count - pIndex->mHeight / 2);
    Fix16_Point_POD offset;
    offset.SetXY(gPixelsToFix16.SignedPixelsToFix16(px), gPixelsToFix16.SignedPixelsToFix16(py));
    offset.RotateByAngle(pSprite->mRotation);
    x = (pSprite->mXy.x + offset.x).ToFloat();
    y = (pSprite->mXy.y + offset.y).ToFloat();
    return true;
}
// One light as Light.cpp submits the game's: bit 16 on, radius in blocks * 32, intensity. A spot light along
// (dx, dy) when cosInner > -1.
void AddLight(f32 x, f32 y, f32 z, f32 radius, s32 intensity, u32 colour, f32 dx = 0.0f, f32 dy = 0.0f,
              f32 cosOuter = -2.0f, f32 cosInner = -1.0f)
{
    if (intensity <= 0 || x < sMinX || x > sMaxX || y < sMinY || y > sMaxY)
    {
        return;
    }
    SLight light;
    light.field_0 = 0x10000 | static_cast<u32>(radius * 32) << 8 | (intensity > 255 ? 255 : intensity);
    light.field_4_x = x;
    light.field_8_y = y;
    light.field_C_z = z;
    light.field_10_colour = colour;
#if defined(GTA2_PORT_SDL)
    pgbh_AddLight(&light);
    if (cosInner > -1.0f)
    {
        port_SpotLight(dx, dy, cosOuter, cosInner);
    }
#else
    // The original renderer has no spot lights: a point light a third of the range ahead.
    light.field_4_x += dx * radius / 3;
    light.field_8_y += dy * radius / 3;
    pgbh_AddLight(&light);
#endif
}

void CarLights(Car* pCar, bool headlights, bool effects)
{
    Sprite* pSprite = pCar->mCarSprite;
    if (!pSprite || pSprite->mZoom)
    {
        return;
    }
    // The sprite's rows run from the car's back to its front: forward is (0, 1) turned as RotateByAngle.
    const f32 fx = Ang16::sine(pSprite->mRotation).ToFloat();
    const f32 fy = Ang16::cosine(pSprite->mRotation).ToFloat();
    const f32 z = pSprite->mZpos.ToFloat();
    BitSet32& bits = pCar->mDamagedAreas;
    const s32 kHead[2] = {CarDeltaBitsEnum::FrontRightHeadlight_6, CarDeltaBitsEnum::FrontLeftHeadlight_23};
    const s32 kBrake[2] = {CarDeltaBitsEnum::BackRightBrakeLight_5, CarDeltaBitsEnum::BackLeftBrakeLight_22};
    for (s32 i = 0; i < 2; i++)
    {
        f32 x;
        f32 y;
        if (headlights && bits.mask_bit(kHead[i]) && LampPosition(pSprite, kHeadlightDelta, i == 1, x, y))
        {
            // The apex inside the car: the two beams overlap from the bumper on.
            AddLight(x - fx * 0.4f, y - fy * 0.4f, z, 5.0f, 190, kHalogen, fx, fy, 0.9f, 0.98f);
        }
        // A soft red pool behind a brake light while the game shows it lit (Car::UpdateBrakeLights).
        if (effects && bits.mask_bit(kBrake[i]) && LampPosition(pSprite, kBrakeLightDelta, i == 1, x, y))
        {
            AddLight(x, y, z, 1.5f, 150, kRed, -fx, -fy, 0.0f, 0.6f);
        }
    }
}

// Blasts flash over their first steps (the countdown starts at 100); fires glow while they burn. Molotov
// flames carry a light of the game's own (Weapon::throwable).
void ExplosionLight(const Explosion& explosion)
{
    if (!explosion.mpObj2C || !explosion.mpObj2C->mSprite)
    {
        return;
    }
    const Sprite* pSprite = explosion.mpObj2C->mSprite;
    const f32 x = pSprite->mXy.x.ToFloat();
    const f32 y = pSprite->mXy.y.ToFloat();
    const f32 z = pSprite->mZpos.ToFloat();
    switch (explosion.mType)
    {
        case explosion_type::fire_3:
        case explosion_type::car_fire_4:
        case explosion_type::car_fire_level1_12:
        case explosion_type::car_fire_level2_13:
        case explosion_type::car_fire_level3_14:
            AddLight(x, y, z, 2.5f, 160, kFire);
            break;
        case explosion_type::small_18:
        case explosion_type::item_19:
        case explosion_type::large_20:
        case explosion_type::building_45_22:
        case explosion_type::building_225_23:
        case explosion_type::building_135_24:
        case explosion_type::building_315_25:
        case explosion_type::no_ring_32:
        case explosion_type::small_33:
            AddLight(x, y, z, 6.0f, (explosion.mTimer - 25) * 255 / 60, kFire);
            break;
        default:
            break;
    }
}
} // namespace

void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y)
{
    const bool headlights = PortSettings::Headlights();
    const bool effects = PortSettings::LightEffects();
    if (!gGtx_0x106C || (!headlights && !effects))
    {
        return;
    }
    sMinX = min_x - 6.0f, sMinY = min_y - 6.0f, sMaxX = max_x + 6.0f, sMaxY = max_y + 6.0f;
    for (Car* pCar = gCar_BC_Pool ? gCar_BC_Pool->GetFirstCar_420E50() : NULL; pCar; pCar = pCar->mpNext)
    {
        CarLights(pCar, headlights, effects);
    }
    if (!effects)
    {
        return;
    }
    for (s32 i = 0; gExplosionPool_7A8 && i < 40; i++)
    {
        if (gExplosionPool_7A8->mbUsed[i])
        {
            ExplosionLight(gExplosionPool_7A8->mExplosions[i]);
        }
    }
    for (s32 i = 0; gExplosionPool_3D4 && i < 20; i++)
    {
        if (gExplosionPool_3D4->mbUsed[i])
        {
            ExplosionLight(gExplosionPool_3D4->mExplosions[i]);
        }
    }
    // Muzzle flashes (ParticleManager::GunMuzzelFlash): states 40 and 41, mSubState counting their few steps.
    for (Particle_4C* p = gParticle_4C_Pool ? gParticle_4C_Pool->GetFirstUsed() : NULL; p; p = p->mpNext)
    {
        if ((p->mState == 40 || p->mState == 41) && p->mpOwnSprite)
        {
            const Sprite* pSprite = p->mpOwnSprite;
            AddLight(pSprite->mXy.x.ToFloat(), pSprite->mXy.y.ToFloat(), pSprite->mZpos.ToFloat(), 2.0f,
                     220 - 45 * p->mSubState, kFire);
        }
    }
}
} // namespace PortLights
