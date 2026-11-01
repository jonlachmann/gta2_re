#include "gl_context.hpp"
#include <SDL3/SDL.h>

namespace
{
class SDLGLContext : public IGLContext
{
  public:
    SDLGLContext(SDL_Window* pWindow, SDL_GLContext ctx) : mWindow(pWindow), mContext(ctx)
    {
    }

    ~SDLGLContext()
    {
        SDL_GL_MakeCurrent(mWindow, NULL);
        SDL_GL_DestroyContext(mContext);
    }

    void* GetProcAddress(const char* name) override
    {
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }
    void GetDrawableSize(int* pW, int* pH) override
    {
        SDL_GetWindowSizeInPixels(mWindow, pW, pH);
    }
    void SwapBuffers() override
    {
        SDL_GL_SwapWindow(mWindow);
    }
    void MakeCurrent() override
    {
        SDL_GL_MakeCurrent(mWindow, mContext);
    }

  private:
    SDL_Window* mWindow;
    SDL_GLContext mContext;
};
} // namespace

IGLContext* CreateGLContext(SDL_Window* pWindow, const char** ppError)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GLContext ctx = SDL_GL_CreateContext(pWindow);
    if (!ctx)
    {
        *ppError = SDL_GetError();
        return NULL;
    }
    SDL_GL_SetSwapInterval(1);
    return new SDLGLContext(pWindow, ctx);
}
