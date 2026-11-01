// Game state the platform layer asks about (declared in compat/src/port_platform.hpp).
#include "Frontend.hpp"
#include "Game.hpp"
#include "Player.hpp"
#include "Camera.hpp"
#include "pause_menu.hpp"

bool port_GameRunning()
{
    return gGame_0x40 && !gFrontend;
}

bool port_QuitMessageShown()
{
    return port_GameRunning() && gGame_0x40->mLocalPlayer &&
           (gGame_0x40->mLocalPlayer->mShowQuitMessage || PortPauseMenu::IsOpen());
}

namespace
{
f32 ToF(Fix16 v)
{
    return v.mValue / 16384.0f;
}

Fix16 FromF(f32 v)
{
    return Fix16(static_cast<s32>(v * 16384.0f), 0);
}

// The 640 (or 480) wide layout scaled by the HUD scale, and what the screen has beyond it: thirds of
// the layout stay at the left (top), centre or right (bottom) of the screen.
f32 Anchor(f32 pos, f32 layoutSize, s32 screenSize)
{
    const f32 scale = ToF(gViewCamera->mUiScale);
    const f32 extra = screenSize - layoutSize * scale;
    const f32 offset = pos < layoutSize / 3 ? 0.0f : (pos > layoutSize * 2 / 3 ? extra : extra / 2);
    return offset;
}
} // namespace

Fix16 PortUiX(Fix16 x)
{
    return FromF(ToF(x) * ToF(gViewCamera->mUiScale) + Anchor(ToF(x), 640.0f, gViewCamera->mScreenPxWidth));
}

Fix16 PortUiY(Fix16 y)
{
    return FromF(ToF(y) * ToF(gViewCamera->mUiScale) + Anchor(ToF(y), 480.0f, gViewCamera->mScreenPxHeight));
}

// A string is anchored by its centre, or to the left when it starts near the left edge (text next to
// a face or an icon).
Fix16 PortUiStringX(const wchar_t* pStr, Fix16 x, u16 font)
{
    const f32 width = static_cast<f32>(Frontend::GetMaxTextWidth(const_cast<wchar_t*>(pStr), font));
    const f32 centre = ToF(x) < 640.0f / 6 ? 0.0f : ToF(x) + width / 2;
    return FromF(ToF(x) * ToF(gViewCamera->mUiScale) + Anchor(centre, 640.0f, gViewCamera->mScreenPxWidth));
}
