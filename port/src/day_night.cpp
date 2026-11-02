#include "day_night.hpp"
#include "gbh_graphics.hpp"
#include "rng.hpp"
#include "settings.hpp"
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

// Dawn from 5 to 7, dusk from 18 to 20; exactly 1 by day and 0 by night.
f32 Daylight()
{
    const u32 dayTicks = PortSettings::DayLength() * kTicksPerMinute;
    const u32 tick = (static_cast<u32>(gpRng->get_cur_rng()) + PortSettings::StartTime() * dayTicks / 24) % dayTicks;
    const f32 hour = 24.0f * tick / dayTicks;
    return SmoothStep(5.0f, 7.0f, hour) - SmoothStep(18.0f, 20.0f, hour);
}
} // namespace

void SetAmbient(f32 ambient)
{
#if defined(GTA2_PORT_SDL)
    if (!PortSettings::DayNightCycle() || !gpRng)
    {
        sDay = 0.0f;
        port_SetAmbient(ambient, ambient, ambient, 1.0f);
        return;
    }
    sDay = Daylight();
    const f32 night = 1.0f - sDay;
    const f32 warm = 4.0f * sDay * night;
    const f32 level = ambient * night + sDay;
    port_SetAmbient(level * (1.0f + 0.10f * warm - 0.05f * night), level * (1.0f - 0.01f * warm - 0.02f * night),
                    level * (1.0f - 0.12f * warm + 0.06f * night), night);
#else
    pgbh_SetAmbient(ambient);
#endif
}

bool Lit()
{
    return gLighting && sDay < 1.0f;
}
} // namespace PortDayNight
