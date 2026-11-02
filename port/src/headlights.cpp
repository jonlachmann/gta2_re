#include "headlights.hpp"
#include "Car.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "ang16.hpp"
#include "gbh_graphics.hpp"
#include "registry.hpp"
#include "sprite.hpp"
#include "3rdParty/GTA2Hax/d3ddll/d3ddll.hpp"

namespace PortHeadlights
{
void Submit(s32 min_x, s32 min_y, s32 max_x, s32 max_y)
{
    if (!gCar_BC_Pool || !gRegistry.Get_Screen_Setting("car_headlights", 1))
    {
        return;
    }
    // Bit 16 switches the light on, the second byte is the radius (blocks * 32), the first the intensity.
    const u32 kRadius = 3 * 32;
    const u32 kIntensity = 160;
    const u32 kColour = 0xFFFFF2D8;
    for (Car* pCar = gCar_BC_Pool->GetFirstCar_420E50(); pCar; pCar = pCar->mpNext)
    {
        if (!pCar->mCarSprite || !pCar->mDriver)
        {
            continue;
        }
        const f32 x = pCar->GetX().ToFloat();
        const f32 y = pCar->GetY().ToFloat();
        if (x < min_x - 4 || x > max_x + 4 || y < min_y - 4 || y > max_y + 4)
        {
            continue;
        }
        // The sprite's facing: angle 0 points up the screen (-y).
        const Ang16 angle = pCar->mCarSprite->mRotation;
        const f32 fx = -Ang16::sine(angle).ToFloat();
        const f32 fy = -Ang16::cosine(angle).ToFloat();
        for (s32 side = -1; side <= 1; side += 2)
        {
            SLight light;
            light.field_0 = 0x10000 | (kRadius << 8) | kIntensity;
            light.field_4_x = x + fx * 1.6f + fy * 0.35f * side;
            light.field_8_y = y + fy * 1.6f - fx * 0.35f * side;
            light.field_C_z = pCar->GetZ().ToFloat();
            light.field_10_colour = kColour;
            pgbh_AddLight(&light);
        }
    }
}
} // namespace PortHeadlights
