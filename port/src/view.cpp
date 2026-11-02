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
void port_SetViewTurn(s32 kind, f32 cx, f32 cy, f32 c, f32 s, f32 tx, f32 ty, f32 cosP, f32 sinP, f32 scale, f32 markerDepth);
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
// STREETS: the target is the street axis (a multiple of 90 degrees) nearest the direction of travel, taken
// once the travel has been within kAxisSnap of a new one for kAxisSteps; the view then turns to it in one
// smootherstep of kTurnSteps[ROTATION SPEED] steps per quarter turn (0: at once).
const s32 kQuarter = 90 * 256;
const s32 kAxisSnap = 40 * 256;
const s32 kAxisSteps = 4;
const s32 kTurnSteps[] = {0, 36, 30, 25, 20, 16, 13, 11, 9, 7, 6};
// FOLLOW: the yaw eases in and out after the heading: per ROTATION SPEED point, the most it turns per step and
// the most that changes per step, in 1/256 degrees. Twice as fast to get behind a car just entered.
const s32 kMaxRatePerSpeed = 256 * 6 / 10;
const s32 kAccelPerSpeed = 256 * 4 / 100;
// FOLLOW: heading changes smaller than this (lane changes, skids) leave the view alone, unless the heading then
// holds within kSteady for kSteadySteps.
const s32 kDeadZone = 15 * 256;
const s32 kSteady = 2 * 256;
const s32 kSteadySteps = 30;
// Slower than this (Fix16, as the camera's look-ahead measures speed) the view doesn't turn.
const s32 kMinSpeed = 16384 / 2;
// Backing up this many steps turns the view round (Chinatown Wars' Auto Reverse).
const s32 kReverseSteps = 40;
// The tilt in 1/256 degrees: the CAMERA TILT setting, none in scripted scenes, eased in.
const s32 kPitchFollow = 12;
// The farthest the subject is drawn from the screen's centre, in half screen heights.
const f32 kMaxLead = 0.5f;

s32 sYaw;
s32 sPrevYaw;
bool sValid;
bool sRecentre;
bool sEngaged;
s32 sTarget;
s32 sRate;
s32 sRateRest;
s32 sTurnFrom;
s32 sTurnTarget;
s32 sTurnSteps;
s32 sTurnAt;
bool sTurnMoved;
s32 sAxisSteps;
s32 sSteadyHeading;
s32 sSteadySteps;
s32 sReverse;
s32 sPitch;
s32 sPrevPitch;
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
f32 sCosP = 1.0f;
f32 sSinP;
f32 sScale = 1.0f;
f32 sHalfHeight = 1.0f;
f32 sMarkerDepth = 1.0f;
Camera* spCamera;
u8 sCameraBytes[sizeof(Camera)];

s32 Wrap(s32 v)
{
    return ((v % kTurn) + kTurn + kHalfTurn) % kTurn - kHalfTurn;
}

// A point of the STREETS turn, t from 0 to 1, eased in and out.
s32 TurnAt(f32 t)
{
    const f32 eased = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    return (sTurnFrom + static_cast<s32>(Wrap(sTurnTarget - sTurnFrom) * eased) + kTurn) % kTurn;
}

