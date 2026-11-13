#include "day_night.hpp"
#include "Camera.hpp"
#include "Draw.hpp"
#include "TextureCache.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "rng.hpp"
#include "settings.hpp"
#include "sprite.hpp"
#include "winmain.hpp"

#if defined(GTA2_PORT_SDL)
void port_SetAmbient(f32 r, f32 g, f32 b, f32 lights);
#endif

namespace PortDayNight
{
namespace
{
// Ticks of 33 ms.
const u32 kTicksPerMinute = 60000 / 33;

// 1 by day, 0 by night, as of the frame's SetAmbient.
f32 sDay;

f32 SmoothStep(f32 from, f32 to, f32 x)
{
    f32 t = (x - from) / (to - from);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// The time of day in [0, 1).
f32 DayFraction(u32* pMinute = NULL)
{
    const u32 dayTicks = PortSettings::DayLength() * kTicksPerMinute;
    const u32 tick = (static_cast<u32>(gpRng->get_cur_rng()) + PortSettings::StartTime() * dayTicks / 24) % dayTicks;
    if (pMinute)
    {
        *pMinute = tick * 1440 / dayTicks;
    }
    return static_cast<f32>(tick) / dayTicks;
}

// Dawn from 5 to 7, dusk from 18 to 20; exactly 1 by day and 0 by night.
f32 Daylight()
{
    const f32 hour = 24.0f * DayFraction();
    return SmoothStep(5.0f, 7.0f, hour) - SmoothStep(18.0f, 20.0f, hour);
}

// The clock: the money's digits (user sprite 158, rows 9 to 0 under a blank one) at 3/4 of their size,
// left of the money, its colon two squares cut from the 0's left side.
const s16 kMoneyDigits = 158;
const f32 kClockScale = 0.75f;
const f32 kClockRight = 484.0f;
const f32 kClockY = 14.0f;
const s32 kClockSlots = 5;
TextureCache* spClockCache;
s16 sClockSlot;

void DrawGlyph(s32 slot, s32 row, s32 width, s32 height, f32 x, f32 y)
{
    STexture* pTexture = gTextureCache_15D8->GetDigitTexture(sClockSlot + slot, row, height);
    // Kept at its distance from the screen's right edge, with the money.
    const Fix16 screenX = UI_X(Fix16(640)) - Fix16(640.0f - x) * gViewCamera->mUiScale;
    DrawTexture(pTexture, screenX, UI_Y(Fix16(y)), width, height, Ang16(0), gViewCamera->mUiScale * Fix16(kClockScale), 0, 0);
}

// A car's sprite masks are cached by its delta flags only, with or without its lights apart (Lit), so
// they are built again when that changes, as the game does when LIGHTING changes.
void SetDay(f32 day)
{
    if ((day < 1.0f) != (sDay < 1.0f) && gSpriteMaskCache)
    {
        gSpriteMaskCache->InvalidateAllMasks();
    }
    sDay = day;
}
} // namespace

void SetAmbient(f32 ambient)
{
#if defined(GTA2_PORT_SDL)
    // A network game: the original's lighting, not each player's own cycle (some at night, some by day)
    if (!PortSettings::DayNightCycle() || !gpRng || bStartNetworkGame)
    {
        SetDay(0.0f);
        port_SetAmbient(ambient, ambient, ambient, 1.0f);
        return;
    }
    SetDay(Daylight());
    const f32 night = 1.0f - sDay;
    const f32 warm = 4.0f * sDay * night;
    const f32 level = ambient * night + sDay;
    port_SetAmbient(level * (1.0f + 0.10f * warm - 0.05f * night), level * (1.0f - 0.01f * warm - 0.02f * night),
                    level * (1.0f - 0.12f * warm + 0.06f * night), night);
#else
    pgbh_SetAmbient(ambient);
#endif
}

void DrawClock()
{
    if (!PortSettings::Clock() || !gpRng || !gTextureCache_15D8 || bStartNetworkGame)
    {
        return;
    }
    // The cache is new with each level, and its digit textures are handed out in turn.
    if (spClockCache != gTextureCache_15D8 || gTextureCache_15D8->mNextDigitIdx != sClockSlot + kClockSlots)
    {
        if (gTextureCache_15D8->mNextDigitIdx + kClockSlots > GTA2_COUNTOF(gTextureCache_15D8->mDigitTextures))
        {
            return;
        }
        sClockSlot = gTextureCache_15D8->RegisterDigits(kClockSlots, kMoneyDigits);
        spClockCache = gTextureCache_15D8;
    }
    const sprite_index* pSprite = gGtx_0x106C->get_sprite_index(gGtx_0x106C->GetSpriteTrueIndex(sprite_types_enum::user_6, kMoneyDigits));
    const s32 w = pSprite->mWidth;
    const s32 h = pSprite->mHeight / 11;
    const s32 dot = (w + 2) / 3;
    u32 minute;
    DayFraction(&minute);
    const s32 digits[4] = {static_cast<s32>(minute / 600), static_cast<s32>(minute / 60 % 10), static_cast<s32>(minute % 60 / 10),
                           static_cast<s32>(minute % 10)};
    const f32 k = kClockScale;
    f32 x = kClockRight - k * (4 * w + 2 * dot);
    for (s32 i = 0; i < 4; i++)
    {
        if (i == 2)
        {
            const s32 row = 10 * h + (h - dot) / 2;
            DrawGlyph(4, row, dot, dot, x + k * dot, kClockY - k * dot);
            DrawGlyph(4, row, dot, dot, x + k * dot, kClockY + k * dot);
            x += k * 2 * dot;
        }
        DrawGlyph(i, ('9' + 1 - '0' - digits[i]) * h, w, h, x + k * w / 2, kClockY);
        x += k * w;
    }
}

bool Lit()
{
    return gLighting && sDay < 1.0f;
}
} // namespace PortDayNight
