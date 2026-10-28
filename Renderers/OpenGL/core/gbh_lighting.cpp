// The gbh_* layer's lights (gbh_internal.hpp): gbh_AddLight's, applied per vertex as the original does, or per pixel
// (an enhancement).
#include "gbh_internal.hpp"
#include <math.h>

namespace gbh
{
namespace
{
struct LightInternal
{
    u32 mFlags;
    f32 mBrightness;
    f32 mRadius;
    f32 mRadiusSquared;
    f32 mInvRadius;
    f32 mX, mY, mZ;
    f32 mR, mG, mB;
};

LightInternal gLights[256];
u32 gNumLights;
f32 gAmbient = 255.0f;
bool gLightsDirty = true;

// Enhancement: the active lights for per pixel lighting.
void UploadLights()
{
    static RLight sLights[256];
    s32 n = 0;
    for (u32 j = 0; j < gNumLights; j++)
    {
        const LightInternal& l = gLights[j];
        if ((l.mFlags & 0x30000) != 0x10000)
        {
            continue;
        }
        RLight& o = sLights[n++];
        o.x = l.mX, o.y = l.mY, o.z = l.mZ;
        o.radius = l.mRadius;
        o.invRadius = l.mInvRadius;
        o.r = l.mR * l.mBrightness, o.g = l.mG * l.mBrightness, o.b = l.mB * l.mBrightness;
    }
    gRenderer->SetLights(sLights, n, gAmbient);
    gLightsDirty = false;
}

// The original's table driven square root (12 bit mantissa index, 4096 entries).
u32 gSqrtTable[4096];

void InitSqrtTable()
{
    for (u32 i = 0; i < 2048; i++)
    {
        union
        {
            u32 u;
            f32 f;
        } a, b;
        a.u = (i << 12) | 0x3F800000u; // [1, 2)
        b.u = (i << 12) | 0x40000000u; // [2, 4)
        a.f = (f32)sqrt((double)a.f);
        b.f = (f32)sqrt((double)b.f);
        gSqrtTable[i] = a.u & 0x7FFFFF;
        gSqrtTable[2048 + i] = b.u & 0x7FFFFF;
    }
}

f32 TableSqrt(f32 x)
{
    union
    {
        u32 u;
        f32 f;
    } v;
    v.f = x;
    u32 mant = v.u & 0x7FFFFF;
    u32 exp = (v.u & 0x7F800000u) >> 1;
    if (exp & 0x400000)
    {
        exp += 0x20000000u;
    }
    else
    {
        exp += 0x1F800000u;
        mant |= 0x800000;
    }
    v.u = gSqrtTable[mant >> 12] | (exp & 0x7F800000u);
    return v.f;
}
} // namespace

void InitLighting()
{
    InitSqrtTable();
}

bool LightingOn()
{
    return gAmbient != 255.0f;
}

u32 LightVertex(const Vert& vert, u8 shade, u32 colour)
{
    f32 lr = 0.0f, lg = 0.0f, lb = 0.0f;
    for (u32 j = 0; j < gNumLights; j++)
    {
        const LightInternal& light = gLights[j];
        if ((light.mFlags & 0x30000) != 0x10000)
        {
            continue;
        }
        const f32 dx = vert.x - light.mX;
        const f32 dy = vert.y - light.mY;
        const f32 dz = vert.z - light.mZ;
        const f32 d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > light.mRadiusSquared)
        {
            continue;
        }
        const f32 att = (light.mRadius - TableSqrt(d2)) * light.mInvRadius;
        if (att > 0.0f)
        {
            const f32 k = att * light.mBrightness;
            lr += light.mR * k;
            lg += light.mG * k;
            lb += light.mB * k;
        }
    }
    const f32 n = shade / 255.0f;
    f32 r = n * ((colour >> 16) & 0xFF) * lr + gAmbient;
    f32 g = n * ((colour >> 8) & 0xFF) * lg + gAmbient;
    f32 b = n * (colour & 0xFF) * lb + gAmbient;
    const u32 ri = r > 255.0f ? 255 : (u32)r;
    const u32 gi = g > 255.0f ? 255 : (u32)g;
    const u32 bi = b > 255.0f ? 255 : (u32)b;
    return (colour & 0xFF000000u) | (ri << 16) | (gi << 8) | bi;
}

void MarkLightsDirty()
{
    gLightsDirty = true;
}

void UploadLightsIfDirty()
{
    if (gLightsDirty)
    {
        UploadLights();
    }
}
} // namespace gbh

using namespace gbh;

GBH_EXPORT void GTA2_CC gbh_ResetLights()
{
    gNumLights = 0;
    gLightsDirty = true;
}

GBH_EXPORT s32 GTA2_CC gbh_AddLight(SLight* pLight)
{
    if (gNumLights >= 256)
    {
        return 0;
    }
    const u32 idx = gNumLights++;
    gLightsDirty = true;
    LightInternal& l = gLights[idx];
    const f32 k = 1.0f / 255.0f;
    l.mFlags = pLight->field_0;
    l.mBrightness = (pLight->field_0 & 0xFF) * k;
    l.mX = pLight->field_4_x;
    l.mY = pLight->field_8_y;
    l.mZ = pLight->field_C_z;
    l.mR = ((pLight->field_10_colour >> 16) & 0xFF) * k;
    l.mG = ((pLight->field_10_colour >> 8) & 0xFF) * k;
    l.mB = (pLight->field_10_colour & 0xFF) * k;
    l.mRadius = ((pLight->field_0 >> 8) & 0xFF) * k * 8.0f;
    l.mRadiusSquared = l.mRadius * l.mRadius;
    l.mInvRadius = l.mRadius > 0.0f ? 1.0f / l.mRadius : 0.0f;
    return idx * 0x2C;
}

GBH_EXPORT void GTA2_CC gbh_SetAmbient(f32 ambient)
{
    gAmbient = ambient * 255.0f;
    gLightsDirty = true;
}
