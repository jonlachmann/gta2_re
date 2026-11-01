#include "pause_menu.hpp"
#include "Camera.hpp"
#include "Draw.hpp"
#include "Frontend.hpp"
#include "Function.hpp"
#include "Game.hpp"
#include "Globals.hpp"
#include "Hud.hpp"
#include "InputRecorder_67F8B0.hpp"
#include "TextTable.hpp"
#include "debug.hpp"
#include "gbh_graphics.hpp"
#include "options_menu.hpp"
#include "registry.hpp"
#include "root_sound.hpp"
#include <stdio.h>
#include <string.h>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

EXTERN_GLOBAL(s16, word_7064D8);

void __stdcall SetStartMode(s32 startMode);
void __stdcall SetVideoModeFromSettings();
void OnVideoModeChanged();
char_type IsWindowedModeAvailable();
#if defined(GTA2_PORT_SDL)
void port_CaptureStart();
int port_CapturedKey();
int port_CapturedPad();
#endif

namespace PortPauseMenu
{
namespace
{
enum Item
{
    kResume,
    kSfx,
    kMusic,
    kBrightness,
    kDisplay,
    kTextSpeed,
    kControls,
    kQuit,
    kNumItems,
};

const wchar_t* const kItemNames[kNumItems] = {L"RESUME", L"SOUND EFFECTS", L"MUSIC", L"BRIGHTNESS",
                                              L"DISPLAY", L"TEXT SPEED", L"CONTROLS", L"QUIT GAME"};

bool sOpen;
bool sControlsPage;
s32 sSelected;
s32 sControlSelected; // NumControls() is the BACK line
s32 sColumn;          // 0 keyboard, 1 gamepad
bool sCapturing;

f32 Scale()
{
    return gViewCamera->mUiScale.mValue / 16384.0f;
}

// The 640x480 layout centred on the screen.
f32 PxX(f32 x)
{
    return x * Scale() + (gViewCamera->mScreenPxWidth - 640.0f * Scale()) / 2;
}

f32 PxY(f32 y)
{
    return y * Scale() + (gViewCamera->mScreenPxHeight - 480.0f * Scale()) / 2;
}

Fix16 Fx(f32 v)
{
    return Fix16(static_cast<s32>(v * 16384.0f), 0);
}

void Text(const wchar_t* pText, f32 x, f32 y, bool selected)
{
    // Font palette 5 is the frontend's red, for the selected line.
    const s32 paletteType = selected ? palette_types_enum::font_remaps_8 : palette_types_enum::sprites_2;
    DrawString(pText, Fx(PxX(x)), Fx(PxY(y)), static_cast<u16>(word_7064D8), gViewCamera->mUiScale, paletteType,
               selected ? 5 : 0, 0, 0);
}

void Panel(f32 left, f32 top, f32 right, f32 bottom)
{
    Vert verts[4] = {};
    verts[0].x = PxX(left);
    verts[0].y = PxY(top);
    verts[1].x = PxX(right);
    verts[1].y = PxY(top);
    verts[2].x = PxX(right);
    verts[2].y = PxY(bottom);
    verts[3].x = PxX(left);
    verts[3].y = PxY(bottom);
    pgbh_DrawFlatRect(verts, 0);
}

s32 Clamp(s32 v, s32 lo, s32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void Change(s32 item, s32 delta)
{
    switch (item)
    {
        case kSfx:
        case kMusic:
        {
            const char* name = item == kSfx ? "SFXVol" : "CDVol";
            const s32 step = Clamp((gRegistry.Get_Sound_Setting(name, 127) * 10 + 63) / 127 + delta, 0, 10);
            const s32 volume = step * 127 / 10;
            gRegistry.Set_Sound_Setting(name, volume);
            if (item == kSfx)
            {
                gRoot_sound.SetSfxVol(static_cast<u8>(volume));
            }
            else
            {
                gRoot_sound.SetCDVol(static_cast<u8>(volume));
            }
            break;
        }
        case kBrightness:
        {
            const s32 gamma = Clamp(gRegistry.Get_Screen_Setting("gamma", 10) + delta, 1, 20);
            gRegistry.Set_Screen_Setting("gamma", gamma);
            SetGamma(gamma);
            break;
        }
        case kDisplay:
            if (IsWindowedModeAvailable())
            {
                SetStartMode(gRegistry.Get_Screen_Setting("start_mode", 1) ? 0 : 1);
                SetVideoModeFromSettings();
                OnVideoModeChanged();
            }
            break;
        case kTextSpeed:
        {
            const DWORD speed = static_cast<DWORD>(Clamp(gRegistry.Get_Option_Int("text_speed", 3) + delta, 1, 5));
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Option", 0, NULL, 0,
                                KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS)
            {
                RegSetValueExA(hKey, "text_speed", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&speed), sizeof(speed));
                RegCloseKey(hKey);
            }
            gHud_2B00->GetTextSpeed();
            break;
        }
        default:
            break;
    }
}

void Value(s32 item, wchar_t* pOut)
{
    pOut[0] = 0;
    switch (item)
    {
        case kSfx:
        case kMusic:
            swprintf(pOut, L"%d", (gRegistry.Get_Sound_Setting(item == kSfx ? "SFXVol" : "CDVol", 127) * 10 + 63) / 127);
            break;
        case kBrightness:
            swprintf(pOut, L"%d", gRegistry.Get_Screen_Setting("gamma", 10));
            break;
        case kDisplay:
            wcscpy(pOut, gRegistry.Get_Screen_Setting("start_mode", 1) ? L"FULL SCREEN" : L"WINDOW");
            break;
        case kTextSpeed:
            wcscpy(pOut, PortOptionsMenu::TextSpeedName(gRegistry.Get_Option_Int("text_speed", 3)));
            break;
        default:
            break;
    }
}

void Close()
{
    sOpen = false;
    sControlsPage = false;
    sCapturing = false;
    if (gGame_0x40->IsPaused())
    {
        gGame_0x40->Unpause();
    }
}

// The keyboard's bindings are read when a game starts; the pad's at each press.
void Rebind(s32 binding)
{
    PortOptionsMenu::SetBinding(sColumn, sControlSelected, binding);
    if (gInputDevices)
    {
        gInputDevices->get_registry_controls();
    }
    sCapturing = false;
}

bool HandleControlsKey(s32 action)
{
    const s32 count = PortOptionsMenu::NumControls();
    switch (action)
    {
        case DIK_UP:
            sControlSelected = (sControlSelected + count) % (count + 1);
            break;
        case DIK_DOWN:
            sControlSelected = (sControlSelected + 1) % (count + 1);
            break;
        case DIK_LEFT:
        case DIK_RIGHT:
#if defined(GTA2_PORT_SDL)
            sColumn = action == DIK_RIGHT ? 1 : 0;
#endif
            break;
        case DIK_RETURN:
            if (sControlSelected == count)
            {
                sControlsPage = false;
            }
            else
            {
#if defined(GTA2_PORT_SDL)
                sCapturing = true;
                port_CaptureStart();
#endif
            }
            break;
        case DIK_ESCAPE:
            sControlsPage = false;
            break;
        default:
            break;
    }
    return true;
}
} // namespace

