// SHADOWS SOFT: the game's car, ped and object shadows cast on the ground under the sprite, with soft edges.
#pragma once

#include "types.hpp"

class Sprite;
struct STexture;

namespace PortShadows
{
// Draws the shadow of the sprite whose quad is in gSpriteVerts; false when SHADOWS is ORIGINAL.
bool Draw(Sprite* pSprite, STexture* pTexture);
} // namespace PortShadows
