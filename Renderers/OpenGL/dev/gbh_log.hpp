// Development aid (README "Development tools"): the gbh_* layer's side of the command log (GTA2GL_CMDLOG, cmdlog.hpp),
// in the reference log's fields, so the two can be diffed draw by draw. Nothing is written unless it is set.
#pragma once

#include "../core/gta2_api.hpp"

void GbhLog_Init(const u32* pFlipCounter); // the number of Vid_FlipBuffers calls so far
void GbhLog_Call(const char* func, u32 flags, STexture* pTexture, u32 colour); // a gbh_* call made by the game
// The primitive it gave the renderer: <state> the renderer's RS_* bits, <blit> an exact image copy
void GbhLog_Prim(const Vert* pVerts, s32 count, u32 state, STexture* pTexture, bool blit);
