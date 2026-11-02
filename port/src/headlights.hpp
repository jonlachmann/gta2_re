// Car headlights for the dusk lighting: two of the game's point lights in front of each driven car,
// with the level's lights (Screen setting car_headlights, on by default).
#pragma once

#include "types.hpp"

namespace PortHeadlights
{
void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y);
} // namespace PortHeadlights
