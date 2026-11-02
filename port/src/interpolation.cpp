#include "interpolation.hpp"
#include "Camera.hpp"
#include "Game.hpp"
#include "Player.hpp"
#include "sprite.hpp"
#include <string.h>
#include <vector>

namespace PortInterp
{
namespace
{
// Fix16 units: one block is 16384. A sprite or the camera moving further in one step jumped (respawn,
// teleport, a new use of the same sprite): it is drawn where it is.
const s32 kMaxStepMove = 2 * 16384;
const s32 kMaxCameraStepMove = 4 * 16384;
// Ang16 units: 1440 a turn. A sprite turning more than this in one step snapped round.
const s32 kMaxStepTurn = 360;
const s32 kTurn = 1440;

const s32 kNumSprites = sizeof(gSpritePool->mPool.mPool) / sizeof(gSpritePool->mPool.mPool[0]);

struct Pose
{
    s32 mX;
    s32 mY;
    s32 mZ;
    s16 mRot;
    s16 mId; // 0: no live sprite
};

struct Shape
{
    SpriteShape* mpShape;
    u8 mBytes[sizeof(SpriteShape)];

    void Save(SpriteShape* pShape)
    {
        mpShape = pShape;
        if (pShape)
        {
            memcpy(mBytes, pShape, sizeof(SpriteShape));
        }
    }

    bool UpToDate() const
    {
        return mpShape && reinterpret_cast<const SpriteShape*>(mBytes)->mBboxUpToDate;
    }

