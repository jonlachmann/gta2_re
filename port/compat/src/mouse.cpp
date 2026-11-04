// The mouse, for the menus and TWIN STICK's aim: its position in the game's frame and the frame's clicks
// and wheel turns; the cursor shows while the mouse is used over a menu.
#include "port_platform.hpp"

namespace
{
float sX;
float sY;
bool sMoved;
int sLeft;
bool sLeftDown;
int sRight;
float sWheel;
bool sCursorShown = true;
} // namespace

void port_MouseEvent(const SDL_Event& e)
{
    switch (e.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
            sX = e.motion.x;
            sY = e.motion.y;
            sMoved |= e.motion.xrel != 0 || e.motion.yrel != 0;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            sX = e.button.x;
            sY = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT)
            {
                sLeft++;
                sLeftDown = true;
            }
            else if (e.button.button == SDL_BUTTON_RIGHT)
            {
                sRight++;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT)
            {
                sLeftDown = false;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            sLeftDown = false;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            sWheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            break;
        default:
            break;
    }
}

void port_MousePos(float* pX, float* pY)
{
    // The frame is drawn at its aspect ratio, centred in the window (GLRenderer::Present).
    int windowW = 0;
    int windowH = 0;
    int frameW = 640;
    int frameH = 480;
    if (SDL_Window* pWindow = port_MainSDLWindow())
    {
        SDL_GetWindowSize(pWindow, &windowW, &windowH);
    }
    port_FrameSize(&frameW, &frameH);
    float scale = 1.0f;
    if (windowW > 0 && windowH > 0 && frameW > 0 && frameH > 0)
    {
        const float scaleX = static_cast<float>(windowW) / frameW;
        const float scaleY = static_cast<float>(windowH) / frameH;
        scale = scaleX < scaleY ? scaleX : scaleY;
    }
    *pX = (sX - (windowW - frameW * scale) / 2) / scale;
    *pY = (sY - (windowH - frameH * scale) / 2) / scale;
}

bool port_TakeMouse(float* pX, float* pY, int* pLeft, int* pRight, int* pWheel)
{
    port_MousePos(pX, pY);
    *pLeft = sLeft;
    *pRight = sRight;
    *pWheel = static_cast<int>(sWheel);
    const bool moved = sMoved;
    sWheel -= *pWheel;
    sMoved = false;
    sLeft = 0;
    sRight = 0;
    return moved;
}

bool port_MouseLeftDown()
{
    return sLeftDown;
}

void port_MouseEndFrame()
{
    sMoved = false;
    sLeft = 0;
    sRight = 0;
    sWheel = 0;

    static bool sCursorSet;
    if (!sCursorSet)
    {
        sCursorSet = true;
        port_SetGameCursor();
    }
    // TWIN STICK aims with the mouse: kept in the window, the HUD's reticle in place of the cursor.
    static bool sGrabbed;
    const bool grab = port_GameRunning() && !port_MenuUsesMouse() && port_TwinStick() == 1;
    SDL_Window* pWindow = port_MainSDLWindow();
    if (grab != sGrabbed && pWindow)
    {
        sGrabbed = grab;
        SDL_SetWindowMouseGrab(pWindow, grab);
    }
    const bool show = port_MouseMode() && port_MenuUsesMouse();
    if (show != sCursorShown)
    {
        sCursorShown = show;
        show ? SDL_ShowCursor() : SDL_HideCursor();
    }
}
