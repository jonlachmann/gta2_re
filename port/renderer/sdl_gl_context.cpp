#include "gl_context.hpp"
#include "gl_funcs.hpp"
#include "present_log.hpp"
#include <SDL3/SDL.h>

namespace
{
class SDLGLContext : public IGLContext
{
  public:
    SDLGLContext(SDL_Window* pWindow, SDL_GLContext ctx)
        : mWindow(pWindow), mContext(ctx), mFlush(reinterpret_cast<void (*)()>(SDL_GL_GetProcAddress("glFlush")))
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
        PresentLog::BeforeSwap();
        // SDL's Wayland swap waits for the frame callback before the commands go out: send them first, so the GPU draws during the wait.
        mFlush();
        SDL_GL_SwapWindow(mWindow);
        PresentLog::AfterSwap();
    }
    void MakeCurrent() override
    {
        SDL_GL_MakeCurrent(mWindow, mContext);
    }

  private:
    SDL_Window* mWindow;
    SDL_GLContext mContext;
    void (*mFlush)();
};
} // namespace

// OpenGL ES 3.0 first; else OpenGL 3.3 core, for Windows drivers and wine without ES contexts.
IGLContext* CreateGLContext(SDL_Window* pWindow, const char** ppError)
{
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GLContext ctx = SDL_GL_CreateContext(pWindow);
    if (!ctx)
    {
        SDL_Log("OpenGL ES 3.0: %s; trying OpenGL 3.3 core", SDL_GetError());
        SDL_GL_UnloadLibrary();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        if (SDL_GL_LoadLibrary(NULL))
        {
            ctx = SDL_GL_CreateContext(pWindow);
        }
    }
    if (!ctx)
    {
        *ppError = SDL_GetError();
        return NULL;
    }
    PFNGLGETSTRINGPROC getString = reinterpret_cast<PFNGLGETSTRINGPROC>(SDL_GL_GetProcAddress("glGetString"));
    SDL_Log("OpenGL: %s", getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : "?");
    SDL_GL_SetSwapInterval(1);
    PresentLog::Attach(pWindow);
    return new SDLGLContext(pWindow, ctx);
}
