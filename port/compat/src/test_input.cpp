// Test tools driven by environment variables, for runs nobody watches:
//   GTA2_INPUT=<frame>:<key>[:<frames held>],...   SDL key names ("Return", "Down", "Escape", "A"),
//                                                   "Alt+" for the Alt modifier ("Alt+Return"),
//                                                   "pad.<button>" or "pad.<axis>+/-" for a virtual
//                                                   gamepad ("pad.a", "pad.dpup", "pad.leftx-"),
//                                                   or the mouse: "mouse.<x>x<y>" moves it to that
//                                                   window position, "mouse.left" and "mouse.right"
//                                                   click, "mouse.wheelup" and "mouse.wheeldown" turn
//                                                   the wheel one notch
//   GTA2_EXIT_AT=<frame>                            quit at that frame
// Frames count Vid_FlipBuffers calls, but logic steps while the game runs (port_FrameCount), so the same
// script presses the same keys on the same game step at any frame rate.
#include "port_platform.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
struct KeyEvent
{
    unsigned mFrame;
    SDL_Scancode mScancode;
    SDL_Keymod mMod;
    bool mDown;
    int mPadButton; // -1: keyboard
    int mPadAxis;   // -1: not an axis
    Sint16 mAxisValue;
    Uint32 mMouseEvent; // 0: not the mouse
    float mX;           // motion: the position; button: which; wheel: the notches
    float mY;
};

SDL_Joystick* sPad;

// The virtual gamepad exists before the game enumerates its DirectInput devices.
void AttachPad()
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "GTA2 test pad";
    SDL_setenv_unsafe("GTA2_GAMEPAD", desc.name, 1);
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    sPad = id ? SDL_OpenJoystick(id) : NULL;
    if (!sPad)
    {
        SDL_Log("GTA2_INPUT: no virtual gamepad: %s", SDL_GetError());
    }
}

std::vector<KeyEvent>& Events()
{
    static std::vector<KeyEvent> sEvents;
    return sEvents;
}

unsigned sExitAt;

void Load()
{
    if (const char* exitAt = getenv("GTA2_EXIT_AT"))
    {
        sExitAt = (unsigned)atoi(exitAt);
    }
    const char* spec = getenv("GTA2_INPUT");
    if (!spec)
    {
        return;
    }
    std::string s(spec);
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t end = s.find(',', pos);
        if (end == std::string::npos)
        {
            end = s.size();
        }
        const std::string item = s.substr(pos, end - pos);
        pos = end + 1;
        const size_t c1 = item.find(':');
        if (c1 == std::string::npos)
        {
            continue;
        }
        const size_t c2 = item.find(':', c1 + 1);
        const unsigned frame = (unsigned)atoi(item.c_str());
        std::string key = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
        SDL_Keymod mod = SDL_KMOD_NONE;
        if (key.compare(0, 4, "Alt+") == 0)
        {
            mod = SDL_KMOD_LALT;
            key = key.substr(4);
        }
        const unsigned held = c2 == std::string::npos ? 2 : (unsigned)atoi(item.c_str() + c2 + 1);
        if (key.compare(0, 6, "mouse.") == 0)
        {
            const std::string name = key.substr(6);
            const KeyEvent mouse = {frame, SDL_SCANCODE_UNKNOWN, mod, true, -1, -1, 0, 0, 0, 0};
            KeyEvent e = mouse;
            if (name == "left" || name == "right")
            {
                e.mMouseEvent = SDL_EVENT_MOUSE_BUTTON_DOWN;
                e.mX = name == "left" ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
                Events().push_back(e);
                e.mFrame = frame + held;
                e.mDown = false;
                e.mMouseEvent = SDL_EVENT_MOUSE_BUTTON_UP;
            }
            else if (name == "wheelup" || name == "wheeldown")
            {
                e.mMouseEvent = SDL_EVENT_MOUSE_WHEEL;
                e.mX = name == "wheelup" ? 1.0f : -1.0f;
            }
            else if (sscanf(name.c_str(), "%fx%f", &e.mX, &e.mY) == 2)
            {
                e.mMouseEvent = SDL_EVENT_MOUSE_MOTION;
            }
            else
            {
                SDL_Log("GTA2_INPUT: unknown mouse input %s", key.c_str());
                continue;
            }
            Events().push_back(e);
            continue;
        }
        if (key.compare(0, 4, "pad.") == 0)
        {
            std::string name = key.substr(4);
            int axis = -1;
            Sint16 value = 0;
            int button = -1;
            if (!name.empty() && (name.back() == '+' || name.back() == '-'))
            {
                value = name.back() == '+' ? 32767 : -32768;
                name.pop_back();
                axis = SDL_GetGamepadAxisFromString(name.c_str());
            }
            else
            {
                button = SDL_GetGamepadButtonFromString(name.c_str());
            }
            if (axis < 0 && button < 0)
            {
                SDL_Log("GTA2_INPUT: unknown pad input %s", key.c_str());
                continue;
            }
            if (!sPad)
            {
                AttachPad();
            }
            Events().push_back(KeyEvent{frame, SDL_SCANCODE_UNKNOWN, mod, true, button, axis, value, 0, 0, 0});
            Events().push_back(KeyEvent{frame + held, SDL_SCANCODE_UNKNOWN, mod, false, button, axis, 0, 0, 0, 0});
            continue;
        }
        const SDL_Scancode sc = SDL_GetScancodeFromName(key.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN)
        {
            SDL_Log("GTA2_INPUT: unknown key %s", key.c_str());
            continue;
        }
        Events().push_back(KeyEvent{frame, sc, mod, true, -1, -1, 0, 0, 0, 0});
        Events().push_back(KeyEvent{frame + held, sc, mod, false, -1, -1, 0, 0, 0, 0});
    }
}
float sMouseX;
float sMouseY;

