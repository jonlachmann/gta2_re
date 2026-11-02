#include "view.hpp"
#include "Camera.hpp"
#include "Car.hpp"
#include "CarPhysics.hpp"
#include "Fix16_Point.hpp"
#include "Game.hpp"
#include "Ped.hpp"
#include "Player.hpp"
#include "ang16.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include <math.h>
#include <string.h>

#if defined(GTA2_PORT_SDL)
void port_SetViewTurn(bool on, f32 cx, f32 cy, f32 c, f32 s, f32 tx, f32 ty);
#endif

namespace PortView
{
namespace
{
// The yaw in 1/64 Ang16 units, so that the smoothing converges in integers. 0 is north up; a car points up
// the screen at its rotation + 180 degrees.
const s32 kSub = 64;
const s32 kTurn = 1440 * kSub;
const s32 kHalfTurn = kTurn / 2;
// Per step, out of 256, of the angle left: slow at rest, faster with speed, fast after getting in.
const s32 kFollowSlow = 15;
const s32 kFollowFast = 36;
const s32 kFollowRecentre = 48;
const s32 kMaxStep = 6 * 4 * kSub;
// The speed (Fix16, as the camera's look-ahead measures it) from which it follows at kFollowFast.
const s32 kFastSpeed = 4 * 16384;
// Backing up this many steps turns the view round (Chinatown Wars' Auto Reverse).
const s32 kReverseSteps = 40;
// The farthest the subject is drawn from the screen's centre, in half screen heights.
const f32 kMaxLead = 0.5f;

s32 sYaw;
s32 sPrevYaw;
bool sValid;
bool sRecentre;
s32 sReverse;
Car* spCar;
f32 sStepCos = 1.0f;
f32 sStepSin;

bool sDrawn;
f32 sCos;
f32 sSin;
f32 sCx;
f32 sCy;
f32 sMarkX;
f32 sMarkY;
Camera* spCamera;
u8 sCameraBytes[sizeof(Camera)];

s32 Wrap(s32 v)
{
    return ((v % kTurn) + kTurn + kHalfTurn) % kTurn - kHalfTurn;
}

f32 Radians(f32 yaw)
{
    return yaw * (2.0f * 3.14159265f / kTurn);
}

f32 ToF(Fix16 v)
{
    return v.mValue / 16384.0f;
}

Player* ViewPlayer()
{
    return gGame_0x40 && gGame_0x40->mNumPlayers == 1 ? gGame_0x40->mDrawPlayer : NULL;
}

// Debug and watched-car cameras, and scripted scenes with the controls off: framed for a north-up view.
bool Scripted(const Player* pPlayer)
{
    return pPlayer->mCameraMode == 1 || pPlayer->mCameraMode == 3 || pPlayer->mInDebugCamMode || pPlayer->mDisableAllControls;
}

Ped* Subject(Player* pPlayer)
{
    return pPlayer->mCameraMode == 2 ? pPlayer->mAuxPed : pPlayer->mPlayerPed;
}

s32 CarTarget(Car* pCar, s32* pFollow)
{
    const Ang16 rotation = pCar->GetCarRotation();
    const Fix16_Point velocity = pCar->GetLinearVelocity();
    const long long forward = static_cast<long long>(velocity.x.mValue) * Ang16::sine(rotation).mValue +
        static_cast<long long>(velocity.y.mValue) * Ang16::cosine(rotation).mValue;
    const bool backing = pCar->mPhysics && pCar->mPhysics->is_backward_gas_on();
    sReverse = backing ? (forward < 0 && sReverse < kReverseSteps ? sReverse + 1 : sReverse) : 0;

    s32 speed = velocity.GetLength_41E260().mValue * 16;
    speed = speed < kFastSpeed ? speed : kFastSpeed;
    *pFollow = kFollowSlow + (kFollowFast - kFollowSlow) * (speed / 64) / (kFastSpeed / 64);
    return (rotation.rValue + 720 + (sReverse >= kReverseSteps ? 720 : 0)) * kSub;
}

Camera* ViewCamera()
{
    Player* pPlayer = ViewPlayer();
    return pPlayer ? &pPlayer->mViewCamera : NULL;
}

// The drawn camera moved so that the subject stays within kMaxLead of the centre in the turned view, and
// the Special 2 pan follows the screen's axes.
void Lead(Camera* pCamera)
{
    Player* pPlayer = ViewPlayer();
    Ped* pPed = Subject(pPlayer);
    Sprite* pSprite = pPed && (pPed->mCar || pPed->mGameObject) ? pPed->GetSprite() : NULL;
    const f32 scale = ToF(pCamera->mScreenScale.x);
    const f32 depth = ToF(pCamera->mCurPos.mZ) + 8.0f - (pSprite ? ToF(pSprite->mZpos) : 0.0f);
    if (Scripted(pPlayer) || !pSprite || depth < 0.5f || scale <= 0.0f)
    {
        return;
    }
    const f32 ox = ToF(pSprite->mXy.x - pCamera->mCurPos.mX) * scale / depth;
    const f32 oy = ToF(pSprite->mXy.y - pCamera->mCurPos.mY) * scale / depth;
    const f32 px = ToF(pCamera->mPanX) * scale / ToF(pCamera->mScreenScale.y);
    const f32 py = ToF(pCamera->mPanY) * scale / ToF(pCamera->mScreenScale.y);
    const f32 limit = kMaxLead * pCamera->mScreenPxHeight / 2;
    f32 lx = sCos * (ox + px) - sSin * (oy + py);
    f32 ly = sSin * (ox + px) + sCos * (oy + py);
    lx = lx < -limit ? -limit : (lx > limit ? limit : lx);
    ly = ly < -limit ? -limit : (ly > limit ? limit : ly);
    const f32 dx = sCos * ox - sSin * oy - (lx - px);
    const f32 dy = sSin * ox + sCos * oy - (ly - py);
    sMarkX = -(sCos * dx + sSin * dy);
    sMarkY = -(-sSin * dx + sCos * dy);
    pCamera->mCurPos.mX += Fix16(-sMarkX * depth / scale);
    pCamera->mCurPos.mY += Fix16(-sMarkY * depth / scale);
}

// The sprites drawn are those of the turned view's bounding box.
void WidenBounds(Camera* pCamera)
{
    WorldRect& bounds = pCamera->mViewBounds;
    const Fix16 x = pCamera->mCurPos.mX;
    const Fix16 y = pCamera->mCurPos.mY;
    const f32 hx = ToF(bounds.mRight - x > x - bounds.mLeft ? bounds.mRight - x : x - bounds.mLeft);
    const f32 hy = ToF(bounds.mBottom - y > y - bounds.mTop ? bounds.mBottom - y : y - bounds.mTop);
    const Fix16 turnedX(fabsf(sCos) * hx + fabsf(sSin) * hy);
    const Fix16 turnedY(fabsf(sSin) * hx + fabsf(sCos) * hy);
    bounds.mLeft = x - turnedX;
    bounds.mRight = x + turnedX;
    bounds.mTop = y - turnedY;
    bounds.mBottom = y + turnedY;
}
} // namespace

void Snapshot()
{
    sPrevYaw = sYaw;
}

void Clear()
{
    sValid = false;
    sYaw = 0;
    sPrevYaw = 0;
    spCar = NULL;
}

void Step()
{
    Player* pPlayer = ViewPlayer();
    if (!PortSettings::ChinatownCamera() || !pPlayer)
    {
        Clear();
        sStepCos = 1.0f;
        sStepSin = 0.0f;
        return;
    }
    s32 target = sYaw;
    s32 follow = kFollowFast;
    Car* pCar = NULL;
    if (Scripted(pPlayer))
    {
        target = 0;
    }
    else
    {
        Ped* pPed = Subject(pPlayer);
        pCar = pPed ? pPed->get_car() : NULL;
        if (pCar && pCar->mCarSprite)
        {
            sRecentre = sRecentre || pCar != spCar;
            target = CarTarget(pCar, &follow);
        }
        else
        {
            sReverse = 0;
        }
    }
    spCar = pCar;

    const s32 left = Wrap(target - sYaw);
    if (!sValid)
    {
        sYaw = target;
        sPrevYaw = target;
        sValid = true;
    }
    else
    {
        s32 step = static_cast<s32>(static_cast<long long>(left) * (sRecentre ? kFollowRecentre : follow) / 256);
        step = step < -kMaxStep ? -kMaxStep : (step > kMaxStep ? kMaxStep : step);
        sYaw = (sYaw + step + kTurn) % kTurn;
    }
    if (left < 10 * 4 * kSub && left > -10 * 4 * kSub)
    {
        sRecentre = false;
    }
    sStepCos = cosf(Radians(static_cast<f32>(sYaw)));
    sStepSin = sinf(Radians(static_cast<f32>(sYaw)));
}

f32 Yaw(f32 alpha)
{
    const f32 yaw = (sPrevYaw + Wrap(sYaw - sPrevYaw) * alpha) / kSub;
    return yaw < 0.0f ? yaw + 1440.0f : yaw;
}

void BeginDraw(f32 alpha)
{
    sDrawn = false;
    Camera* pCamera = ViewCamera();
    if (!sValid || !pCamera || !PortSettings::ChinatownCamera())
    {
        return;
    }
    const f32 yaw = Radians(Yaw(alpha) * kSub);
    sCos = cosf(yaw);
    sSin = sinf(yaw);
    sMarkX = 0.0f;
    sMarkY = 0.0f;
    memcpy(sCameraBytes, pCamera, sizeof(Camera));
    spCamera = pCamera;
    Lead(pCamera);
    pCamera->UpdateBoundaries();
    WidenBounds(pCamera);
    sCx = static_cast<f32>(pCamera->mScreenPxCenterX);
    sCy = static_cast<f32>(pCamera->mScreenPxCenterY);
    sDrawn = true;
}

void EndDraw()
{
    SetTurn(kOff);
    if (spCamera)
    {
        memcpy(spCamera, sCameraBytes, sizeof(Camera));
        spCamera = NULL;
    }
    sDrawn = false;
}

void SetTurn(Turn turn)
{
#if defined(GTA2_PORT_SDL)
    const bool on = sDrawn && turn != kOff;
    port_SetViewTurn(on, sCx, sCy, sCos, sSin, turn == kMarkers ? sMarkX : 0.0f, turn == kMarkers ? sMarkY : 0.0f);
#endif
}

void TurnPoint(Fix16& x, Fix16& y)
{
    if (sDrawn)
    {
        const f32 dx = ToF(x) - sCx;
        const f32 dy = ToF(y) - sCy;
        x = Fix16(sCx + sCos * dx - sSin * dy);
        y = Fix16(sCy + sSin * dx + sCos * dy);
    }
}
} // namespace PortView

void PortViewTurnedExtent(Fix16& width, Fix16& height)
{
    if (PortView::sDrawn)
    {
        const f32 w = PortView::ToF(width);
        const f32 h = PortView::ToF(height);
        width = Fix16(fabsf(PortView::sCos) * w + fabsf(PortView::sSin) * h);
        height = Fix16(fabsf(PortView::sSin) * w + fabsf(PortView::sCos) * h);
    }
}

void PortViewTurnPoint640(Fix16& x, Fix16& y)
{
    if (PortView::sValid)
    {
        const f32 dx = PortView::ToF(x) - 320.0f;
        const f32 dy = PortView::ToF(y) - 240.0f;
        x = Fix16(320.0f + PortView::sStepCos * dx - PortView::sStepSin * dy);
        y = Fix16(240.0f + PortView::sStepSin * dx + PortView::sStepCos * dy);
    }
}

void PortViewTurnSound(Fix16& x, Fix16& y)
{
    if (PortView::sValid)
    {
        const f32 fx = PortView::ToF(x);
        const f32 fy = PortView::ToF(y);
        x = Fix16(PortView::sStepCos * fx - PortView::sStepSin * fy);
        y = Fix16(PortView::sStepSin * fx + PortView::sStepCos * fy);
    }
}
