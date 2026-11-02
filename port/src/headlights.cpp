#include "headlights.hpp"
#include "Car.hpp"
#include "CarInfoTable.hpp"
#include "Fix16_Point.hpp"
#include "Globals.hpp"
#include "ang16.hpp"
#include "enums.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "3rdParty/GTA2Hax/d3ddll/d3ddll.hpp"
#include <string.h>

#if defined(GTA2_PORT_SDL)
void port_SpotLight(f32 dx, f32 dy, f32 cosOuter, f32 cosInner);
#endif

namespace PortHeadlights
{
namespace
{
// A car sprite's delta 6 is its lit headlight, set as delta bit 6 and, mirrored, bit 23 (Car::SetHeadlightDeltas);
// front damage clears the bit (Car::DamageArea). The deltas apply to rows of 256 pixels, as for the sprite masks.
const u8 kHeadlightDelta = 6;
u8 sPixels[256 * 256];

// A lit lamp's centre on the ground in world units, from the pixels the game's delta draws on the sprite.
bool LampPosition(Sprite* pSprite, bool mirrored, f32& x, f32& y)
{
    const u16 spriteIdx = gGtx_0x106C->GetSpriteTrueIndex(pSprite->mSpriteTypeEnum, pSprite->mTypeSpriteIdx);
    sprite_delta* pDelta = gGtx_0x106C->get_delta(spriteIdx, kHeadlightDelta);
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
} // namespace

void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y)
{
    if (!gCar_BC_Pool || !gGtx_0x106C || !PortSettings::Headlights())
    {
        return;
    }
    // As the game's lights (Light.cpp): bit 16 on, radius in blocks * 32, intensity; a warm halogen colour.
    const u32 kLight = 0x10000 | (5 * 32) << 8 | 200;
    const u32 kColour = 0xFFFFDCA0;
    for (Car* pCar = gCar_BC_Pool->GetFirstCar_420E50(); pCar; pCar = pCar->mpNext)
    {
        Sprite* pSprite = pCar->mCarSprite;
        if (!pSprite || pSprite->mZoom)
        {
            continue;
        }
        // The sprite's rows run from the car's back to its front: forward is (0, 1) turned as RotateByAngle.
        const f32 fx = Ang16::sine(pSprite->mRotation).ToFloat();
        const f32 fy = Ang16::cosine(pSprite->mRotation).ToFloat();
        const s32 kBits[2] = {CarDeltaBitsEnum::FrontRightHeadlight_6, CarDeltaBitsEnum::FrontLeftHeadlight_23};
        for (s32 i = 0; i < 2; i++)
        {
            SLight light;
            if (!pCar->mDamagedAreas.mask_bit(kBits[i]) || !LampPosition(pSprite, i == 1, light.field_4_x, light.field_8_y) ||
                light.field_4_x < min_x - 6 || light.field_4_x > max_x + 6 || light.field_8_y < min_y - 6 || light.field_8_y > max_y + 6)
            {
                continue;
            }
            light.field_0 = kLight;
            light.field_C_z = pSprite->mZpos.ToFloat();
            light.field_10_colour = kColour;
#if defined(GTA2_PORT_SDL)
            pgbh_AddLight(&light);
            port_SpotLight(fx, fy, 0.9f, 0.98f);
#else
            // The original renderer has no spot lights: a point light ahead of the lamp.
            light.field_4_x += fx * 1.5f;
            light.field_8_y += fy * 1.5f;
            pgbh_AddLight(&light);
#endif
        }
    }
}
} // namespace PortHeadlights
