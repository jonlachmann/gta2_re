#include "frame_loop.hpp"
#include "Draw.hpp"
#include "Frontend.hpp"
#include "Game.hpp"
#include "Hud.hpp"
#include "Player.hpp"
#include "Ped.hpp"
#include "interpolation.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "view.hpp"
#include <stdio.h>
#include <stdlib.h>

#if defined(GTA2_PORT_SDL)
unsigned long long port_NowNs();
void port_DelayNs(unsigned long long ns);
void port_CountGameStep();
void port_SetGameFlip(bool gameFlip);
void port_PresentContent(unsigned long long gameNs);
unsigned long long port_SwapRefreshNs();
void port_PresentAgain();
#else
namespace
{
unsigned long long port_NowNs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return static_cast<unsigned long long>(static_cast<f64>(now.QuadPart) * 1e9 / static_cast<f64>(freq.QuadPart));
}

void port_DelayNs(unsigned long long ns)
{
    if (ns >= 2000000)
    {
        Sleep(static_cast<DWORD>(ns / 1000000) - 1);
    }
}

void port_CountGameStep()
{
}

void port_SetGameFlip(bool)
{
}

void port_PresentContent(unsigned long long)
{
}

unsigned long long port_SwapRefreshNs()
{
    return 0;
}

void port_PresentAgain()
{
}
} // namespace
#endif