bool IsOpen()
{
    return sOpen;
}

bool HandleKey(s32 action, Player* pPlayer)
{
    if (!sOpen)
    {
        if (action != DIK_ESCAPE || pPlayer->mShowQuitMessage || bSkip_quit_confirm || !pPlayer->IsUser())
        {
            return false;
        }
        sOpen = true;
        sSelected = kResume;
        if (!gGame_0x40->IsPaused())
        {
            gGame_0x40->Pause();
        }
        return true;
    }
    if (sCapturing)
    {
        return true;
    }
    if (sControlsPage)
    {
        return HandleControlsKey(action);
    }
    switch (action)
    {
        case DIK_UP:
            sSelected = (sSelected + kNumItems - 1) % kNumItems;
            break;
        case DIK_DOWN:
            sSelected = (sSelected + 1) % kNumItems;
            break;
        case DIK_LEFT:
        case DIK_RIGHT:
            Change(sSelected, action == DIK_LEFT ? -1 : 1);
            break;
        case DIK_RETURN:
            if (sSelected == kResume)
            {
                Close();
            }
            else if (sSelected == kControls)
            {
                sControlsPage = true;
                sControlSelected = 0;
            }
            else if (sSelected == kQuit)
            {
                Close();
                gHud_2B00->mQuitMessage.ShowQuitMessage(pPlayer);
            }
            else
            {
                Change(sSelected, 1);
            }
            break;
        case DIK_ESCAPE:
            Close();
            break;
        default:
            break;
    }
    return true;
}

