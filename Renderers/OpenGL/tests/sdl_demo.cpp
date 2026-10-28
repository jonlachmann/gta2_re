// Native SDL3 smoke test of the GL core: opens a window and draws through IRenderer like the gbh layer does.
// No game code or data. F12 toggles the ini enhancements, Esc quits.
#include "../core/vid_system.hpp"
#include "../platform/gl_context.hpp"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

int main(int, char**)
{
    const s32 w = 640, h = 480;
    const char* pError = "";
    IGLContext* pContext = CreateGLContextWindow("GTA2 GL renderer demo", w, h, &pError);
    if (!pContext)
    {
        fprintf(stderr, "window: %s\n", pError);
        return 1;
    }
    VidSystem vid; // owns pContext once attached
    vid.Init(0, 0, 0, 0);
    if (!vid.AttachContext(pContext, &pError))
    {
        fprintf(stderr, "renderer: %s\n", pError);
        return 1;
    }
    vid.SetMode(vid.CheckMode(w, h, 16), w, h);
    GLRenderer& r = vid.Renderer();

    u32 checker[64 * 64];
    for (s32 i = 0; i < 64 * 64; i++)
    {
        checker[i] = (((i % 64) / 8 + (i / 64) / 8) & 1) ? 0xFF2080F0u : 0xFFF0F0F0u; // bytes r, g, b, a
    }
    RTexture* pTex = r.CreateTexture();
    r.UploadTexture(pTex, 64, 64, checker);

    for (u32 frame = 0;; frame++)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
            {
                r.FreeTexture(pTex);
                return 0;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12)
            {
                vid.ToggleFaithful();
            }
        }

        vid.Clear(0x20, 0x20, 0x30, 0, 0, w, h);
        r.BeginScene();
        RVertex v[6] = {};
        const f32 a = frame * 0.02f, cx = w / 2.0f, cy = h / 2.0f, rad = 150.0f;
        const f32 uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const s32 idx[6] = {0, 1, 2, 0, 2, 3};
        for (s32 i = 0; i < 6; i++)
        {
            const f32 t = a + idx[i] * 1.5707963f;
            v[i].x = cx + rad * cosf(t);
            v[i].y = cy + rad * sinf(t);
            v[i].rhw = 1.0f;
            v[i].rgba = 0xFFFFFFFFu;
            v[i].u = uv[idx[i]][0];
            v[i].v = uv[idx[i]][1];
        }
        r.DrawTriangles(v, 6, pTex, RS_FILTER_LINEAR);
        // An additive, untextured triangle: what bloom picks up.
        RVertex g[3] = {};
        for (s32 i = 0; i < 3; i++)
        {
            g[i].x = 100.0f + 80.0f * cosf(-a + i * 2.0943951f);
            g[i].y = 100.0f + 80.0f * sinf(-a + i * 2.0943951f);
            g[i].rhw = 1.0f;
            g[i].rgba = 0xFF2060FFu;
        }
        r.DrawTriangles(g, 3, 0, RS_BLEND_ADD | RS_NO_TEXTURE);
        r.EndScene();
        vid.Flip();
        SDL_Delay(16);
    }
}