namespace PortFrameLoop
{
namespace
{
// After a stall the game catches up this many steps before a frame is drawn; the rest of the backlog is dropped.
const u32 kMaxStepsPerDraw = 4;
// Nearer than this, the next step or frame is waited for by polling the message loop.
const unsigned long long kMinDelayNs = 200000;

bool sStarted;
unsigned long long sLast;
unsigned long long sAccumulated;
unsigned long long sStepNs = 33000000;
u32 sStepsSinceDraw;
unsigned long long sNextDraw; // timer pacing
unsigned long long sLastPresent; // vsync pacing
u32 sRepeatsLeft;
u32 sRefreshesPerFrame;
f32 sAlpha;
bool sInDraw;
u32 sDrawSteps;
u32 sMaxFps = 30;

// GTA2_LOOP_TRACE=<file>: the player's position after each step, the drawn camera and player each frame.
// GTA2_LOOP_STATS=1: steps and frames per second on stderr.
FILE* spTrace;
bool sStats;
u32 sStepCount;
u32 sStatsSteps;
u32 sStatsDraws;
unsigned long long sStatsStart;
// SHOW FPS: the draws since sFpsStart, and sFps, their rate over the last half second.
u32 sFpsDraws;
unsigned long long sFpsStart;
u32 sFps;

bool Original()
{
    return sMaxFps == 30;
}

// The frame limit as a whole number of refreshes: the nearest one, at least 1.
u32 RefreshesPerFrame(unsigned long long refreshNs)
{
    const u32 n = sMaxFps ? static_cast<u32>(1e9 / sMaxFps / refreshNs + 0.5) : 1;
    return n ? n : 1;
}

Sprite* PlayerSprite()
{
    Ped* pPed = gGame_0x40 && gGame_0x40->mLocalPlayer ? gGame_0x40->mLocalPlayer->mPlayerPed : NULL;
    return pPed && (pPed->mCar || pPed->mGameObject) ? pPed->GetSprite() : NULL;
}

void Trace(char kind)
{
    if (!spTrace)
    {
        return;
    }
    const Sprite* pSprite = PlayerSprite();
    const Camera* pCamera = gGame_0x40 && gGame_0x40->mDrawPlayer ? &gGame_0x40->mDrawPlayer->mViewCamera : NULL;
    fprintf(spTrace, "%c %u %.4f cam %d %d %d player %d %d %d %d\n", kind, sStepCount, kind == 'D' ? sAlpha : 1.0f,
            pCamera ? pCamera->mCurPos.mX.mValue : 0, pCamera ? pCamera->mCurPos.mY.mValue : 0,
            pCamera ? pCamera->mCurPos.mZ.mValue : 0, pSprite ? pSprite->mXy.x.mValue : 0,
            pSprite ? pSprite->mXy.y.mValue : 0, pSprite ? pSprite->mZpos.mValue : 0,
            pSprite ? pSprite->mRotation.rValue : 0);
}

void Stats(unsigned long long now)
{
    if (!sStats)
    {
        return;
    }
    if (!sStatsStart)
    {
        sStatsStart = now;
        return;
    }
    const f64 seconds = (now - sStatsStart) / 1e9;
    if (seconds >= 5.0)
    {
        u32 moved;
        u32 jumped;
        u32 snapped;
        PortInterp::TakeCounts(&moved, &jumped, &snapped);
        fprintf(stderr, "loop: %.2f steps/s, %.1f frames/s (frame limit %u, %u refreshes per frame), sprites per frame: %.1f in between, %.2f jumped, %.2f snapped round\n",
                sStatsSteps / seconds, sStatsDraws / seconds, sMaxFps, sRefreshesPerFrame, moved / (f64)(sStatsDraws ? sStatsDraws : 1),
                jumped / (f64)(sStatsDraws ? sStatsDraws : 1), snapped / (f64)(sStatsDraws ? sStatsDraws : 1));
        sStatsStart = now;
        sStatsSteps = 0;
        sStatsDraws = 0;
    }
}
} // namespace

void ApplySettings()
{
    sMaxFps = static_cast<u32>(PortSettings::MaxFps());
    static bool sInit;
    if (!sInit)
    {
        sInit = true;
        const char* pTrace = getenv("GTA2_LOOP_TRACE");
        spTrace = pTrace && *pTrace ? fopen(pTrace, "w") : NULL;
        sStats = getenv("GTA2_LOOP_STATS") != NULL;
        // Otherwise only the frontend's options menu hands them to the renderer, and skip_frontend never builds it.
        PortSettings::ApplyRenderOptions();
    }
}

void Reset()
{
    sStarted = false;
    sFpsStart = 0;
    sFps = 0;
    sAccumulated = 0;
    sStepsSinceDraw = 0;
    sRepeatsLeft = 0;
    PortInterp::Clear();
    ApplySettings();
}

Action Next(s32 stepMs, bool forceStep, bool noDraw)
{
    const unsigned long long now = port_NowNs();
    if (!sStarted)
    {
        sStarted = true;
        sLast = now;
        sNextDraw = now;
    }
    sAccumulated += now - sLast;
    sLast = now;
    sStepNs = static_cast<unsigned long long>(stepMs) * 1000000;
    Stats(now);

    if (forceStep || noDraw)
    {
        sAccumulated = 0;
        return kStep;
    }
    if (sAccumulated >= sStepNs)
    {
        if (sStepsSinceDraw < kMaxStepsPerDraw)
        {
            sAccumulated -= sStepNs;
            return kStep;
        }
        sAccumulated %= sStepNs;
    }

    unsigned long long wake = now + (sStepNs - sAccumulated);
    const unsigned long long refreshNs = Original() ? 0 : port_SwapRefreshNs();
    sRefreshesPerFrame = refreshNs ? RefreshesPerFrame(refreshNs) : 0;
    if (Original())
    {
        if (sStepsSinceDraw)
        {
            sAlpha = 1.0f;
            return kDraw;
        }
    }
    else if (refreshNs)
    {
        // Vsync: present on every refresh, since the swap waits for it, so frames follow the screen's clock.
        // A new frame every sRefreshesPerFrame, the last one again in between.
        const unsigned long long due = sLastPresent + refreshNs / 2; // only binds when the swap does not wait (hidden window)
        if (now >= due)
        {
            sLastPresent = now;
            if (sRepeatsLeft)
            {
                sRepeatsLeft--;
                port_PresentAgain();
                return kIdle;
            }
            sRepeatsLeft = sRefreshesPerFrame - 1;
            sAlpha = static_cast<f32>(static_cast<f64>(sAccumulated) / sStepNs);
            return kDraw;
        }
        if (due < wake)
        {
            wake = due;
        }
    }
    else
    {
        // No vsync, or no known refresh rate: a timer at the frame limit.
        if (now >= sNextDraw)
        {
            const unsigned long long interval = sMaxFps ? 1000000000ull / sMaxFps : 0;
            sNextDraw = now - sNextDraw > interval ? now + interval : sNextDraw + interval;
            sAlpha = static_cast<f32>(static_cast<f64>(sAccumulated) / sStepNs);
            return kDraw;
        }
        if (sNextDraw < wake)
        {
            wake = sNextDraw;
        }
    }
    if (wake > now + kMinDelayNs)
    {
        port_DelayNs(wake - now - kMinDelayNs / 2);
    }
    return kIdle;
}

void BeforeStep()
{
    PortInterp::Snapshot();
}

void AfterStep()
{
    sStepsSinceDraw++;
    sStepCount++;
    sStatsSteps++;
    port_CountGameStep();
    Trace('S');
}

void BeginDraw()
{
    sInDraw = true;
    sDrawSteps = sStepsSinceDraw;
    if (!Original())
    {
        PortInterp::Apply(sAlpha);
    }
    Trace('D');
    port_PresentContent(static_cast<unsigned long long>((sStepCount + static_cast<f64>(sAlpha)) * sStepNs));
    PortView::BeginDraw();
    port_SetGameFlip(true);
}

void EndDraw()
{
    port_SetGameFlip(false);
    PortInterp::Restore();
    sStepsSinceDraw = 0;
    sInDraw = false;
    sStatsDraws++;
    sFpsDraws++;
    const unsigned long long now = port_NowNs();
    if (now - sFpsStart >= 500000000ull)
    {
        sFps = sFpsStart ? static_cast<u32>(sFpsDraws * 1e9 / (now - sFpsStart) + 0.5) : 0;
        sFpsStart = now;
        sFpsDraws = 0;
    }
}

void DrawFps()
{
    if (!sFps || !PortSettings::ShowFps())
    {
        return;
    }
    wchar_t text[16];
    swprintf(text, L"%u FPS", sFps);
    // Clear of the briefs along the bottom and of the power-ups' icons lower on the right.
    DrawStringScaled(text, Fix16(636 - Frontend::GetMaxTextWidth(text, gDebugFont)), Fix16(48), gDebugFont);
}

u32 StepsBeforeDraw()
{
    return sInDraw ? sDrawSteps : 1;
}

u32 StepCount()
{
    return sStepCount;
}

f32 DrawAlpha()
{
    return sInDraw && !Original() ? sAlpha : 1.0f;
}
} // namespace PortFrameLoop