void Draw()
{
    if (!sOpen)
    {
        return;
    }
#if defined(GTA2_PORT_SDL)
    if (sCapturing)
    {
        // Escape cancels; B cancels on the keyboard column, Back on the pad column.
        const int key = port_CapturedKey();
        const int pad = port_CapturedPad();
        if (key == DIK_ESCAPE || (sColumn == 0 && pad == 1) || (sColumn == 1 && pad == 6))
        {
            sCapturing = false;
        }
        else if (sColumn == 0 && key > 0)
        {
            Rebind(key);
        }
        else if (sColumn == 1 && pad >= 0)
        {
            Rebind(pad);
        }
    }
#endif

    // The original's pause banner, then the menu below it.
    DrawFigureScaled(sprite_types_enum::user_6, 134, 227, 120, Ang16(0), palette_types_enum::sprites_2, 0, 0, 0);
    DrawFigureScaled(sprite_types_enum::user_6, 136, 320, 120, Ang16(0), palette_types_enum::sprites_2, 0, 0, 0);
    DrawFigureScaled(sprite_types_enum::user_6, 135, 413, 120, Ang16(0), palette_types_enum::sprites_2, 0, 0, 0);
    wchar_t* pPause = gText_0x14->Find("pause");
    Text(pPause, 320.0f - Frontend::GetMaxTextWidth(pPause, static_cast<u16>(word_7064D8)) / 2.0f, 98, false);

    wchar_t value[32];
    if (!sControlsPage)
    {
        Panel(150, 165, 490, 165 + kNumItems * 20 + 10);
        for (s32 i = 0; i < kNumItems; i++)
        {
            const f32 y = 172.0f + i * 20;
            Text(kItemNames[i], 170, y, i == sSelected);
            Value(i, value);
            Text(value, 360, y, i == sSelected);
        }
        return;
    }

    const s32 count = PortOptionsMenu::NumControls();
    Panel(60, 160, 580, 160 + (count + 2) * 18 + 10);
    Text(L"KEYBOARD", 300, 165, false);
#if defined(GTA2_PORT_SDL)
    Text(L"GAMEPAD", 460, 165, false);
#endif
    wchar_t key[24];
    wchar_t pad[24];
    for (s32 i = 0; i <= count; i++)
    {
        const f32 y = 183.0f + i * 18;
        const bool selected = i == sControlSelected;
        if (i == count)
        {
            Text(L"BACK", 80, y, selected);
            break;
        }
        PortOptionsMenu::BindingNames(i, key, pad);
        if (selected && sCapturing)
        {
            wcscpy(sColumn ? pad : key, L"PRESS");
        }
        Text(PortOptionsMenu::ControlName(i), 80, y, selected);
        Text(key, 300, y, selected && sColumn == 0);
#if defined(GTA2_PORT_SDL)
        Text(pad, 460, y, selected && sColumn == 1);
#endif
    }
}
} // namespace PortPauseMenu
