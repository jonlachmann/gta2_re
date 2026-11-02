#include "headlights.hpp"
#include "Camera.hpp"
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
#include "winmain.hpp"
#include "3rdParty/GTA2Hax/d3ddll/d3ddll.hpp"
#include <string.h>

namespace PortHeadlights
{
namespace
{
// The game's draw flags (DMA's d3ddll): additive blending, colours per vertex.
const s32 kBlendAdd = 0x200;
const s32 kVertexDiffuse = 0x2000;

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

// A point on the ground at height z, projected as MapRenderer projects the tiles' tops.
void Project(f32 x, f32 y, f32 z, u32 colour, Vert& v)
{
    const f32 inv = 1.0f / (gViewCamera->mCurPos.mZ.ToFloat() - z + 8.0f);
    const f32 scale = inv * gViewCamera->mScreenScale.x.ToFloat();
    v.x = (x - gViewCamera->mCurPos.mX.ToFloat()) * scale + static_cast<u32>(gViewCamera->mScreenPxCenterX);
    v.y = (y - gViewCamera->mCurPos.mY.ToFloat()) * scale + static_cast<u32>(gViewCamera->mScreenPxCenterY);
    v.z = inv;
    v.w = inv;
    v.diff = colour;
    v.spec = 0;
    v.u = 0.0f;
    v.v = 0.0f;
}

u32 Colour(f32 intensity)
{
    const s32 i = static_cast<s32>(intensity * 255.0f);
    const s32 c = i < 0 ? 0 : (i > 255 ? 255 : i);
    return 0xFF000000u | (c << 16) | ((c * 235 / 255) << 8) | (c * 200 / 255);
}

// One lamp's beam along forward (fx, fy): a cone on the ground, brightest along its axis near the lamp.
void DrawBeam(f32 lx, f32 ly, f32 z, f32 fx, f32 fy, f32 side)
{
    const f32 kLength = 5.0f;
    const f32 kSpread = 0.3f; // half-width per block of length
    const f32 kPeak = 0.42f;
    const s32 kRings = 7;
    const s32 kColumns = 5;
    const f32 kAcross[kColumns] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    const f32 kProfile[kColumns] = {0.0f, 0.55f, 1.0f, 0.55f, 0.0f};

    const f32 ax = fy;
    const f32 ay = -fx;
    // Slightly outwards, as real headlights are aimed.
    const f32 dx = fx + ax * 0.06f * side;
    const f32 dy = fy + ay * 0.06f * side;

    Vert rings[kRings][kColumns];
    for (s32 r = 0; r < kRings; r++)
    {
        const f32 t = static_cast<f32>(r) / (kRings - 1);
        const f32 along = t * t * kLength; // rings closer together near the lamp
        const f32 halfWidth = 0.04f + along * kSpread;
        const f32 fade = 1.0f - along / kLength;
        // A short rise from the lamp, then the light falls off with distance.
        const f32 intensity = kPeak * fade * fade * (r == 0 ? 0.5f : 1.0f);
        for (s32 c = 0; c < kColumns; c++)
        {
            const f32 x = lx + dx * along + ax * halfWidth * kAcross[c];
            const f32 y = ly + dy * along + ay * halfWidth * kAcross[c];
            Project(x, y, z, Colour(intensity * kProfile[c]), rings[r][c]);
        }
    }
    for (s32 r = 0; r + 1 < kRings; r++)
    {
        for (s32 c = 0; c + 1 < kColumns; c++)
        {
            Vert quad[8];
            quad[0] = rings[r][c];
            quad[1] = rings[r][c + 1];
            quad[2] = rings[r + 1][c + 1];
            quad[3] = rings[r + 1][c];
            pgbh_DrawQuad(kBlendAdd | kVertexDiffuse, NULL, quad, 0);
        }
    }
}
} // namespace

void DrawBeams(s32 zLayer)
{
    if (!gCar_BC_Pool || !gGtx_0x106C || !gLighting || !PortSettings::Headlights())
    {
        return;
    }
    for (Car* pCar = gCar_BC_Pool->GetFirstCar_420E50(); pCar; pCar = pCar->mpNext)
    {
        Sprite* pSprite = pCar->mCarSprite;
        if (!pSprite || pSprite->mZoom || static_cast<s32>(pSprite->mZpos.ToFloat()) != zLayer + 1)
        {
            continue;
        }
        // The sprite's rows run from the car's back to its front: forward is (0, 1) turned as RotateByAngle.
        const f32 fx = Ang16::sine(pSprite->mRotation).ToFloat();
        const f32 fy = Ang16::cosine(pSprite->mRotation).ToFloat();
        const s32 kBits[2] = {CarDeltaBitsEnum::FrontRightHeadlight_6, CarDeltaBitsEnum::FrontLeftHeadlight_23};
        for (s32 i = 0; i < 2; i++)
        {
            f32 x;
            f32 y;
            if (pCar->mDamagedAreas.mask_bit(kBits[i]) && LampPosition(pSprite, i == 1, x, y))
            {
                DrawBeam(x, y, pSprite->mZpos.ToFloat(), fx, fy, i == 0 ? 1.0f : -1.0f);
            }
        }
    }
}
} // namespace PortHeadlights