void InjectMouse(const KeyEvent& event)
{
    SDL_Event e = {};
    e.type = event.mMouseEvent;
    SDL_Window* pWindow = port_MainSDLWindow();
    const SDL_WindowID window = pWindow ? SDL_GetWindowID(pWindow) : 0;
    switch (event.mMouseEvent)
    {
        case SDL_EVENT_MOUSE_MOTION:
            e.motion.windowID = window;
            e.motion.x = event.mX;
            e.motion.y = event.mY;
            e.motion.xrel = event.mX - sMouseX;
            e.motion.yrel = event.mY - sMouseY;
            sMouseX = event.mX;
            sMouseY = event.mY;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            e.wheel.windowID = window;
            e.wheel.y = event.mX;
            e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            e.wheel.mouse_x = sMouseX;
            e.wheel.mouse_y = sMouseY;
            break;
        default:
            e.button.windowID = window;
            e.button.button = static_cast<Uint8>(event.mX);
            e.button.down = event.mDown;
            e.button.clicks = 1;
            e.button.x = sMouseX;
            e.button.y = sMouseY;
            break;
    }
    SDL_PushEvent(&e);
}
} // namespace

void port_InitTestInput()
{
    Load();
}

void port_InjectTestInput(unsigned frame)
{
    for (auto it = Events().begin(); it != Events().end();)
    {
        if (it->mFrame > frame)
        {
            ++it;
            continue;
        }
        if (it->mMouseEvent)
        {
            InjectMouse(*it);
            SDL_Log("test input: frame %u mouse %u %g %g", frame, it->mMouseEvent, it->mX, it->mY);
            it = Events().erase(it);
            continue;
        }
        if (it->mPadAxis >= 0 || it->mPadButton >= 0)
        {
            if (it->mPadAxis >= 0)
            {
                SDL_SetJoystickVirtualAxis(sPad, it->mPadAxis, it->mAxisValue);
            }
            else
            {
                SDL_SetJoystickVirtualButton(sPad, it->mPadButton, it->mDown);
            }
            SDL_Log("test input: frame %u pad %d/%d %s", frame, it->mPadButton, it->mPadAxis, it->mDown ? "down" : "up");
            it = Events().erase(it);
            continue;
        }
        SDL_Event e = {};
        e.type = it->mDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = it->mScancode;
        e.key.key = SDL_GetKeyFromScancode(it->mScancode, SDL_KMOD_NONE, false);
        e.key.mod = it->mMod;
        e.key.down = it->mDown;
        SDL_PushEvent(&e);
        SDL_Log("test input: frame %u %s %s", frame, SDL_GetScancodeName(it->mScancode), it->mDown ? "down" : "up");
        it = Events().erase(it);
    }
    if (sExitAt && frame >= sExitAt)
    {
        sExitAt = 0;
        SDL_Event e = {};
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
    }
}
