#include "lights.hpp"
#include "Car.hpp"
#include "CarInfoTable.hpp"
#include "Char_Pool.hpp"
#include "Explosion.hpp"
#include "Fix16_Point.hpp"
#include "Globals.hpp"
#include "Object_5C.hpp"
#include "ObjectPool.hpp"
#include "Ped.hpp"
#include "char.hpp"
#include "ParticleManager.hpp"
#include "ang16.hpp"
#include "enums.hpp"
#include "explosion_type.hpp"
#include "frame_loop.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "3rdParty/GTA2Hax/d3ddll/d3ddll.hpp"
#include <string.h>

#if defined(GTA2_PORT_SDL)
void port_LampLight(f32 dx, f32 dy, f32 width);
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
const u32 kElectric = 0xFF5080FF;
const u32 kExhaust = 0xFFFFD8A0;
u8 sPixels[256 * 256];
f32 sMinX, sMinY, sMaxX, sMaxY;

// Lamp centres in sprite pixels by delta, as LampPosition finds them; the delta's data identifies it.
struct LampCentre
{
    const void* mpData;
    u16 mLen;
    bool mMirrored;
    bool mFound;
    s8 mX;
    s8 mY;
};
LampCentre sLampCentres[512];

bool FindLampCentre(const sprite_delta* pDelta, const sprite_index* pIndex, bool mirrored, s8& px, s8& py)
{
    LampCentre& c = sLampCentres[((uintptr_t)pDelta->mpData / 4 * 2 + mirrored) % 512];
    if (c.mpData != pDelta->mpData || c.mLen != pDelta->mLen || c.mMirrored != mirrored)
    {
        const s32 size = 256 * pIndex->mHeight;
        memset(sPixels, 0, size);
        sprite_delta* pApply = const_cast<sprite_delta*>(pDelta);
        mirrored ? pApply->ApplyMirroredToPixels(sPixels, pIndex->mWidth) : pApply->ApplyToPixels(sPixels);
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
        c.mpData = pDelta->mpData;
        c.mLen = pDelta->mLen;
        c.mMirrored = mirrored;
        c.mFound = count != 0;
        c.mX = count ? static_cast<s8>(sumX / count - pIndex->mWidth / 2) : 0;
        c.mY = count ? static_cast<s8>(sumY / count - pIndex->mHeight / 2) : 0;
    }
    px = c.mX;
    py = c.mY;
    return c.mFound;
}

// A lit lamp's centre on the ground in world units, from the pixels the game's delta draws on the sprite.
bool LampPosition(Sprite* pSprite, u8 delta, bool mirrored, f32& x, f32& y)
{
    const u16 spriteIdx = gGtx_0x106C->GetSpriteTrueIndex(pSprite->mSpriteTypeEnum, pSprite->mTypeSpriteIdx);
    sprite_delta* pDelta = gGtx_0x106C->get_delta(spriteIdx, delta);
    const sprite_index* pIndex = gGtx_0x106C->get_sprite_index(spriteIdx);
    s8 px;
    s8 py;
    if (!pDelta || !FindLampCentre(pDelta, pIndex, mirrored, px, py))
    {
        return false;
    }
    // From the sprite's centre to the world, as Car::GetDoorWorldPos places a door.
    Fix16_Point_POD offset;
    offset.SetXY(gPixelsToFix16.SignedPixelsToFix16(px), gPixelsToFix16.SignedPixelsToFix16(py));
    offset.RotateByAngle(pSprite->mRotation);
    x = (pSprite->mXy.x + offset.x).ToFloat();
    y = (pSprite->mXy.y + offset.y).ToFloat();
    return true;
}
// One light as Light.cpp submits the game's: bit 16 on, radius in blocks * 32, intensity. A beam along
// (dx, dy) of that length when width > 0 (port_LampLight).
void AddLight(f32 x, f32 y, f32 z, f32 radius, s32 intensity, u32 colour, f32 dx = 0.0f, f32 dy = 0.0f, f32 width = 0.0f)
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
    port_LampLight(dx, dy, width);
#else
    // The original renderer has no beams: a point light a third of the range ahead.
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
    // Lamps are on the car's sprite, well within two blocks of its centre.
    const f32 cx = pSprite->mXy.x.ToFloat();
    const f32 cy = pSprite->mXy.y.ToFloat();
    if (cx < sMinX - 2.0f || cx > sMaxX + 2.0f || cy < sMinY - 2.0f || cy > sMaxY + 2.0f)
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
            AddLight(x, y, z, 1.2f, 110, kHalogen, fx, fy, 0.06f);
        }
        // A small red glow behind a brake light while the game shows it lit (Car::UpdateBrakeLights).
        if (effects && bits.mask_bit(kBrake[i]) && LampPosition(pSprite, kBrakeLightDelta, i == 1, x, y))
        {
            AddLight(x, y, z, 0.45f, 105, kRed, -fx, -fy, 0.06f);
        }
    }
}

// Hashed value in [0, 1] for lattice point i of a noise.
f32 Hash(u32 seed, u32 i)
{
    u32 h = seed * 0x9E3779B1u ^ i * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (h & 0xFFFF) / 65535.0f;
}

