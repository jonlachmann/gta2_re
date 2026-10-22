#include "gl_context.hpp"
#include <SDL3/SDL.h>
#include <stdio.h>

namespace
{
bool SDLCALL DropAllEvents(void*, SDL_Event*)
{
    return false;
}

class SDLGLContext : public IGLContext
{
  public:
    SDLGLContext(SDL_Window* pWindow, SDL_GLContext ctx, void* pNative, bool owned) :
        mWindow(pWindow), mContext(ctx), mNative(pNative), mOwned(owned)
    {
    }

    ~SDLGLContext()
    {
        SDL_GL_MakeCurrent(mWindow, NULL);
        SDL_GL_DestroyContext(mContext);
        if (mOwned)
        {
            SDL_DestroyWindow(mWindow);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void* GetProcAddress(const char* name)
    {
        return (void*)SDL_GL_GetProcAddress(name);
    }
    void GetDrawableSize(int* pW, int* pH)
    {
        SDL_GetWindowSizeInPixels(mWindow, pW, pH);
    }
    void SwapBuffers()
    {
        SDL_GL_SwapWindow(mWindow);
    }
    void MakeCurrent()
    {
        SDL_GL_MakeCurrent(mWindow, mContext);
    }
    void* NativeWindow()
    {
        return mNative;
    }

  private:
    SDL_Window* mWindow;
    SDL_GLContext mContext;
    void* mNative;
    bool mOwned;
};

bool InitVideo(const char** ppError)
{
    // The game owns process termination; SDL must not install SIGINT/SIGTERM handlers.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        *ppError = SDL_GetError();
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // macOS only gives a core context above 2.1 when it is forward compatible.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    return true;
}

IGLContext* Finish(SDL_Window* pWindow, void* pNative, bool owned, const char** ppError)
{
    if (!pWindow)
    {
        *ppError = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(pWindow);
    if (!ctx)
    {
        *ppError = SDL_GetError();
        if (owned)
        {
            SDL_DestroyWindow(pWindow);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    SDL_GL_MakeCurrent(pWindow, ctx);
    // The game paces itself.
    SDL_GL_SetSwapInterval(0);
    return new SDLGLContext(pWindow, ctx, pNative, owned);
}
} // namespace

IGLContext* CreateGLContextFromNativeWindow(void* pNativeWindow, const char** ppError)
{
    if (!InitVideo(ppError))
    {
        return NULL;
    }
    // The game owns the message loop on its own window; events SDL sees through its window hook are dropped.
    SDL_SetEventFilter(DropAllEvents, NULL);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, pNativeWindow);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_Window* pWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    return Finish(pWindow, pNativeWindow, false, ppError);
}

IGLContext* CreateGLContextWindow(const char* title, int w, int h, const char** ppError)
{
    if (!InitVideo(ppError))
    {
        return NULL;
    }
    SDL_Window* pWindow =
        SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    return Finish(pWindow, NULL, true, ppError);
}

IGLContext* CreateGLContextForSDLWindow(void* pSDLWindow, const char** ppError)
{
    if (!InitVideo(ppError))
    {
        return NULL;
    }
    return Finish((SDL_Window*)pSDLWindow, NULL, false, ppError);
}