    void Restore(SpriteShape* pCurrent) const
    {
        if (mpShape && pCurrent == mpShape)
        {
            memcpy(mpShape, mBytes, sizeof(SpriteShape));
        }
    }
};

struct Saved
{
    Sprite* mpSprite;
    Pose mPose;
    char_type mZCol;
    Shape mVisual;
    Shape mCollision;
};

Pose sPoses[kNumSprites];
Camera* spCamera;
CameraPos sCameraPos;
bool sValid;

std::vector<Saved> sSaved;
u32 sMoved;
u32 sJumped;
u32 sSnapped;
Camera* spAppliedCamera;
u8 sCameraBytes[sizeof(Camera)];

Pose PoseOf(const Sprite& sprite)
{
    Pose pose;
    pose.mX = sprite.mXy.x.mValue;
    pose.mY = sprite.mXy.y.mValue;
    pose.mZ = sprite.mZpos.mValue;
    pose.mRot = sprite.mRotation.rValue;
    pose.mId = sprite.mInstanceId;
    return pose;
}

s32 Abs(s32 v)
{
    return v < 0 ? -v : v;
}

s32 Lerp(s32 prev, s32 cur, f32 alpha)
{
    const f64 v = prev + static_cast<f64>(cur - prev) * alpha;
    return static_cast<s32>(v < 0 ? v - 0.5 : v + 0.5);
}

// The shortest way round, in [-720, 720).
s32 TurnBetween(s32 prev, s32 cur)
{
    return ((cur - prev) % kTurn + kTurn + kTurn / 2) % kTurn - kTurn / 2;
}

Camera* ViewCamera()
{
    return gGame_0x40 && gGame_0x40->mDrawPlayer ? &gGame_0x40->mDrawPlayer->mViewCamera : NULL;
}

void ApplySprite(Sprite& sprite, const Pose& prev, f32 alpha)
{
    const Pose cur = PoseOf(sprite);
    if (!cur.mId || cur.mId != prev.mId)
    {
        return;
    }
    const bool moved = cur.mX != prev.mX || cur.mY != prev.mY || cur.mZ != prev.mZ;
    s32 turn = TurnBetween(prev.mRot, cur.mRot);
    if (Abs(turn) > kMaxStepTurn)
    {
        turn = 0;
        sSnapped++;
    }
    if (!moved && !turn)
    {
        return;
    }
    if (Abs(cur.mX - prev.mX) > kMaxStepMove || Abs(cur.mY - prev.mY) > kMaxStepMove || Abs(cur.mZ - prev.mZ) > kMaxStepMove)
    {
        sJumped++;
        return;
    }
    sMoved++;

    Saved saved;
    saved.mpSprite = &sprite;
    saved.mPose = cur;
    saved.mZCol = sprite.mZCol;
    saved.mVisual.Save(sprite.mpVisualShape);
    saved.mCollision.Save(sprite.mpCollisionShape != sprite.mpVisualShape ? sprite.mpCollisionShape : NULL);
    sSaved.push_back(saved);

    sprite.mXy.x.mValue = Lerp(prev.mX, cur.mX, alpha);
    sprite.mXy.y.mValue = Lerp(prev.mY, cur.mY, alpha);
    sprite.mZpos.mValue = Lerp(prev.mZ, cur.mZ, alpha);
    sprite.mRotation.rValue = static_cast<s16>(cur.mRot - turn + Lerp(0, turn, alpha));
    sprite.mRotation.Normalize();
    sprite.InvalidateZColAndBBoxes();
}

// The caches the draw fills (bounding boxes, z layer) are left as a draw at the real positions leaves them.
void RestoreSprite(const Saved& saved)
{
    Sprite& sprite = *saved.mpSprite;
    const bool visualFilled = sprite.mpVisualShape && sprite.mpVisualShape->mBboxUpToDate && !saved.mVisual.UpToDate();
    const bool collisionFilled = saved.mCollision.mpShape && sprite.mpCollisionShape == saved.mCollision.mpShape &&
                                 sprite.mpCollisionShape->mBboxUpToDate && !saved.mCollision.UpToDate();
    const bool zColFilled = saved.mZCol == -1 && sprite.mZCol != -1;

    sprite.mXy.x.mValue = saved.mPose.mX;
    sprite.mXy.y.mValue = saved.mPose.mY;
    sprite.mZpos.mValue = saved.mPose.mZ;
    sprite.mRotation.rValue = saved.mPose.mRot;
    sprite.mZCol = saved.mZCol;
    saved.mVisual.Restore(sprite.mpVisualShape);
    saved.mCollision.Restore(sprite.mpCollisionShape);
    if (visualFilled)
    {
        sprite.mpVisualShape->mBboxUpToDate = false;
        sprite.mpVisualShape->UpdateRotatedBoundingBox(sprite.mXy.x, sprite.mXy.y, sprite.mZpos, sprite.mRotation);
    }
    if (collisionFilled)
    {
        sprite.mpCollisionShape->mBboxUpToDate = false;
        sprite.UpdateCollisionBoundsIfNeeded();
    }
    if (zColFilled)
    {
        sprite.ComputeZLayer();
    }
}
} // namespace

void Snapshot()
{
    sValid = gSpritePool && ViewCamera();
    if (!sValid)
    {
        return;
    }
    for (s32 i = 0; i < kNumSprites; i++)
    {
        sPoses[i] = PoseOf(gSpritePool->mPool.mPool[i]);
    }
    spCamera = ViewCamera();
    sCameraPos = spCamera->mCurPos;
}

void Clear()
{
    sValid = false;
}

void Apply(f32 alpha)
{
    sSaved.clear();
    spAppliedCamera = NULL;
    if (!sValid || !gSpritePool || ViewCamera() != spCamera)
    {
        return;
    }
    for (s32 i = 0; i < kNumSprites; i++)
    {
        ApplySprite(gSpritePool->mPool.mPool[i], sPoses[i], alpha);
    }

    Camera* pCamera = spCamera;
    const CameraPos cur = pCamera->mCurPos;
    const CameraPos& prev = sCameraPos;
    if (Abs(cur.mX.mValue - prev.mX.mValue) <= kMaxCameraStepMove && Abs(cur.mY.mValue - prev.mY.mValue) <= kMaxCameraStepMove &&
        Abs(cur.mZ.mValue - prev.mZ.mValue) <= kMaxCameraStepMove)
    {
        memcpy(sCameraBytes, pCamera, sizeof(Camera));
        spAppliedCamera = pCamera;
        pCamera->mCurPos.mX.mValue = Lerp(prev.mX.mValue, cur.mX.mValue, alpha);
        pCamera->mCurPos.mY.mValue = Lerp(prev.mY.mValue, cur.mY.mValue, alpha);
        pCamera->mCurPos.mZ.mValue = Lerp(prev.mZ.mValue, cur.mZ.mValue, alpha);
        pCamera->mCurPos.mZoom.mValue = Lerp(prev.mZoom.mValue, cur.mZoom.mValue, alpha);
        pCamera->UpdateBoundaries();
    }
}

void Restore()
{
    if (spAppliedCamera)
    {
        memcpy(spAppliedCamera, sCameraBytes, sizeof(Camera));
        spAppliedCamera = NULL;
    }
    for (size_t i = sSaved.size(); i-- > 0;)
    {
        RestoreSprite(sSaved[i]);
    }
    sSaved.clear();
}

void TakeCounts(u32* pMoved, u32* pJumped, u32* pSnapped)
{
    *pMoved = sMoved;
    *pJumped = sJumped;
    *pSnapped = sSnapped;
    sMoved = 0;
    sJumped = 0;
    sSnapped = 0;
}
} // namespace PortInterp
