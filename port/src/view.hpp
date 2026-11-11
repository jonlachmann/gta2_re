// The drawn view's projection between the ground and the screen, for TWIN STICK's aim.
#pragma once

#include "types.hpp"

namespace PortView
{
// Before a game draw, once the camera is where the frame shows it.
void BeginDraw();

// The last frame's projection between the ground at height z and the screen, in the game's screen pixels.
void GroundToScreen(f32 x, f32 y, f32 z, f32* pX, f32* pY);
void ScreenToGround(f32 x, f32 y, f32 z, f32* pX, f32* pY);
} // namespace PortView
