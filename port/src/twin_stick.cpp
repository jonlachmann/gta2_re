#include "twin_stick.hpp"
#include "Camera.hpp"
#include "Car.hpp"
#include "Frontend.hpp"
#include "Game.hpp"
#include "Ped.hpp"
#include "Player.hpp"
#include "char.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "TextureCache.hpp"
#include "view.hpp"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "winmain.hpp"

#if defined(GTA2_PORT_SDL)
bool port_TwinStickInput(float* pMoveX, float* pMoveY, float* pAimX, float* pAimY);
void port_MousePos(float* pX, float* pY);
void port_DrawReticle(f32 x, f32 y, f32 size, f32 alpha);
bool port_LookFurther();
#endif

namespace PortTwinStick
{
namespace
{
// The most the stick turns the ped per step, in Ang16 units (24 degrees); the mouse turns it at once.
const s32 kTurnStep = 96;
// In a jump, the original's turn keys turn the ped by this much more per step, up to the most
// (kJumpTurnAccel and kMaxJumpTurnSpeed in Player::DoPedControlInputs).
const s32 kJumpTurnAccel = 4;
const s32 kMaxJumpTurn = 24;
// A roof gun's turn per step, as SPECIAL with left or right (Car::HandleRoofTurretRotation).
const s32 kTurretStep = 0x14;
// A released stick's aim holds for these steps, as its reticle fades.
const s32 kAimSteps = 10;
// The pad's reticle: from this part of the half screen height away from the ped at the dead zone's edge, out to
// as far as the screen's centre is from its edge (the mouse's reach) with the stick pushed fully, by the deflection
// to the power 1.5; it follows the stick by this part per step. The camera's look-ahead never changes the reach.
const f32 kMinReach = 0.2f;
const f32 kPadFollow = 0.5f;
// AIM CAMERA: the look-ahead is this times the aim's offset on screen from the centre (the stick's
// reticle's offset from the ped), further while Shift, LT or R3 is held, at most this many half
// heights (the player stays on screen); it follows by this part per step.
const f32 kLook = 0.45f;
const f32 kLookFurther = 2.5f;
const f32 kMaxLook = 0.6f;
const f32 kMaxLookFurther = 0.9f;
const f32 kLookFollow = 0.12f;
// A walking ped's sprite faces down, its trailing leg the rows above the torso no wider than this.
const s32 kLegWidth = 5;
// From this far off the facing to as far off its back, in Ang16 units (45 degrees), only the feet show, the trailing
// leg under the torso.
const s32 kFeetTurn = 180;
// The feet step this far ahead of and behind the hips, in texels, are this long and this far apart.
const f32 kStride = 7.0f;
const f32 kFootLength = 5.0f;
const f32 kFootApart = 1.5f;
// The hips: this part of the way down the torso from its back.
const f32 kHips = 0.35f;
// The front leg starts this many rows below the trailing one and is this many pixels from its shoe at most.
const s32 kFrontLegRow = 2;
const s32 kFrontLegGrow = 3;
// The largest walk frame drawn this way, in texels, and the most walk frame sizes with their front leg hidden.
const s32 kMaxFrame = 64;
const s32 kMaskedSizes = 16;

Char* spWalker; // the ped walking this step, along sMove
s32 sMove;
Char* spWalked; // the ped that walked along sMove this step
bool sMoving;
s32 sJumpTurn;
bool sBackward;
bool sMouse;
s32 sAim;
s32 sAimSteps;
f32 sPadX; // the pad's reticle on screen from the ped, in pixels
f32 sPadY;
f32 sLookX; // in blocks
f32 sLookY;

f32 ToF(Fix16 v)
{
    return v.mValue / 16384.0f;
}

// A world direction as an Ang16, as Fix16_Point::GetAngle_40ACD0.
s32 Angle(f32 x, f32 y)
{
    return Fix16::atan2_fixed(Fix16(y), Fix16(x)).rValue;
}

// From the screen's centre along (dx, dy) to its edge, less a margin.
f32 EdgeReach(f32 dx, f32 dy)
{
    const f32 x = static_cast<f32>(gViewCamera->mScreenPxCenterX);
    const f32 y = static_cast<f32>(gViewCamera->mScreenPxCenterY);
    const f32 margin = 0.05f * gViewCamera->mScreenPxHeight;
    const f32 w = static_cast<f32>(gViewCamera->mScreenPxWidth) - margin;
    const f32 h = static_cast<f32>(gViewCamera->mScreenPxHeight) - margin;
    const f32 tx = dx > 0 ? (w - x) / dx : (dx < 0 ? (margin - x) / dx : 1e9f);
    const f32 ty = dy > 0 ? (h - y) / dy : (dy < 0 ? (margin - y) / dy : 1e9f);
    return fmaxf(fminf(tx, ty), 0.0f);
}

// From b to a, -720 to 719.
s32 Diff(s32 a, s32 b)
{
    return ((a - b) % 1440 + 2160) % 1440 - 720;
}

Ped* ControlledPed()
{
    // Single player only: a network game's server gets the input bits alone, so it would neither see nor predict these
    // controls (also with --players 1)
    Player* pPlayer = gGame_0x40 && !gFrontend && gGame_0x40->mNumPlayers == 1 && !bStartNetworkGame ? gGame_0x40->mLocalPlayer : NULL;
    return pPlayer && !pPlayer->mDisableAllControls && pPlayer->mCameraMode == 0 ? pPlayer->mPlayerPed : NULL;
}

Ped* PlayerPed()
{
    return PortSettings::TwinStick() ? ControlledPed() : NULL;
}

Ped* OnFootPed()
{
    Ped* pPed = PlayerPed();
    return pPed && pPed->mGameObject && !pPed->get_car() ? pPed : NULL;
}

// The aim from pSprite this step, into sAim and sAimSteps: the mouse, the right stick, or the stick's last aim.
void Aim(const Sprite* pSprite, f32 ax, f32 ay)
{
#if defined(GTA2_PORT_SDL)
    if (sMouse)
    {
        f32 x, y;
        port_MousePos(&x, &y);
        PortView::ScreenToGround(x, y, ToF(pSprite->mZpos), &x, &y);
        sAim = Angle(x - ToF(pSprite->mXy.x), y - ToF(pSprite->mXy.y));
        sAimSteps = kAimSteps;
    }
    else if ((ax || ay) && gViewCamera)
    {
        f32 x, y;
        PortView::GroundToScreen(ToF(pSprite->mXy.x), ToF(pSprite->mXy.y), ToF(pSprite->mZpos), &x, &y);
        const f32 m = sqrtf(ax * ax + ay * ay);
        const f32 t = fminf((m - 0.25f) / 0.75f, 1.0f);
        const f32 min = kMinReach * gViewCamera->mScreenPxCenterY;
        const f32 reach = fmaxf(EdgeReach(ax / m, ay / m), min);
        const f32 r = min + (reach - min) * t * sqrtf(t);
        sPadX += (ax / m * r - sPadX) * kPadFollow;
        sPadY += (ay / m * r - sPadY) * kPadFollow;
        PortView::ScreenToGround(x + sPadX, y + sPadY, ToF(pSprite->mZpos), &x, &y);
        sAim = Angle(x - ToF(pSprite->mXy.x), y - ToF(pSprite->mXy.y));
        sAimSteps = kAimSteps;
    }
    else if (sAimSteps > 0)
    {
        sAimSteps--;
    }
#endif
}

// The roof gun of the player's fire truck, tank or gun jeep.
AttachedSprite* Turret(Car* pCar)
{
    const s32 model = pCar->IsFireTruck() ? objects::water_cannon_114
        : (pCar->IsTank() ? objects::tank_cannon_148 : (pCar->IsGunJeep() ? 248 : 0));
    return model ? pCar->mAttachments.GetSpriteForModel(model) : NULL;
}

// The sprite the aim starts from: the player on foot, or his vehicle with a roof gun.
const Sprite* AimFrom()
{
    Ped* pPed = PlayerPed();
    Car* pCar = pPed ? pPed->get_car() : NULL;
    return pCar ? (Turret(pCar) ? pCar->mCarSprite : NULL) : (pPed && pPed->mGameObject ? pPed->GetSprite() : NULL);
}

struct Frame
{
    STexture* mpTexture;
    s32 mWidth;
    s32 mHeight;
    const u8* mpData;
};

Frame GetFrame(const Sprite* pSprite, s32 idx)
{
    const sprite_index* pIndex = gGtx_0x106C->get_sprite_index(gGtx_0x106C->GetSpriteTrueIndex(pSprite->mSpriteTypeEnum, idx));
    const Frame frame = {gTextureCache_15D8->GetSpriteTexture(pSprite->mSpriteTypeEnum, idx, pSprite->mPaletteType, pSprite->mRemap),
                         pIndex->mWidth,
                         pIndex->mHeight,
                         pIndex->mpData};
    return frame;
}

struct Box
{
    f32 u0;
    f32 v0;
    f32 u1;
    f32 v1;
};

// The frame's texels tex drawn on its texels at, the frame centred on the sprite in pV and turned by turn about its
// middle column on row hv.
void DrawPart(const Frame& frame, u32 flags, const Vert* pV, s32 count, const Box& at, const Box& tex, s32 turn, f32 hv)
{
    // The quad is flat: a texel's place on screen and in the world is linear in (u, v).
    const f32 du = 1.0f / (pV[1].u - pV[0].u);
    const f32 dv = 1.0f / (pV[3].v - pV[0].v);
    const f32 pu = (pV[0].u + pV[1].u) * 0.5f;
    const f32 pv = (pV[0].v + pV[3].v) * 0.5f;
    const f32 fu = (frame.mWidth - 0.3f) * 0.5f;
    const f32 fv = (frame.mHeight - 0.3f) * 0.5f;
    const f32 c = cosf(turn * (3.14159265f / 720.0f));
    const f32 s = -sinf(turn * (3.14159265f / 720.0f));
    Vert verts[8];
    for (s32 i = 0; i < 4; i++)
    {
        const bool right = i == 1 || i == 2;
        const f32 u = (right ? at.u1 : at.u0) - fu;
        const f32 v = (i < 2 ? at.v0 : at.v1) - hv;
        const f32 tu = (pu + u * c - v * s - pV[0].u) * du;
        const f32 tv = (pv + hv - fv + u * s + v * c - pV[0].v) * dv;
        for (s32 k = 0; k < count; k += 4)
        {
            Vert& out = verts[k + i];
            out = pV[k + i];
            out.x = pV[k].x + tu * (pV[k + 1].x - pV[k].x) + tv * (pV[k + 3].x - pV[k].x);
            out.y = pV[k].y + tu * (pV[k + 1].y - pV[k].y) + tv * (pV[k + 3].y - pV[k].y);
            out.z = pV[k].z + tu * (pV[k + 1].z - pV[k].z) + tv * (pV[k + 3].z - pV[k].z);
        }
        verts[i].u = right ? tex.u1 : tex.u0;
        verts[i].v = i < 2 ? tex.v0 : tex.v1;
    }
    pgbh_DrawQuad(flags, frame.mpTexture, verts, 255);
}

// A frame's trailing leg: the rows top to split above the first one wider than a leg (split), and the columns left to
// right of its top row with the pixels next to them below.
struct Leg
{
    s32 mTop;
    s32 mSplit;
    s32 mLeft;
    s32 mRight;
};

// False if the frame shows no leg, its split still found.
bool FindLeg(const Frame& frame, Leg* pLeg)
{
    const u8* pData = frame.mpData;
    s32 top = -1;
    s32 split = 0;
    for (; split < frame.mHeight; split++)
    {
        s32 n = 0;
        for (s32 x = 0; x < frame.mWidth; x++)
        {
            n += pData[split * 256 + x] != 0;
        }
        top = top < 0 && n ? split : top;
        if (n > kLegWidth)
        {
            break;
        }
    }
    pLeg->mSplit = split < frame.mHeight ? split : 0;
    if (top < 0 || split - top < 2 || split >= frame.mHeight)
    {
        return false;
    }
    s32 left = 0;
    while (!pData[top * 256 + left])
    {
        left++;
    }
    s32 right = left;
    while (right + 1 < frame.mWidth && pData[top * 256 + right + 1])
    {
        right++;
    }
    for (s32 y = top + 1; y < split; y++)
    {
        while (left > 0 && pData[y * 256 + left - 1])
        {
            left--;
        }
        while (right + 1 < frame.mWidth && pData[y * 256 + right + 1])
        {
            right++;
        }
    }
    pLeg->mTop = top;
    pLeg->mLeft = left;
    pLeg->mRight = right;
    return true;
}

// The frame's trailing leg turned by turn about hips, slid under the torso by slide texels, its last row stretched to the
// hips to stay under the torso.
void DrawLeg(const Frame& frame, const Leg& leg, u32 flags, const Vert* pV, s32 count, s32 turn, f32 hips, f32 slide)
{
    const f32 l = leg.mLeft - 0.5f;
    const f32 r = leg.mRight + 1.5f;
    const f32 t = leg.mTop - 0.5f;
    const f32 v = leg.mSplit - 0.5f;
    slide = fminf(slide, hips - v);
    const Box join = {l, v + slide, r, hips};
    const Box joinTex = {l, v, r, v};
    const Box strip = {l, t + slide, r, v + slide};
    const Box stripTex = {l, t, r, v};
    DrawPart(frame, flags, pV, count, join, joinTex, turn, hips);
    DrawPart(frame, flags, pV, count, strip, stripTex, turn, hips);
}

// A foot, a rounded box the colour of the shoe of the leg in frame, at ahead texels along the move (turn) from the hips
// (hv in the frame) and side texels across it.
void DrawFoot(const Frame& frame, const Leg& leg, u32 flags, const Vert* pV, s32 count, s32 turn, f32 hv, f32 ahead, f32 side)
{
    const f32 u = (frame.mWidth - 0.3f) * 0.5f + side;
    const f32 v = hv + ahead;
    const f32 h = kFootLength * 0.5f;
    // The shoe: the top texel of the leg's middle column.
    const f32 shoe = (leg.mLeft + leg.mRight + 1) * 0.5f;
    const Box tex = {shoe, leg.mTop + 0.5f, shoe, leg.mTop + 0.5f};
    const Box core = {u - 1.0f, v - h + 0.5f, u + 1.0f, v + h - 0.5f};
    const Box wide = {u - 1.25f, v - h + 1.0f, u + 1.25f, v + h - 1.0f};
    const Box tall = {u - 0.75f, v - h, u + 0.75f, v + h};
    DrawPart(frame, flags, pV, count, core, tex, turn, hv);
    DrawPart(frame, flags, pV, count, wide, tex, turn, hv);
    DrawPart(frame, flags, pV, count, tall, tex, turn, hv);
}

// The frame with the pixels set in hidden (kMaxFrame per row) cleared, in a texture of its own for each frame size.
Frame MaskFrame(const Frame& frame, u16 physPal, const bool* pHidden)
{
    struct Copy
    {
        s32 mWidth;
        s32 mHeight;
        STexture* mpTexture;
        u8 mData[kMaxFrame * 256];
    };
    static Copy sCopies[kMaskedSizes];
    Copy* pCopy = NULL;
    for (s32 i = 0; i < kMaskedSizes && !pCopy; i++)
    {
        Copy& copy = sCopies[i];
        if (!copy.mpTexture)
        {
            // Sprite pages are 256 texels wide, as the texture reads them.
            copy.mWidth = frame.mWidth;
            copy.mHeight = frame.mHeight;
            copy.mpTexture = pgbh_RegisterTexture(frame.mWidth, frame.mHeight, copy.mData, physPal, 0);
        }
        pCopy = copy.mWidth == frame.mWidth && copy.mHeight == frame.mHeight && copy.mpTexture ? &copy : NULL;
    }
    if (!pCopy)
    {
        return frame;
    }
    for (s32 y = 0; y < frame.mHeight; y++)
    {
        for (s32 x = 0; x < frame.mWidth; x++)
        {
            pCopy->mData[y * 256 + x] = pHidden[y * kMaxFrame + x] ? 0 : frame.mpData[y * 256 + x];
        }
    }
    // Also marks the texture to be uploaded again.
    pgbh_AssignPalette(pCopy->mpTexture, physPal);
    const Frame masked = {pCopy->mpTexture, frame.mWidth, frame.mHeight, pCopy->mData};
    return masked;
}
} // namespace

void PedControls(Player* pPlayer, Ped* pPed)
{
    spWalker = NULL;
    sMoving = false;
#if defined(GTA2_PORT_SDL)
    if (!pPed || pPed != OnFootPed())
    {
        return;
    }
    Char* pChar = pPed->mGameObject;
    f32 mx, my, ax, ay;
    sMouse = port_TwinStickInput(&mx, &my, &ax, &ay);
    // The movement keys too: forward is up the screen.
    mx += pPlayer->mNowRightPressed - pPlayer->mNowLeftPressed;
    my += pPlayer->mNowDownPressed - pPlayer->mNowForwardPressed;
    const bool moving = mx * mx + my * my > 0.01f;
    const s32 move = moving ? Angle(mx, my) : 0;
    Aim(pPed->GetSprite(), ax, ay);
    const bool aiming = sAimSteps > 0;
    const s32 rotation = pChar->mRotation.rValue;
    s32 turn = aiming || moving ? Diff(aiming ? sAim : move, rotation) : 0;
    turn = sMouse ? turn : (turn > kTurnStep ? kTurnStep : (turn < -kTurnStep ? -kTurnStep : turn));
    const bool jumping = pChar->GetCharState() == Char_B4_state::Jumping_15;
    if (jumping && moving && pPlayer->mNowHandBrakeOrJumpPressed && pPlayer->mHandBrakeOrJumpChanged)
    {
        pChar->mRotation = Ang16(static_cast<s16>(move), 0);
    }
    // In a jump the move steers as the original's turn keys do there, without turning past it.
    s32 jumpTurn = 0;
    if (jumping && moving)
    {
        const s32 diff = Diff(move, pChar->mRotation.rValue);
        jumpTurn = sJumpTurn + (diff > 0 ? kJumpTurnAccel : -kJumpTurnAccel);
        jumpTurn = jumpTurn > kMaxJumpTurn ? kMaxJumpTurn : (jumpTurn < -kMaxJumpTurn ? -kMaxJumpTurn : jumpTurn);
        jumpTurn = (diff >= 0 && jumpTurn > diff) || (diff <= 0 && jumpTurn < diff) ? diff : jumpTurn;
    }
    sJumpTurn = jumpTurn;
    // Standing, no turn that calls off getting in a car.
    pPlayer->mTurnSpeed = Ang16(jumping ? jumpTurn : (!moving && pPed->IsPedGoingToEnterCar() ? 0 : turn));
    pPlayer->mMoveDirection = Fix16(moving ? 1 : 0);
    if (moving)
    {
        pPed->CancelEnterCarObjective();
        spWalker = pChar;
        sMove = move;
        sMoving = !jumping;
    }
#endif
}

bool Walk(Char* pChar, Ang16* pFacing)
{
    if (pChar != spWalker || pChar->mpPed != OnFootPed() || pChar->GetCharState() == Char_B4_state::Jumping_15)
    {
        return false;
    }
    spWalker = NULL;
    spWalked = pChar;
    // The walk adds the turn to mRotation: it goes along sMove, and turns back to the facing at the end.
    const s32 turn = pChar->mpPed->GetTurnSpeed().rValue;
    *pFacing = Ang16(static_cast<s16>(pChar->mRotation.rValue + turn), 0);
    pChar->mRotation = Ang16(static_cast<s16>(sMove - turn), 0);
    return true;
}

void WalkVelocity(Char* pChar)
{
    if (pChar == spWalked)
    {
        spWalked = NULL;
        Ang16 move(static_cast<s16>(sMove), 0);
        pChar->mVelocityVector.SetFromPolar(pChar->mVelocity, move);
    }
}

bool DrawLegs(const Sprite* pSprite, STexture*, u32 flags, const Vert* pVerts, s32 count)
{
    Ped* pPed = OnFootPed();
    const s32 anim = pPed ? pPed->mGameObject->mAnimationState : -1;
    if (!pPed || !sMoving || pSprite != pPed->GetSprite() || pSprite->mZoom ||
        (anim != Char_Anim_state::Walking_0 && anim != Char_Anim_state::Running_1))
    {
        return false;
    }
    // The walk and run frames: 0 to 15 from the body's first sprite, 37 to 52 with a weapon (Char::UpdateAnimState).
    const s32 idx = pSprite->mTypeSpriteIdx;
    const s32 body = idx >= 316 ? 316 : (idx >= 158 ? 158 : 0);
    const bool armed = idx - body >= 37;
    const s32 off = Diff(sMove, pSprite->mRotation.rValue);
    if (!off || idx - body - (armed ? 37 : 0) > 15)
    {
        return false;
    }
    // Walking backward the walk plays backward; the switch holds until 5 degrees past 135.
    const s32 side = abs(off);
    sBackward = side > 560 || (sBackward && side > 520);
    const s32 cycle = idx - (idx - body - (armed ? 37 : 0)) % 8;
    const s32 step = sBackward ? 7 - (idx - cycle) : idx - cycle;
    // As Hotline Miami's, seen from above: off the facing the feet step out from under the torso along the move, in time
    // with the walk; toward the back the trailing leg comes back up along the move.
    const Frame walk = GetFrame(pSprite, cycle + step);
    if (walk.mWidth > kMaxFrame || walk.mHeight > kMaxFrame)
    {
        return false;
    }
    Leg leg;
    const bool hasLeg = FindLeg(walk, &leg);
    const f32 hips = leg.mSplit + (walk.mHeight - leg.mSplit) * kHips;
    const f32 out = fminf(fminf(side, 720 - side) / static_cast<f32>(kFeetTurn), 1.0f);
    if (hasLeg && out < 1.0f)
    {
        const s32 turn = side < 360 ? off : off - (off > 0 ? 720 : -720);
        DrawLeg(walk, leg, flags, pVerts, count, turn, hips, (leg.mSplit - leg.mTop) * out);
    }
    // The feet the colour of the shoe of the cycle's longest leg.
    Frame shoe = walk;
    Leg shoeLeg = {0, 0, 0, 0};
    for (s32 i = 0; i < 8; i++)
    {
        const Frame frame = GetFrame(pSprite, cycle + i);
        Leg l;
        if (FindLeg(frame, &l) && l.mSplit - l.mTop > shoeLeg.mSplit - shoeLeg.mTop)
        {
            shoe = frame;
            shoeLeg = l;
        }
    }
    if (shoeLeg.mSplit > shoeLeg.mTop)
    {
        const f32 hv = hips - (walk.mHeight - 0.3f) * 0.5f + (shoe.mHeight - 0.3f) * 0.5f;
        const f32 ahead = kStride * out * sinf(step * (3.14159265f / 4.0f));
        DrawFoot(shoe, shoeLeg, flags, pVerts, count, off, hv, ahead, kFootApart);
        DrawFoot(shoe, shoeLeg, flags, pVerts, count, off, hv, -ahead, -kFootApart);
    }
    // The front leg, ahead of the hips: its shoe, in the colour of the longest trailing leg's foot, and the pixels in that
    // leg's colours next to it. It sinks under the torso from the foot up as the trailing leg does.
    static bool sHidden[kMaxFrame * kMaxFrame];
    static bool sNext[kMaxFrame * kMaxFrame];
    memset(sHidden, 0, sizeof(sHidden));
    bool shoeColour[256] = {};
    bool legColour[256] = {};
    for (s32 y = shoeLeg.mTop; y < shoeLeg.mSplit; y++)
    {
        for (s32 x = shoeLeg.mLeft; x <= shoeLeg.mRight; x++)
        {
            const u8 c = shoe.mpData[y * 256 + x];
            shoeColour[c] = shoeColour[c] || (c && y == shoeLeg.mTop);
            legColour[c] = legColour[c] || c;
        }
    }
    const s32 w = walk.mWidth;
    const s32 h = walk.mHeight;
    const s32 front = leg.mSplit + kFrontLegRow;
    for (s32 y = front; y < h; y++)
    {
        for (s32 x = 0; x < w; x++)
        {
            sHidden[y * kMaxFrame + x] = shoeColour[walk.mpData[y * 256 + x]];
        }
    }
    for (s32 i = 0; i < kFrontLegGrow; i++)
    {
        memcpy(sNext, sHidden, sizeof(sNext));
        for (s32 y = front; y < h; y++)
        {
            for (s32 x = 0; x < w; x++)
            {
                bool next = false;
                for (s32 n = 0; n < 9; n++)
                {
                    const s32 yy = y + n / 3 - 1;
                    const s32 xx = x + n % 3 - 1;
                    next = next || (yy >= front && yy < h && xx >= 0 && xx < w && sHidden[yy * kMaxFrame + xx]);
                }
                sNext[y * kMaxFrame + x] = sNext[y * kMaxFrame + x] || (next && legColour[walk.mpData[y * 256 + x]]);
            }
        }
        memcpy(sHidden, sNext, sizeof(sHidden));
    }
    s32 first = h;
    s32 last = -1;
    for (s32 y = front; y < h; y++)
    {
        for (s32 x = 0; x < w; x++)
        {
            first = sHidden[y * kMaxFrame + x] && y < first ? y : first;
            last = sHidden[y * kMaxFrame + x] ? y : last;
        }
    }
    const s32 cut = last < 0 ? h : static_cast<s32>(ceilf(last + 1 - (last + 1 - first) * out));
    for (s32 y = front; y < cut && y < h; y++)
    {
        memset(sHidden + y * kMaxFrame, 0, kMaxFrame);
    }
    const Frame torso = last < cut ? walk : MaskFrame(walk, gGtx_0x106C->get_phys_pal(pSprite->GetTruePalette()), sHidden);
    const Box rows = {0.0f, fmaxf(leg.mSplit - 0.5f, 0.0f), walk.mWidth - 0.3f, walk.mHeight - 0.3f};
    DrawPart(torso, flags, pVerts, count, rows, rows, 0, 0.0f);
    return true;
}

void AimTurret(Car* pCar)
{
#if defined(GTA2_PORT_SDL)
    AttachedSprite* pTurret = AimFrom() == pCar->mCarSprite ? Turret(pCar) : NULL;
    if (!pTurret)
    {
        return;
    }
    f32 mx, my, ax, ay;
    sMouse = port_TwinStickInput(&mx, &my, &ax, &ay);
    Aim(pCar->mCarSprite, ax, ay);
    if (sAimSteps > 0)
    {
        // At the original's turn speed. The fire truck's cannon fires backward from its sprite (Weapon.cpp).
        const s32 back = pCar->IsFireTruck() ? 720 : 0;
        const s32 turn = Diff(sAim - pCar->mCarSprite->mRotation.rValue - back, pTurret->mOffsetRot.rValue);
        pTurret->mOffsetRot += Ang16(turn > kTurretStep ? kTurretStep : (turn < -kTurretStep ? -kTurretStep : turn));
        pCar->mTurretRotated = turn != 0;
    }
#endif
}

bool FreeAim(const Ped* pPed)
{
    return pPed == OnFootPed();
}

void DrawReticle()
{
#if defined(GTA2_PORT_SDL)
    const Sprite* pSprite = AimFrom();
    if (!pSprite || !gViewCamera || (!sMouse && !sAimSteps))
    {
        return;
    }
    f32 x, y;
    if (sMouse)
    {
        port_MousePos(&x, &y);
    }
    else
    {
        PortView::GroundToScreen(ToF(pSprite->mXy.x), ToF(pSprite->mXy.y), ToF(pSprite->mZpos), &x, &y);
        x += sPadX;
        y += sPadY;
    }
    port_DrawReticle(x, y, 18.0f * ToF(gViewCamera->mUiScale), sMouse ? 1.0f : static_cast<f32>(sAimSteps) / kAimSteps);
#endif
}
void AimCamera(Camera* pCamera, Ped* pPed, Fix16& x, Fix16& y)
{
#if defined(GTA2_PORT_SDL)
    Player* pPlayer = gGame_0x40 ? gGame_0x40->mLocalPlayer : NULL;
    if (!pPlayer || pCamera != &pPlayer->mGameCamera || !gViewCamera)
    {
        return;
    }
    f32 wantX = 0.0f;
    f32 wantY = 0.0f;
    if (PortSettings::AimCamera() && pPed == OnFootPed())
    {
        // Measured on the screen, not from the aim's world point, which moves with the camera.
        const f32 z = ToF(pPed->GetSprite()->mZpos);
        const f32 cx = static_cast<f32>(gViewCamera->mScreenPxCenterX);
        const f32 cy = static_cast<f32>(gViewCamera->mScreenPxCenterY);
        f32 sx = sAimSteps ? cx + sPadX : cx;
        f32 sy = sAimSteps ? cy + sPadY : cy;
        if (sMouse)
        {
            port_MousePos(&sx, &sy);
        }
        f32 x0, y0, x1, y1, tx, ty;
        PortView::ScreenToGround(cx, cy, z, &x0, &y0);
        PortView::ScreenToGround(sx, sy, z, &x1, &y1);
        PortView::ScreenToGround(cx, 0.0f, z, &tx, &ty);
        const bool further = port_LookFurther();
        wantX = (x1 - x0) * (further ? kLookFurther : kLook);
        wantY = (y1 - y0) * (further ? kLookFurther : kLook);
        const f32 length = sqrtf(wantX * wantX + wantY * wantY);
        const f32 max = sqrtf((tx - x0) * (tx - x0) + (ty - y0) * (ty - y0)) * (further ? kMaxLookFurther : kMaxLook);
        wantX = length > max ? wantX * max / length : wantX;
        wantY = length > max ? wantY * max / length : wantY;
    }
    sLookX += (wantX - sLookX) * kLookFollow;
    sLookY += (wantY - sLookY) * kLookFollow;
    if (!wantX && !wantY && sLookX * sLookX + sLookY * sLookY < 1e-4f)
    {
        sLookX = sLookY = 0.0f;
    }
    if (sLookX || sLookY)
    {
        x += Fix16(sLookX);
        y += Fix16(sLookY);
    }
#endif
}
} // namespace PortTwinStick

bool port_Driving()
{
    Ped* pPed = PortTwinStick::ControlledPed();
    return pPed && pPed->get_car();
}

bool port_EnteringCar()
{
    Ped* pPed = PortTwinStick::ControlledPed();
    return pPed && !pPed->get_car() && pPed->IsPedGoingToEnterCar();
}

bool port_TwinStickAims()
{
    return PortTwinStick::AimFrom() != NULL;
}

int port_TwinStick()
{
    Ped* pPed = PortTwinStick::PlayerPed();
    return !pPed ? 0 : (pPed->get_car() ? 2 : (pPed->mGameObject ? 1 : 0));
}
