// Car headlights for the dusk lighting (the graphics option CAR HEADLIGHTS): a spot light at each lit
// headlight lamp of the cars' sprites, among the game's lights.
#pragma once

#include "types.hpp"

namespace PortHeadlights
{
// With the level's lights.
void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y);
} // namespace PortHeadlights
