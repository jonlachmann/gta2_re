#include "headlights.hpp"
#include "Camera.hpp"
#include "Car.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "ang16.hpp"
#include "gbh_graphics.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "winmain.hpp"
#include "3rdParty/GTA2Hax/d3ddll/d3ddll.hpp"

namespace PortHeadlights
{
namespace
{
// The game's draw flags (DMA's d3ddll): additive blending, colours per vertex.
const s32 kBlendAdd = 0x200;
const s32 kVertexDiffuse = 0x2000;

struct Frame
{
    f32 mX, mY, mZ; // the car's centre
    f32 mFx, mFy;   // forward
    f32 mRx, mRy;   // right
    f32 mHalfLength;
    f32 mHalfWidth;
};

bool GetFrame(Car* pCar, Frame& frame)
{
    if (!pCar->mCarSprite || !pCar->mDriver)
    {
        return false;
    }
    const Sprite* pSprite = pCar->mCarSprite;
    frame.mX = pCar->GetX().ToFloat();
    frame.mY = pCar->GetY().ToFloat();
    frame.mZ = pCar->GetZ().ToFloat();
    // The sprite's facing: angle 0 points up the screen (-y).
    frame.mFx = -Ang16::sine(pSprite->mRotation).ToFloat();
    frame.mFy = -Ang16::cosine(pSprite->mRotation).ToFloat();
    frame.mRx = frame.mFy;
    frame.mRy = -frame.mFx;
    frame.mHalfLength = pSprite->mpVisualShape ? pSprite->mpVisualShape->mHeight.ToFloat() / 2 : 0.5f;
    frame.mHalfWidth = pSprite->mpVisualShape ? pSprite->mpVisualShape->mWidth.ToFloat() / 2 : 0.25f;
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

// One lamp's beam: a cone on the ground, brightest along its axis near the lamp.
void DrawBeam(const Frame& frame, f32 side)
{
    const f32 kLength = 5.0f;
    const f32 kSpread = 0.3f; // half-width per block of length
    const f32 kPeak = 0.42f;
    const s32 kRings = 7;
    const s32 kColumns = 5;
    const f32 kAcross[kColumns] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    const f32 kProfile[kColumns] = {0.0f, 0.55f, 1.0f, 0.55f, 0.0f};

    const f32 lx = frame.mX + frame.mFx * frame.mHalfLength + frame.mRx * frame.mHalfWidth * 0.6f * side;
    const f32 ly = frame.mY + frame.mFy * frame.mHalfLength + frame.mRy * frame.mHalfWidth * 0.6f * side;
    // Slightly outwards, as real headlights are aimed.
    const f32 toe = 0.06f * side;
    const f32 dx = frame.mFx + frame.mRx * toe;
    const f32 dy = frame.mFy + frame.mRy * toe;

    Vert rings[kRings][kColumns];
    for (s32 r = 0; r < kRings; r++)
    {
        const f32 t = static_cast<f32>(r) / (kRings - 1);
        const f32 along = t * t * kLength; // rings closer together near the lamp
        const f32 halfWidth = 0.06f + along * kSpread;
        const f32 fade = 1.0f - along / kLength;
        // A short rise from the lamp, then the light falls off with distance.
        const f32 intensity = kPeak * fade * fade * (r == 0 ? 0.5f : 1.0f);
        for (s32 c = 0; c < kColumns; c++)
        {
            const f32 x = lx + dx * along + frame.mRx * halfWidth * kAcross[c];
            const f32 y = ly + dy * along + frame.mRy * halfWidth * kAcross[c];
            Project(x, y, frame.mZ, Colour(intensity * kProfile[c]), rings[r][c]);
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

void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y)
{
    if (!gCar_BC_Pool || PortSettings::Headlights() == 0)
    {
        return;
    }
    // Bit 16 switches the light on, the second byte is the radius (blocks * 32), the first the intensity.
    const bool beams = PortSettings::Headlights() == 2;
    const u32 kRadius = (beams ? 2 : 3) * 32;
    const u32 kIntensity = beams ? 110 : 160;
    const u32 kColour = 0xFFFFF2D8;
    for (Car* pCar = gCar_BC_Pool->GetFirstCar_420E50(); pCar; pCar = pCar->mpNext)
    {
        Frame frame;
        if (!GetFrame(pCar, frame))
        {
            continue;
        }
        if (frame.mX < min_x - 4 || frame.mX > max_x + 4 || frame.mY < min_y - 4 || frame.mY > max_y + 4)
        {
            continue;
        }
        const f32 ahead = beams ? frame.mHalfLength + 1.2f : 1.6f;
        for (s32 side = -1; side <= 1; side += 2)
        {
            SLight light;
            light.field_0 = 0x10000 | (kRadius << 8) | kIntensity;
            light.field_4_x = frame.mX + frame.mFx * ahead + frame.mRx * 0.35f * side;
            light.field_8_y = frame.mY + frame.mFy * ahead + frame.mRy * 0.35f * side;
            light.field_C_z = frame.mZ;
            light.field_10_colour = kColour;
            pgbh_AddLight(&light);
        }
    }
}

void DrawBeams(s32 zLayer)
{
    if (!gCar_BC_Pool || !gLighting || PortSettings::Headlights() != 2)
    {
        return;
    }
    for (Car* pCar = gCar_BC_Pool->GetFirstCar_420E50(); pCar; pCar = pCar->mpNext)
    {
        Frame frame;
        if (!GetFrame(pCar, frame) || static_cast<s32>(frame.mZ) != zLayer + 1)
        {
            continue;
        }
        DrawBeam(frame, -1.0f);
        DrawBeam(frame, 1.0f);
    }
}
} // namespace PortHeadlights
