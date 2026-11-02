// The CHINATOWN camera: the view turns so that the player's vehicle points up the screen. Only the drawing
// turns: the game's camera, view bounds and spawning stay those of the unturned view.
#pragma once

#include "fix16.hpp"

namespace PortView
{
// Around each logic step.
void Snapshot();
void Step();
// Nothing to turn from (load, new game).
void Clear();

// Around a game draw; alpha as PortInterp::Apply.
void BeginDraw(f32 alpha);
void EndDraw();

enum Turn
{
    kOff,
    kWorld,
    // Sprites keep their shape: only their centre is tilted.
    kSprites,
    // HUD markers placed with the game camera: also moved as the drawn camera was.
    kMarkers,
};
void SetTurn(Turn turn);

// A screen point of height z turned and tilted with the view (upright text anchored in the world).
void TurnPoint(Fix16& x, Fix16& y, Fix16 z);

// The turn drawn at alpha between the last two steps, in Ang16 units, for the trace.
f32 Yaw(f32 alpha);
// The same for the tilt, in degrees.
f32 Pitch(f32 alpha);
} // namespace PortView
