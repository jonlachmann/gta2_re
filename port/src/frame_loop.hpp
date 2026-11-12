// The port's game loop pacing: logic in whole steps of Game::GetFrameDurationMs() on a monotonic clock,
// as the original paces it, and frames drawn in between at the chosen rate, interpolated (interpolation.hpp).
#pragma once

#include "types.hpp"

namespace PortFrameLoop
{
enum Action
{
    kIdle,
    kStep,
    kDraw,
};

// The step duration and whether the game asks for a step now (screen size change, no-draw debug mode).
Action Next(s32 stepMs, bool forceStep, bool noDraw);

// Around ExecuteGame and around DrawAndFlip.
void BeforeStep();
void AfterStep();
void BeginDraw();
void EndDraw();

// Logic steps since the previous draw: what the original advanced once per drawn frame advances that often.
u32 StepsBeforeDraw();

// Logic steps run so far, and where the current draw sits between the last two (1: on the last).
u32 StepCount();
f32 DrawAlpha();

// Two values a draw changes, put back at the end of the scope unless a logic step ran before this draw.
template <typename T>
class StepLocked
{
  public:
    StepLocked(T& a, T& b) : mA(a), mB(b), mKeepA(a), mKeepB(b)
    {
    }
    ~StepLocked()
    {
        if (!StepsBeforeDraw())
        {
            mA = mKeepA;
            mB = mKeepB;
        }
    }

  private:
    T& mA;
    T& mB;
    T mKeepA;
    T mKeepB;
};

// After a load or a new screen size: no backlog, nothing to interpolate from.
void Reset();

// SHOW FPS: the frames drawn per second, at the top right, under the health.
void DrawFps();

// The FRAME LIMIT setting changed.
void ApplySettings();
} // namespace PortFrameLoop