bool Within(s32 delta, s32 limit)
{
    delta = Wrap(delta);
    return delta < limit && delta > -limit;
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

// The heading to follow, or -1 to keep the view as it is.
s32 CarHeading(Car* pCar)
{
    const Ang16 rotation = pCar->GetCarRotation();
    const Fix16_Point velocity = pCar->GetLinearVelocity();
    const long long forward = static_cast<long long>(velocity.x.mValue) * Ang16::sine(rotation).mValue +
        static_cast<long long>(velocity.y.mValue) * Ang16::cosine(rotation).mValue;
    const bool backing = pCar->mPhysics && pCar->mPhysics->is_backward_gas_on();
    sReverse = backing ? (forward < 0 && sReverse < kReverseSteps ? sReverse + 1 : sReverse) : 0;

    if (velocity.GetLength_41E260().mValue * 16 < kMinSpeed)
    {
        return sRecentre ? (rotation.rValue + 720 + 1440) % 1440 * kSub : -1;
    }
    // Backing up, the view stays behind the nose until Auto Reverse.
    const s32 travel = velocity.GetAngle().rValue + (forward < 0 && sReverse < kReverseSteps ? 720 : 0);
    return (travel % 1440 + 2160) % 1440 * kSub;
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
    // The look-ahead's length straight down the screen (up when backing): the car stays put as the world turns.
    const f32 lx = 0.0f;
    const f32 length = sqrtf((ox + px) * (ox + px) + (oy + py) * (oy + py));
    const f32 facing = (ox + px) * Ang16::sine(pSprite->mRotation).ToFloat() + (oy + py) * Ang16::cosine(pSprite->mRotation).ToFloat();
    f32 side = length > 1.0f ? -3.0f * facing / length : 1.0f;
    side = side < -1.0f ? -1.0f : (side > 1.0f ? 1.0f : side);
    const f32 ly = (length < limit ? length : limit) * side;
    const f32 dx = sCos * ox - sSin * oy - (lx - px);
    sMarkerDepth = depth;
    // Tilted, the subject seen as far below the centre as unturned: the eye goes back by depth * tan(pitch).
    const f32 tilted = scale * tanf(atanf((ly - py) / scale) - atan2f(sSinP, sCosP));
    const f32 dy = sSin * ox + sCos * oy - tilted;
    sMarkX = -(sCos * dx + sSin * dy);
    sMarkY = -(-sSin * dx + sCos * dy);
    pCamera->mCurPos.mX += Fix16(-sMarkX * depth / scale);
    pCamera->mCurPos.mY += Fix16(-sMarkY * depth / scale);
}

// Half extents of the view at some depth grown to the tilted view's (symmetric about the eye, which keeps the
// map's far to near order), then to the bounding box of the turned ones.
void TurnExtent(f32* pHalfX, f32* pHalfY)
{
    const f32 v = sHalfHeight;
    const f32 x = *pHalfX / (sCosP - v * sSinP);
    const f32 y = *pHalfY * (sCosP + sSinP / v) / (sCosP - v * sSinP);
    *pHalfX = fabsf(sCos) * x + fabsf(sSin) * y;
    *pHalfY = fabsf(sSin) * x + fabsf(sCos) * y;
}

// The sprites drawn are those of the turned view's bounding box.
void WidenBounds(Camera* pCamera)
{
    WorldRect& bounds = pCamera->mViewBounds;
    const Fix16 x = pCamera->mCurPos.mX;
    const Fix16 y = pCamera->mCurPos.mY;
    f32 hx = ToF(bounds.mRight - x > x - bounds.mLeft ? bounds.mRight - x : x - bounds.mLeft);
    f32 hy = ToF(bounds.mBottom - y > y - bounds.mTop ? bounds.mBottom - y : y - bounds.mTop);
    TurnExtent(&hx, &hy);
    const Fix16 turnedX(hx);
    const Fix16 turnedY(hy);
    bounds.mLeft = x - turnedX;
    bounds.mRight = x + turnedX;
    bounds.mTop = y - turnedY;
    bounds.mBottom = y + turnedY;
}
} // namespace

void Snapshot()
{
    sPrevYaw = sYaw;
    sPrevPitch = sPitch;
}

void Clear()
{
    sValid = false;
    sYaw = 0;
    sPrevYaw = 0;
    sPitch = 0;
    sPrevPitch = 0;
    sTarget = 0;
    sRate = 0;
    sRateRest = 0;
    sAxisSteps = 0;
    sTurnFrom = sTurnTarget = sTurnSteps = sTurnAt = 0;
    sTurnMoved = false;
    sEngaged = false;
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
    s32 heading = -1;
    s32 pitch = PortSettings::CameraTilt() * 256;
    Car* pCar = NULL;
    const bool scripted = Scripted(pPlayer);
    if (scripted)
    {
        pitch = 0;
    }
    else
    {
        Ped* pPed = Subject(pPlayer);
        pCar = pPed ? pPed->get_car() : NULL;
        if (pCar && pCar->mCarSprite)
        {
            sRecentre = sRecentre || pCar != spCar;
            heading = CarHeading(pCar);
        }
        else
        {
            sReverse = 0;
        }
    }
    spCar = pCar;

    const bool steady = heading >= 0 && Within(heading - sSteadyHeading, kSteady);
    sSteadySteps = steady ? sSteadySteps + 1 : 0;
    sSteadyHeading = steady ? sSteadyHeading : heading;
    const s32 axis = (heading + kQuarter / 2) / kQuarter % 4 * kQuarter;
    if (scripted)
    {
        sTarget = 0;
    }
    else if (heading < 0)
    {
    }
    else if (PortSettings::CameraFollows())
    {
        if (sRecentre || !Within(heading - sTarget, kDeadZone) || (sSteadySteps >= kSteadySteps && !Within(heading - sYaw, kSteady)))
        {
            sEngaged = true;
        }
        sTarget = sEngaged ? heading : sTarget;
    }
    else if (sRecentre)
    {
        sTarget = axis;
        sAxisSteps = 0;
    }
    else
    {
        sAxisSteps = axis != sTarget && Within(heading - axis, kAxisSnap) ? sAxisSteps + 1 : 0;
        if (sAxisSteps >= kAxisSteps)
        {
            sTarget = axis;
            sAxisSteps = 0;
        }
    }
    sTurnMoved = false;
    if (!sValid)
    {
        sYaw = sTarget;
        sPrevYaw = sTarget;
        sTurnTarget = sTarget;
        sTurnAt = sTurnSteps = 0;
        sRate = 0;
        sRateRest = 0;
        sPitch = pitch;
        sPrevPitch = pitch;
        sValid = true;
    }
    else if (!PortSettings::CameraFollows())
    {
        if (sTarget != sTurnTarget)
        {
            const s32 angle = Wrap(sTarget - sYaw) < 0 ? -Wrap(sTarget - sYaw) : Wrap(sTarget - sYaw);
            sTurnFrom = sYaw;
            sTurnTarget = sTarget;
            sTurnAt = 0;
            sTurnSteps = kTurnSteps[PortSettings::CameraTurnSpeed()] * (angle > kQuarter ? angle : kQuarter) / kQuarter;
        }
        if (sTurnAt < sTurnSteps)
        {
            sTurnAt++;
            sTurnMoved = true;
        }
        sYaw = TurnAt(sTurnSteps ? static_cast<f32>(sTurnAt) / sTurnSteps : 1.0f);
        sPitch += (pitch - sPitch) * kPitchFollow / 256;
    }
    else
    {
        // In 1/256 of a yaw unit: the most speed that still stops at the target with the acceleration available.
        const f64 maxRate = kMaxRatePerSpeed * PortSettings::CameraTurnSpeed() * (sRecentre ? 2 : 1) * 256.0;
        const f64 accel = kAccelPerSpeed * PortSettings::CameraTurnSpeed() * (sRecentre ? 2 : 1) * 256.0;
        const f64 left = Wrap(sTarget - sYaw) * 256.0;
        f64 wanted = sqrt(2 * accel * fabs(left) + accel * accel / 4) - accel / 2;
        wanted = (wanted < maxRate ? wanted : maxRate) * (left < 0 ? -1 : 1);
        const f64 rate =
            wanted > sRate ? (wanted < sRate + accel ? wanted : sRate + accel) : (wanted > sRate - accel ? wanted : sRate - accel);
        sRate = static_cast<s32>(rate);
        sRateRest += sRate;
        sYaw = (sYaw + sRateRest / 256 + kTurn) % kTurn;
        sRateRest %= 256;
        sYaw = PortSettings::CameraTurnSpeed() ? sYaw : sTarget;
        sTurnTarget = sTarget;
        sPitch += (pitch - sPitch) * kPitchFollow / 256;
    }
    if (Within(sTarget - sYaw, kSteady) &&
        (!PortSettings::CameraFollows() || heading < 0 || (Within(heading - sYaw, kSteady) && sSteadySteps >= kSteadySteps / 4)))
    {
        sEngaged = false;
        sRecentre = false;
    }
    sStepCos = cosf(Radians(static_cast<f32>(sYaw)));
    sStepSin = sinf(Radians(static_cast<f32>(sYaw)));
}

