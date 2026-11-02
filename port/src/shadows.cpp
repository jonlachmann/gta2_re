#include "shadows.hpp"
#include "Camera.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "gbh_graphics.hpp"
#include "map_0x370.hpp"
#include "sprite.hpp"

#if defined(GTA2_PORT_SDL)
bool port_SoftShadows();
#endif
EXTERN_GLOBAL_ARRAY(Vert, gSpriteVerts, 8);

namespace PortShadows
{
namespace
{
// In blocks: the original's 5 pixels of the 640 wide layout at the camera's resting height over a street.
const f32 kOffset = 1.0f / 16;
// The sun's slope: offset per block of height above the ground.
const f32 kOffsetPerHeight = 0.25f;
// Blur radius in texels, wider for a sprite above the ground.
const f32 kBlur = 1.0f;
const f32 kBlurPerHeight = 2.0f;
const f32 kMaxHeight = 2.0f;
// The ground is probed this far above the sprite: drawn between two steps, a sprite on a slope can sit just
// under its surface, and the ground below that is a block lower. Heights under kContact count as none.
const Fix16 kProbe = Fix16(0x800, 0);
const f32 kContact = 1.0f / 16;
// The game's shadow flags, and the renderer's soft shadow (gbh_api.cpp FLAG_SOFT_SHADOW).
const s32 kShadowFlags = 0x2180;
const s32 kSoftShadowFlag = 0x40000000;
} // namespace

bool Draw(Sprite* pSprite, STexture* pTexture)
{
#if defined(GTA2_PORT_SDL)
    if (!port_SoftShadows())
    {
        return false;
    }
    const Fix16 ground = gMap_0x370->GetGroundZBelowCoord(pSprite->mXy.x, pSprite->mXy.y, pSprite->mZpos + kProbe);
    f32 height = (pSprite->mZpos - ground).ToFloat() - kContact;
    height = height < 0.0f ? 0.0f : (height > kMaxHeight ? kMaxHeight : height);
    const f32 offset = kOffset + height * kOffsetPerHeight;
    const u32 blur = static_cast<u32>((kBlur + height * kBlurPerHeight) * 32.0f);

    // The sprite's corners moved down and right onto the ground, projected as Sprite::Draw does.
    const Camera* pCam = gViewCamera;
    const f32 scale = pCam->mScreenScale.x.ToFloat();
    Vert verts[8];
    for (s32 i = 0; i < 4; i++)
    {
        Vert& world = verts[i + 4];
        world = gSpriteVerts[i + 4];
        world.x += offset;
        world.y += offset;
        world.z -= height;
        Vert& v = verts[i];
        v = gSpriteVerts[i];
        v.z = 1.0f / (pCam->mCurPos.mZ.ToFloat() + 8.0f - world.z);
        v.x = scale * (world.x - pCam->mCurPos.mX.ToFloat()) * v.z + static_cast<u32>(pCam->mScreenPxCenterX);
        v.y = scale * (world.y - pCam->mCurPos.mY.ToFloat()) * v.z + static_cast<u32>(pCam->mScreenPxCenterY);
        v.diff = 0xA0000000 | (blur << 16);
    }
    pgbh_DrawQuad(kShadowFlags | kSoftShadowFlag, pTexture, verts, 255);
    return true;
#else
    return false;
#endif
}
} // namespace PortShadows