// Smooth value noise in [0, 1] along t.
f32 Noise(u32 seed, f32 t)
{
    const u32 i = static_cast<u32>(t);
    const f32 f = t - i;
    const f32 a = Hash(seed, i);
    return a + (Hash(seed, i + 1) - a) * f * f * (3.0f - 2.0f * f);
}

// A flame's brightness at a logic step, 1 on average: slow and quick noises, now and then a dip or a flare.
f32 Flame(u32 seed, u32 step)
{
    f32 v = 0.5f * Noise(seed, step / 9.0f) + 0.3f * Noise(seed + 1, step / 4.0f) + 0.2f * Noise(seed + 2, step / 1.7f);
    const f32 event = Noise(seed + 3, step / 2.5f);
    v += event > 0.88f ? (event - 0.88f) * 4.0f : (event < 0.1f ? (event - 0.1f) * 4.0f : 0.0f);
    return 0.6f + 0.8f * v;
}

// Flame() between the last two logic steps, as the sprites are drawn.
f32 FlameNow(u32 seed)
{
    const u32 step = PortFrameLoop::StepCount();
    const f32 prev = Flame(seed, step - 1);
    return prev + (Flame(seed, step) - prev) * PortFrameLoop::DrawAlpha();
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
        {
            // Brightness, size, a little drift and orange to yellow, each on its own noise.
            const u32 seed = static_cast<u16>(explosion.mId) * 8u;
            const f32 k = FlameNow(seed);
            const f32 yellow = FlameNow(seed + 4) - 0.6f;
            const u32 colour = 0xFFFF0000u | static_cast<u32>(0x80 + 0x38 * yellow) << 8 | static_cast<u32>(0x20 + 0x18 * yellow);
            AddLight(x + (FlameNow(seed + 5) - 1.0f) * 0.3f, y + (FlameNow(seed + 6) - 1.0f) * 0.3f, z, 2.3f + 0.3f * k,
                     static_cast<s32>(160 * k), colour);
            break;
        }
        case explosion_type::small_18:
        case explosion_type::item_19:
        case explosion_type::large_20:
        case explosion_type::building_45_22:
        case explosion_type::building_225_23:
        case explosion_type::building_135_24:
        case explosion_type::building_315_25:
        case explosion_type::no_ring_32:
        case explosion_type::small_33:
            AddLight(x, y, z, 6.0f, static_cast<s32>((explosion.mTimer - 25) * 255 / 60 * (0.8f + 0.2f * FlameNow(explosion.mId))),
                     kFire);
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
    // Flamethrower flames (ParticleManager::EmitFlameStreamSegment): state 31, a dim flickering light each.
    // Electro gun arcs (ParticleManager::EmitElectricArcParticle): state 37, fading with mCounter.
    for (Particle_4C* p = gParticle_4C_Pool ? gParticle_4C_Pool->GetFirstUsed() : NULL; p; p = p->mpNext)
    {
        const Sprite* pSprite = p->mpOwnSprite;
        if (!pSprite)
        {
            continue;
        }
        if (p->mState == 40 || p->mState == 41)
        {
            AddLight(pSprite->mXy.x.ToFloat(), pSprite->mXy.y.ToFloat(), pSprite->mZpos.ToFloat(), 2.0f,
                     220 - 45 * p->mSubState, kFire);
        }
        else if (p->mState == 31)
        {
            AddLight(pSprite->mXy.x.ToFloat(), pSprite->mXy.y.ToFloat(), pSprite->mZpos.ToFloat(), 1.2f,
                     static_cast<s32>(25 * FlameNow(p->mId * 8u)), kFire);
        }
        else if (p->mState == 37 && p->mInitialCounter)
        {
            AddLight(pSprite->mXy.x.ToFloat(), pSprite->mXy.y.ToFloat(), pSprite->mZpos.ToFloat(), 1.2f,
                     static_cast<s32>(30 * FlameNow(p->mId * 8u) * p->mCounter / p->mInitialCounter), kElectric);
        }
    }
    // Rockets in flight; thrown molotovs carry a game light already (Weapon::throwable).
    for (Object* pObj = gObject_2C_Pool ? gObject_2C_Pool->GetFirstUsed() : NULL; pObj; pObj = pObj->mpNext)
    {
        if (pObj->mModel == objects::rocket_bullet_128 && pObj->mSprite)
        {
            const Sprite* pSprite = pObj->mSprite;
            AddLight(pSprite->mXy.x.ToFloat(), pSprite->mXy.y.ToFloat(), pSprite->mZpos.ToFloat(), 1.3f,
                     static_cast<s32>(90 * FlameNow(pObj->mId * 8u)), kExhaust);
        }
    }
    // Burning peds (Ped::SetOnFire attaches a fire_197 object, without a light).
    for (Ped* pPed = gPedPool ? gPedPool->mPool.GetFirstUsed() : NULL; pPed; pPed = pPed->mpNext)
    {
        const Sprite* pSprite = pPed->mGameObject ? pPed->mGameObject->mSpritePtr : NULL;
        if (pPed->IsOnFire() && pSprite)
        {
            AddLight(pSprite->mXy.x.ToFloat(), pSprite->mXy.y.ToFloat(), pSprite->mZpos.ToFloat(), 1.4f,
                     static_cast<s32>(70 * FlameNow(pPed->mId * 8u + 3)), kFire);
        }
    }
}
} // namespace PortLights
