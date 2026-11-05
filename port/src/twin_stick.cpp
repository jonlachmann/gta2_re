#include "twin_stick.hpp"
#include "Camera.hpp"
#include "Car.hpp"
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

Char* spWalker; // the ped walking this step, along sMove
s32 sMove;
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

// A screen direction (y down) in the world: the CHINATOWN camera's turn undone.
s32 WorldAngle(f32 x, f32 y)
{
    const f32 yaw = PortView::Yaw(1.0f) * (3.14159265f / 720.0f);
    return Angle(cosf(yaw) * x + sinf(yaw) * y, cosf(yaw) * y - sinf(yaw) * x);
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
    f32 mx, my, ax, ay;
    sMouse = port_TwinStickInput(&mx, &my, &ax, &ay);
    // The movement keys too: forward is up the screen.
    mx += pPlayer->mNowRightPressed - pPlayer->mNowLeftPressed;
    my += pPlayer->mNowDownPressed - pPlayer->mNowForwardPressed;
    const bool moving = mx * mx + my * my > 0.01f;
    const s32 move = moving ? WorldAngle(mx, my) : 0;
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

bool port_TwinStickAims()
{
    return PortTwinStick::AimFrom() != NULL;
}

int port_TwinStick()
{
    Ped* pPed = PortTwinStick::PlayerPed();
    return !pPed ? 0 : (pPed->get_car() ? 2 : (pPed->mGameObject ? 1 : 0));
}
