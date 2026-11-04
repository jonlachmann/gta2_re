#include "twin_stick.hpp"
#include "Camera.hpp"
#include "Frontend.hpp"
#include "Game.hpp"
#include "Ped.hpp"
#include "Player.hpp"
#include "char.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "view.hpp"
#include <math.h>

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
// A released stick's aim holds for these steps, as its reticle fades.
const s32 kAimSteps = 10;
// The pad's reticle, in blocks from the ped.
const f32 kReticleDistance = 3.0f;
// AIM CAMERA: the look-ahead is this times the aim's offset on screen from the centre (the stick's
// deflection as a part of the half height), further while Shift or R3 is held, at most this many half
// heights (the player stays on screen); it follows by this part per step.
const f32 kLook = 0.45f;
const f32 kLookFurther = 2.5f;
const f32 kMaxLook = 0.6f;
const f32 kMaxLookFurther = 0.9f;
const f32 kLookFollow = 0.12f;

Char* spWalker; // the ped walking this step, along sMove
s32 sMove;
bool sMouse;
s32 sAim;
s32 sAimSteps;
f32 sStickX; // the right stick, screen-relative
f32 sStickY;
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

// A screen direction (y down) in the world: the CHINATOWN camera's turn undone.
s32 WorldAngle(f32 x, f32 y)
{
    const f32 yaw = PortView::Yaw(1.0f) * (3.14159265f / 720.0f);
    return Angle(cosf(yaw) * x + sinf(yaw) * y, cosf(yaw) * y - sinf(yaw) * x);
}

// From b to a, -720 to 719.
s32 Diff(s32 a, s32 b)
{
    return ((a - b) % 1440 + 2160) % 1440 - 720;
}

Ped* ControlledPed()
{
    Player* pPlayer = gGame_0x40 && !gFrontend && gGame_0x40->mNumPlayers == 1 ? gGame_0x40->mLocalPlayer : NULL;
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
} // namespace

void PedControls(Player* pPlayer, Ped* pPed)
{
    spWalker = NULL;
#if defined(GTA2_PORT_SDL)
    if (!pPed || pPed != OnFootPed())
    {
        return;
    }
    Char* pChar = pPed->mGameObject;
    const Sprite* pSprite = pPed->GetSprite();
    f32 mx, my, ax, ay;
    sMouse = port_TwinStickInput(&mx, &my, &ax, &ay);
    sStickX = ax;
    sStickY = ay;
    // The movement keys too: forward is up the screen.
    mx += pPlayer->mNowRightPressed - pPlayer->mNowLeftPressed;
    my += pPlayer->mNowDownPressed - pPlayer->mNowForwardPressed;
    const bool moving = mx * mx + my * my > 0.01f;
    const s32 move = moving ? WorldAngle(mx, my) : 0;
    if (sMouse)
    {
        f32 x, y;
        port_MousePos(&x, &y);
        PortView::ScreenToGround(x, y, ToF(pSprite->mZpos), &x, &y);
        sAim = Angle(x - ToF(pSprite->mXy.x), y - ToF(pSprite->mXy.y));
        sAimSteps = kAimSteps;
    }
    else if (ax || ay)
    {
        sAim = WorldAngle(ax, ay);
        sAimSteps = kAimSteps;
    }
    else if (sAimSteps > 0)
    {
        sAimSteps--;
    }
    const bool aiming = sAimSteps > 0;
    const s32 rotation = pChar->mRotation.rValue;
    s32 turn = aiming || moving ? Diff(aiming ? sAim : move, rotation) : 0;
    turn = sMouse ? turn : (turn > kTurnStep ? kTurnStep : (turn < -kTurnStep ? -kTurnStep : turn));
    const bool jumping = pChar->GetCharState() == Char_B4_state::Jumping_15;
    if (jumping && moving && pPlayer->mNowHandBrakeOrJumpPressed && pPlayer->mHandBrakeOrJumpChanged)
    {
        pChar->mRotation = Ang16(static_cast<s16>(move), 0);
    }
    // As the original's turn keys: none in a jump, and standing, none that calls off getting in a car.
    pPlayer->mTurnSpeed = Ang16(jumping || (!moving && pPed->IsPedGoingToEnterCar()) ? 0 : turn);
    pPlayer->mMoveDirection = Fix16(moving ? 1 : 0);
    if (moving)
    {
        pPed->CancelEnterCarObjective();
        spWalker = pChar;
        sMove = move;
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
    // The walk adds the turn to mRotation: it goes along sMove, and turns back to the facing at the end.
    const s32 turn = pChar->mpPed->GetTurnSpeed().rValue;
    *pFacing = Ang16(static_cast<s16>(pChar->mRotation.rValue + turn), 0);
    pChar->mRotation = Ang16(static_cast<s16>(sMove - turn), 0);
    return true;
}

void DrawReticle()
{
#if defined(GTA2_PORT_SDL)
    Ped* pPed = OnFootPed();
    if (!pPed || !gViewCamera || (!sMouse && !sAimSteps))
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
        const Sprite* pSprite = pPed->GetSprite();
        const Ang16 aim(sAim);
        f32 px, py;
        PortView::GroundToScreen(ToF(pSprite->mXy.x), ToF(pSprite->mXy.y), ToF(pSprite->mZpos), &px, &py);
        PortView::GroundToScreen(ToF(pSprite->mXy.x) + kReticleDistance * ToF(Ang16::sine(aim)),
                                 ToF(pSprite->mXy.y) + kReticleDistance * ToF(Ang16::cosine(aim)), ToF(pSprite->mZpos), &x, &y);
        // Closer in when zoomed in: within 3/4 of the half height from the ped.
        const f32 length = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
        const f32 k = length > 0.75f * gViewCamera->mScreenPxCenterY ? 0.75f * gViewCamera->mScreenPxCenterY / length : 1.0f;
        x = px + (x - px) * k;
        y = py + (y - py) * k;
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
        f32 sx = cx + sStickX * cy;
        f32 sy = cy + sStickY * cy;
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
    if (Looking())
    {
        x += Fix16(sLookX);
        y += Fix16(sLookY);
    }
#endif
}

bool Looking()
{
    return sLookX || sLookY;
}
} // namespace PortTwinStick

bool port_Driving()
{
    Ped* pPed = PortTwinStick::ControlledPed();
    return pPed && pPed->get_car();
}

int port_TwinStick()
{
    Ped* pPed = PortTwinStick::PlayerPed();
    return !pPed ? 0 : (pPed->get_car() ? 2 : (pPed->mGameObject ? 1 : 0));
}
