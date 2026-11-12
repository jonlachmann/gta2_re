// Game state the platform layer asks about (declared in compat/src/port_platform.hpp).
#include "Frontend.hpp"
#include "Game.hpp"
#include "Player.hpp"
#include "Camera.hpp"
#include "pause_menu.hpp"
#include "settings.hpp"

bool port_GameRunning()
{
    return gGame_0x40 && !gFrontend;
}

bool port_FrontendActive()
{
    return gFrontend != NULL;
}

bool port_MenuUsesMouse()
{
    return port_FrontendActive() || PortPauseMenu::IsOpen();
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

// The 640 (or 480) wide layout scaled by the HUD scale, and what the safe area has beyond it: what lies
// in the outer sixths of the layout stays at that edge of the safe area, the rest is centred with it.
f32 Offset(f32 pos, f32 layoutSize, s32 screenSize)
{
    const f32 margin = screenSize * PortSettings::SafeArea();
    const f32 extra = screenSize - 2 * margin - layoutSize * ToF(gViewCamera->mUiScale);
    return margin + (pos < layoutSize / 6 ? 0.0f : (pos > layoutSize * 5 / 6 ? extra : extra / 2));
}
} // namespace

Fix16 PortUiX(Fix16 x)
{
    if (PortPauseMenu::IsDrawingStats())
    {
        return FromF(PortPauseMenu::PlaceUi(ToF(x), false));
    }
    return FromF(ToF(x) * ToF(gViewCamera->mUiScale) + Offset(ToF(x), 640.0f, gViewCamera->mScreenPxWidth));
}

Fix16 PortUiY(Fix16 y)
{
    if (PortPauseMenu::IsDrawingStats())
    {
        return FromF(PortPauseMenu::PlaceUi(ToF(y), true));
    }
    return FromF(ToF(y) * ToF(gViewCamera->mUiScale) + Offset(ToF(y), 480.0f, gViewCamera->mScreenPxHeight));
}

// Text centred in the layout stays centred; otherwise its start (left edge) or its end (right edge)
// decides, so text beside a face or an icon keeps its place.
Fix16 PortUiStringX(const wchar_t* pStr, Fix16 x, u16 font)
{
    if (PortPauseMenu::IsDrawingStats())
    {
        return PortUiX(x);
    }
    const f32 width = static_cast<f32>(Frontend::GetMaxTextWidth(const_cast<wchar_t*>(pStr), font));
    const f32 start = ToF(x);
    const f32 centred = (640.0f - width) / 2;
    f32 anchor = 320.0f;
    if (start - centred > 1.5f || centred - start > 1.5f)
    {
        anchor = start < 640.0f / 6 ? 0.0f : (start + width > 640.0f * 5 / 6 ? 640.0f : 320.0f);
    }
    return FromF(start * ToF(gViewCamera->mUiScale) + Offset(anchor, 640.0f, gViewCamera->mScreenPxWidth));
}

// The layout fits the safe area's height on wide screens, its width on tall ones. Scaled up, it can go
// past the height (its edges stay anchored) but not the width, which its texts span. In fixed point:
// the defaults give the scale the screen alone gives.
Fix16 PortHudUiScale(u32 width, u32 height)
{
    const Fix16 safe = FromF(1 - 2 * PortSettings::SafeArea());
    const Fix16 byWidth = Fix16(width) / 640 * safe;
    const Fix16 byHeight = Fix16(height) / 480 * safe;
    const Fix16 scale = (byHeight < byWidth ? byHeight : byWidth) * FromF(PortSettings::HudScale());
    return scale < byWidth ? scale : byWidth;
}

Fix16 PortScreenUiScale()
{
    const Fix16 byWidth = Fix16(gViewCamera->mScreenPxWidth) / 640;
    const Fix16 byHeight = Fix16(gViewCamera->mScreenPxHeight) / 480;
    return byHeight < byWidth ? byHeight : byWidth;
}

Fix16 PortViewWidthFactor()
{
    const s32 width = gViewCamera->mScreenPxWidth;
    const s32 width_4_3 = gViewCamera->mScreenPxHeight * 4 / 3;
    return width > width_4_3 && width_4_3 > 0 ? FromF(static_cast<f32>(width) / width_4_3) : Fix16(1);
}
