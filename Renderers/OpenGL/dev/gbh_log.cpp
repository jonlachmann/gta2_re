#include "gbh_log.hpp"
#include "../core/renderer.hpp"
#include "cmdlog.hpp"

namespace
{
CmdLog gCmd;
const u32* gFlipCounter;
} // namespace

void GbhLog_Init(const u32* pFlipCounter)
{
    gFlipCounter = pFlipCounter;
    gCmd.Init("gl");
}

void GbhLog_Call(const char* func, u32 flags, STexture* pTexture, u32 colour)
{
    if (gFlipCounter)
    {
        gCmd.Update(*gFlipCounter);
    }
    gCmd.Call(func,
              flags,
              pTexture ? pTexture->field_0_id : 0,
              pTexture ? pTexture->field_E_width : 0,
              pTexture ? pTexture->field_10_height : 0,
              colour & 0xFF);
}

// Same fields as the reference log of the D3D calls: D3D render state values, texel units.
void GbhLog_Prim(const Vert* pVerts, s32 count, u32 state, STexture* pTexture, bool blit)
{
    if (!gCmd.Active())
    {
        return;
    }
    u32 st[5] = {0, 0, 0, 2, 2};
    if (state & RS_BLEND_ADD)
    {
        st[0] = 1, st[1] = 2, st[2] = 2;
    }
    else if (state & RS_BLEND_ALPHA)
    {
        st[0] = 1, st[1] = 5, st[2] = 6;
    }
    if (!(state & RS_FILTER_LINEAR))
    {
        st[3] = st[4] = 1;
    }
    gCmd.Prim(blit ? "blit" : (count == 4 ? "fan" : "list"),
              count,
              st,
              pTexture ? pTexture->field_0_id : 0,
              pTexture ? pTexture->field_E_width : 0,
              pTexture ? pTexture->field_10_height : 0);
    for (s32 i = 0; i < count; i++)
    {
        gCmd.Vertex(pVerts[i].x, pVerts[i].y, pVerts[i].z, pVerts[i].w, pVerts[i].diff, pVerts[i].spec, pVerts[i].u, pVerts[i].v);
    }
}
