#include "view.hpp"
#include "Camera.hpp"
#include "Game.hpp"
#include "Player.hpp"
#include "winmain.hpp"

namespace PortView
{
namespace
{
// The drawn camera, as Camera::ProjectWorldToScreen_4B90E0 uses it.
struct Projection
{
    f32 mX, mY, mZ, mScale, mCx, mCy;
} sProj = {0, 0, 0, 1, 320, 240};

f32 ToF(Fix16 v)
{
    return v.mValue / 16384.0f;
}
} // namespace

void BeginDraw()
{
    Player* pPlayer = gGame_0x40 && gGame_0x40->mNumPlayers == 1 && !bStartNetworkGame ? gGame_0x40->mDrawPlayer : NULL;
    if (pPlayer)
    {
        const Camera& c = pPlayer->mViewCamera;
        sProj = {ToF(c.mCurPos.mX), ToF(c.mCurPos.mY), ToF(c.mCurPos.mZ), ToF(c.mScreenScale.x),
                 static_cast<f32>(c.mScreenPxCenterX), static_cast<f32>(c.mScreenPxCenterY)};
    }
}

void GroundToScreen(f32 x, f32 y, f32 z, f32* pX, f32* pY)
{
    const f32 d = sProj.mZ - z + 8.0f;
    *pX = sProj.mCx + (x - sProj.mX) * sProj.mScale / d;
    *pY = sProj.mCy + (y - sProj.mY) * sProj.mScale / d;
}

void ScreenToGround(f32 x, f32 y, f32 z, f32* pX, f32* pY)
{
    const f32 d = sProj.mZ - z + 8.0f;
    *pX = sProj.mX + (x - sProj.mCx) * d / sProj.mScale;
    *pY = sProj.mY + (y - sProj.mCy) * d / sProj.mScale;
}
} // namespace PortView
