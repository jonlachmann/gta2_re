// Port/net: see bot.hpp. Angles are the game's (0..1439, a full turn); facing a means moving along (-sin a, -cos a)
// (Char::GetRunVelocity_545580), and the left key turns towards larger angles.
#include "bot.hpp"
#include "../engine/engine.hpp"
#include <math.h>

namespace
{
const uint32_t kForward = 0x1, kLeft = 0x4, kRight = 0x8, kAttack = 0x10;
const float kPi = 3.14159265f;
} // namespace

uint32_t net::Bot::Think(const Snapshot& snap, int slot, uint32_t frame)
{
    const EntityState* self = 0;
    for (size_t i = 0; i < snap.entities.size(); i++)
    {
        int p = 0;
        uint32_t b = 0;
        if (engine::PlayerOf(snap.entities[i], &p, &b) && p == slot)
        {
            self = &snap.entities[i];
        }
    }
    if (!self)
    {
        return 0;
    }
    const float x = self->f[0] / 16384.0f, y = self->f[1] / 16384.0f;
    const EntityState* target = 0;
    float best = 1e9f;
    for (size_t i = 0; i < snap.entities.size(); i++)
    {
        int p = 0;
        uint32_t b = 0;
        const EntityState& e = snap.entities[i];
        if (engine::PlayerOf(e, &p, &b) && p != slot && engine::PlayerHealth(e) > 0)
        {
            const float dx = e.f[0] / 16384.0f - x, dy = e.f[1] / 16384.0f - y;
            const float d = sqrtf(dx * dx + dy * dy);
            if (d < best)
            {
                best = d;
                target = &e;
            }
        }
    }
    if (!target || best > 40.0f) // nobody near: wander, turning now and then
    {
        if (frame % 30 == 0)
        {
            mSeed = mSeed * 1103515245u + 12345u;
            const uint32_t r = mSeed >> 16;
            mWander = kForward | ((r % 4 == 0) ? kLeft : (r % 4 == 1) ? kRight : 0);
        }
        return mWander;
    }
    const float dx = target->f[0] / 16384.0f - x, dy = target->f[1] / 16384.0f - y;
    int want = (int)(atan2f(-dx, -dy) * 720.0f / kPi);
    int err = want - self->f[3]; // f[3]: the rotation
    while (err > 720)
    {
        err -= 1440;
    }
    while (err < -720)
    {
        err += 1440;
    }
    uint32_t buttons = 0;
    if (best > 1.5f)
    {
        buttons |= kForward;
    }
    if (err > 30)
    {
        buttons |= kLeft;
    }
    else if (err < -30)
    {
        buttons |= kRight;
    }
    if ((err < 0 ? -err : err) < 100 && best < 8.0f)
    {
        buttons |= kAttack;
    }
    return buttons;
}
