// Drawing between two logic steps: every live sprite (cars, peds, objects, particles) and the view camera
// are drawn at prev + (cur - prev) * alpha, then put back exactly, so the logic only ever sees whole steps.
#pragma once

#include "types.hpp"

namespace PortInterp
{
// Before a logic step: the positions it starts from.
void Snapshot();
// Nothing to interpolate from (load, new game).
void Clear();
// Around a draw; alpha in [0, 1] is the fraction of the next step already elapsed.
void Apply(f32 alpha);
void Restore();
// Sprites drawn in between, drawn where they are after a jump, drawn unturned after a snap round; then zeroed.
void TakeCounts(u32* pMoved, u32* pJumped, u32* pSnapped);
} // namespace PortInterp