f32 Yaw(f32 alpha)
{
    // ROTATION SPEED 0 cuts: no frame drawn half way.
    alpha = PortSettings::CameraTurnSpeed() ? alpha : 1.0f;
    const f32 yaw = (sTurnMoved ? TurnAt((sTurnAt - 1 + alpha) / sTurnSteps) : sPrevYaw + Wrap(sYaw - sPrevYaw) * alpha) / kSub;
    return yaw < 0.0f ? yaw + 1440.0f : yaw;
}

f32 Pitch(f32 alpha)
{
    return (sPrevPitch + (sPitch - sPrevPitch) * alpha) / 256.0f;
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
    const f32 pitch = Pitch(alpha) * (3.14159265f / 180.0f);
    sCosP = cosf(pitch);
    sSinP = sinf(pitch);
    sMarkX = 0.0f;
    sMarkY = 0.0f;
    sScale = ToF(pCamera->mScreenScale.x);
    sHalfHeight = pCamera->mScreenPxHeight / (2.0f * sScale);
    sMarkerDepth = ToF(pCamera->mCurPos.mZ) + 6.0f;
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
    port_SetViewTurn(sDrawn ? turn : kOff,
                     sCx,
                     sCy,
                     sCos,
                     sSin,
                     turn == kMarkers ? sMarkX : 0.0f,
                     turn == kMarkers ? sMarkY : 0.0f,
                     sCosP,
                     sSinP,
                     sScale,
                     sMarkerDepth);
#endif
}

void TurnPoint(Fix16& x, Fix16& y, Fix16 z)
{
    if (sDrawn)
    {
        const f32 dx = ToF(x) - sCx;
        const f32 dy = ToF(y) - sCy;
        const f32 rx = sCos * dx - sSin * dy;
        const f32 ry = sSin * dx + sCos * dy;
        const f32 d = ToF(spCamera->mCurPos.mZ - z) + 8.0f;
        const f32 k = d / (d * sCosP - ry * d / sScale * sSinP);
        x = Fix16(sCx + rx * k);
        y = Fix16(sCy + (ry * sCosP + sScale * sSinP) * k);
    }
}
} // namespace PortView

void PortViewTurnedExtent(Fix16& width, Fix16& height)
{
    if (PortView::sDrawn)
    {
        f32 w = PortView::ToF(width);
        f32 h = PortView::ToF(height);
        PortView::TurnExtent(&w, &h);
        width = Fix16(w);
        height = Fix16(h);
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
