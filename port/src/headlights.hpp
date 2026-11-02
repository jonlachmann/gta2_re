// Car headlights for the dusk lighting (the graphics option CAR HEADLIGHTS): a glow, two of the game's
// point lights in front of each driven car, or beams, cones of light on the ground ahead.
#pragma once

#include "types.hpp"

namespace PortHeadlights
{
// With the level's lights.
void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y);

// After the blocks of a layer, for the cars standing on them.
void DrawBeams(s32 zLayer);
} // namespace PortHeadlights
