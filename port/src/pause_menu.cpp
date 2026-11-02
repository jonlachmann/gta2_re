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
#include "settings.hpp"
#include <stdio.h>
#include <string.h>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

EXTERN_GLOBAL(s16, word_7064D8);

#if defined(GTA2_PORT_SDL)
void port_CaptureStart();
int port_CapturedKey();
int port_CapturedPad();
#endif

namespace PortPauseMenu
{
namespace
{
enum Page
{
    kMainPage,
    kScreenPage,
    kGraphicsPage,
    kControlsPage,
};

// A line of a page: a setting, or one of these.
enum Action
{
    kResume = PortSettings::kCount,
    kScreen,
    kGraphics,
    kControls,
    kQuit,
    kBack,
};

const s32 kMaxLines = 16;

// The lines of a page; returns their number.
s32 Lines(Page page, s32* pLines)
{
    s32 n = 0;
    if (page == kMainPage)
    {
        pLines[n++] = kResume;
    }
    const PortSettings::Id* pIds = page == kMainPage ? PortSettings::kGeneral
                                   : page == kScreenPage ? PortSettings::kScreen
                                                         : PortSettings::kGraphics;
    const s32 count = page == kMainPage ? PortSettings::kNumGeneral
                      : page == kScreenPage ? PortSettings::kNumScreen
                                            : PortSettings::kNumGraphics;
    for (s32 i = 0; i < count; i++)
    {
        pLines[n++] = pIds[i];
    }
    if (page == kMainPage)
    {
        pLines[n++] = kScreen;
        pLines[n++] = kGraphics;
        pLines[n++] = kControls;
        pLines[n++] = kQuit;
    }
    else
    {
        pLines[n++] = kBack;
    }
    return n;
}

const wchar_t* LineName(s32 line)
{
    switch (line)
    {
        case kResume:
            return PortSettings::Translate(L"RESUME");
        case kScreen:
            return PortSettings::Translate(L"SCREEN");
        case kGraphics:
            return PortSettings::Translate(L"GRAPHICS");
        case kControls:
            return PortSettings::Translate(L"CONTROLS");
        case kQuit:
            return PortSettings::Translate(L"QUIT GAME");
        case kBack:
            return PortSettings::Translate(L"BACK");
        default:
            return PortSettings::Translate(PortSettings::Name(static_cast<PortSettings::Id>(line)));
    }
}

bool sOpen;
Page sPage;
s32 sSelected;
s32 sMainSelected; // the line to come back to from a subpage
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

void Change(s32 line, s32 delta)
{
    if (line < PortSettings::kCount)
    {
        PortSettings::Change(static_cast<PortSettings::Id>(line), delta);
    }
}

void OpenPage(Page page)
{
    if (sPage == kMainPage)
    {
        sMainSelected = sSelected;
    }
    sPage = page;
    sSelected = page == kMainPage ? sMainSelected : 0;
}

void Close()
{
    sOpen = false;
    sPage = kMainPage;
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
                OpenPage(kMainPage);
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
            OpenPage(kMainPage);
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
        sPage = kMainPage;
        sSelected = 0;
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
    if (sPage == kControlsPage)
    {
        return HandleControlsKey(action);
    }
    s32 lines[kMaxLines];
    const s32 count = Lines(sPage, lines);
    const s32 line = lines[sSelected];
    switch (action)
    {
        case DIK_UP:
            sSelected = (sSelected + count - 1) % count;
            break;
        case DIK_DOWN:
            sSelected = (sSelected + 1) % count;
            break;
        case DIK_LEFT:
        case DIK_RIGHT:
            Change(line, action == DIK_LEFT ? -1 : 1);
            break;
        case DIK_RETURN:
            switch (line)
            {
                case kResume:
                    Close();
                    break;
                case kScreen:
                    OpenPage(kScreenPage);
                    break;
                case kGraphics:
                    OpenPage(kGraphicsPage);
                    break;
                case kControls:
                    OpenPage(kControlsPage);
                    sControlSelected = 0;
                    break;
                case kQuit:
                    Close();
                    gGame_0x40->ExitGameNoBonus(1, GameExitType::PlayerQuit_2);
                    break;
                case kBack:
                    OpenPage(kMainPage);
                    break;
                default:
                    Change(line, 1);
                    break;
            }
            break;
        case DIK_ESCAPE:
            if (sPage == kMainPage)
            {
                Close();
            }
            else
            {
                OpenPage(kMainPage);
            }
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

    if (sPage != kControlsPage)
    {
        s32 lines[kMaxLines];
        const s32 count = Lines(sPage, lines);
        Panel(130, 160, 510, 160 + count * 18 + 10);
        wchar_t value[32];
        for (s32 i = 0; i < count; i++)
        {
            const f32 y = 167.0f + i * 18;
            Text(LineName(lines[i]), 150, y, i == sSelected);
            value[0] = 0;
            if (lines[i] < PortSettings::kCount)
            {
                PortSettings::Value(static_cast<PortSettings::Id>(lines[i]), value);
            }
            Text(value, 370, y, i == sSelected);
        }
        return;
    }

    const s32 count = PortOptionsMenu::NumControls();
    Panel(60, 160, 580, 160 + (count + 2) * 18 + 10);
    Text(PortSettings::Translate(L"KEYBOARD"), 300, 165, false);
#if defined(GTA2_PORT_SDL)
    Text(PortSettings::Translate(L"GAMEPAD"), 460, 165, false);
#endif
    wchar_t key[24];
    wchar_t pad[24];
    for (s32 i = 0; i <= count; i++)
    {
        const f32 y = 183.0f + i * 18;
        const bool selected = i == sControlSelected;
        if (i == count)
        {
            Text(PortSettings::Translate(L"BACK"), 80, y, selected);
            break;
        }
        PortOptionsMenu::BindingNames(i, key, pad);
        if (selected && sCapturing)
        {
            wcscpy(sColumn ? pad : key, PortSettings::Translate(L"PRESS"));
        }
        Text(PortOptionsMenu::ControlName(i), 80, y, selected);
        Text(key, 300, y, selected && sColumn == 0);
#if defined(GTA2_PORT_SDL)
        Text(pad, 460, y, selected && sColumn == 1);
#endif
    }
}
} // namespace PortPauseMenu
