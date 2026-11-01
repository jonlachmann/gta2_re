#include "options_menu.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "KeyboardLayout.hpp"
#include "SoundObject.hpp"
#include "dma_video.hpp"
#include "input.hpp"
#include "registry.hpp"
#include "root_sound.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

EXTERN_GLOBAL(SoundObject, snd1);

void __stdcall SetStartMode(s32 startMode);
void __stdcall SetVideoModeFromSettings();
void OnVideoModeChanged();
char_type IsWindowedModeAvailable();

namespace PortOptionsMenu
{
namespace
{
enum Page
{
    kOptionsPage = 12,
    kScreenPage = 13,
    kControlsPage = 15,
    kControls2Page = 16,
};

enum Action
{
    kNone,
    kGoTo,
    kSfxVolume,
    kMusicVolume,
    kDisplay,
    kFullSize,
    kWindowSize,
    kBrightness,
    kBinding,
    kDefaultKeys,
};

struct Item
{
    Action mAction;
    s32 mArg; // target page, or control index
};

// [page - 12][option]
Item sItems[5][10];

const s32 kNumControls = 12;
const s32 kDefaultControls[kNumControls] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_LCONTROL, DIK_RETURN,
                                            DIK_SPACE, DIK_Z, DIK_X, DIK_TAB, DIK_LMENU, DIK_RSHIFT};
const wchar_t* const kControlNames[kNumControls] = {L"FORWARD", L"BACKWARD", L"LEFT", L"RIGHT", L"ATTACK",
                                                    L"ENTER OR EXIT", L"JUMP OR BRAKE", L"PREVIOUS WEAPON",
                                                    L"NEXT WEAPON", L"SPECIAL 1", L"SPECIAL 2", L"SPECIAL 3"};
// Pad bindings: the original's codes for the two axes, else a button number.
const s32 kPadFlag = 0x8000;
const s32 kPadLeft = 224;
const s32 kPadRight = 225;
const s32 kPadUp = 226;
const s32 kPadDown = 227;

struct Mode
{
    s32 mWidth;
    s32 mHeight;
};
std::vector<Mode> sModes;

s32 sCapturing = -1; // control index waiting for a key or pad button
bool sCaptureArmed; // every key released since capture started
char_type sPrevKeys[256];
u8 sPrevPadButtons[32];
wchar_t sText[100];

struct KeyName
{
    wchar_t mName[24];
};
std::vector<KeyName> sKeyNames;

Item& ItemAt(u16 page, u16 option)
{
    return sItems[page - kOptionsPage][option];
}

bool IsOurs(u16 page)
{
    return page == kOptionsPage || page == kScreenPage || page == kControlsPage || page == kControls2Page;
}

// The key names GTA2 Manager showed: data\keyboard\<lang>_kb.cfg, one name per DirectInput code.
void LoadKeyNames()
{
    static const char* const kFiles[] = {"eng", "fre", "ger", "ita", "spa", "por", "eng"};
    const s32 layout = KeyboardLayout::GetLayout();
    char path[64];
    sprintf(path, "data\\keyboard\\%s_kb.cfg", kFiles[layout >= 0 && layout < 7 ? layout : 0]);
    sKeyNames.clear();
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        return;
    }
    KeyName line = {};
    size_t len = 0;
    for (int c; (c = fgetc(f)) != EOF;)
    {
        if (c == '\n')
        {
            sKeyNames.push_back(line);
            line = KeyName();
            len = 0;
        }
        else if (c != '\r' && c != '`' && len + 1 < 24)
        {
            line.mName[len++] = static_cast<wchar_t>(static_cast<u8>(c));
        }
    }
    fclose(f);
}

void BindingName(s32 binding, wchar_t* pOut)
{
    const s32 code = binding & 0xFF;
    if (binding & kPadFlag)
    {
        switch (code)
        {
            case kPadLeft:
                wcscpy(pOut, L"PAD LEFT");
                break;
            case kPadRight:
                wcscpy(pOut, L"PAD RIGHT");
                break;
            case kPadUp:
                wcscpy(pOut, L"PAD UP");
                break;
            case kPadDown:
                wcscpy(pOut, L"PAD DOWN");
                break;
            default:
                swprintf(pOut, L"PAD BUTTON %d", code + 1);
                break;
        }
    }
    else if (code < static_cast<s32>(sKeyNames.size()) && sKeyNames[code].mName[0])
    {
        wcscpy(pOut, sKeyNames[code].mName);
    }
    else
    {
        swprintf(pOut, L"KEY %d", code);
    }
}

s32 GetControl(s32 i)
{
    return gRegistry.Get_Control_Setting(i, kDefaultControls[i]);
}

void SetControl(s32 i, s32 binding)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Control", 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                        &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }
    char name[4];
    sprintf(name, "%d", i);
    const DWORD data = static_cast<DWORD>(binding);
    RegSetValueExA(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(hKey);
}

// A binding already used by another control moves to the one being changed.
void Bind(s32 control, s32 binding)
{
    const s32 old = GetControl(control);
    for (s32 i = 0; i < kNumControls; i++)
    {
        if (i != control && GetControl(i) == binding)
        {
            SetControl(i, old);
        }
    }
    SetControl(control, binding);
}

void LoadModes()
{
    sModes.clear();
    if (!gVidSys)
    {
        return;
    }
    for (s32 id = pVid_FindFirstMode(gVidSys, 16); id; id = pVid_FindNextMode(gVidSys))
    {
        SDisplayMode* pMode = pVid_FindMode(gVidSys, id);
        if (pMode)
        {
            sModes.push_back(Mode{static_cast<s32>(pMode->field_8_width), static_cast<s32>(pMode->field_C_height)});
        }
    }
    if (sModes.empty())
    {
        sModes.push_back(Mode{640, 480});
    }
}

s32 ModeIndex(s32 width, s32 height)
{
    for (size_t i = 0; i < sModes.size(); i++)
    {
        if (sModes[i].mWidth == width && sModes[i].mHeight == height)
        {
            return static_cast<s32>(i);
        }
    }
    return 0;
}

// Volumes are shown 0 to 10, stored 0 to 127.
s32 VolumeStep(s32 volume)
{
    return (volume * 10 + 63) / 127;
}

void SetTitle(MenuPage* pPage, const wchar_t* pTitle, s16 font)
{
    pPage->mNumberOfElements = 1;
    pPage->mElementsArray[0].mElementType = STRING_TEXT_1;
    pPage->mElementsArray[0].mXpos = 35;
    pPage->mElementsArray[0].mYpos = 11;
    pPage->mElementsArray[0].mIsItDisplayed = 1;
    wcsncpy(pPage->mElementsArray[0].mElementNameStr, pTitle, 50);
    pPage->mElementsArray[0].mFontType = font;
    pPage->mElementsArray[0].mFontPalette = 5;
}

void AddOption(Frontend* pFrontend, u16 page, const wchar_t* pName, Action action, s32 arg, bool spaceBefore = false)
{
    MenuPage* pPage = &pFrontend->mMenuPagesArray[page];
    const u16 i = pPage->mNumberOfOptions++;
    s32 y = 160;
    if (i)
    {
        y = pPage->mOptionsArray[i - 1].mYPos + (spaceBefore ? 34 : 22);
    }
    MenuItem& item = pPage->mOptionsArray[i];
    item.mOptionType = (action == kGoTo || action == kDefaultKeys) ? ENTERABLE_1 : HORIZONTAL_SELECTION_2;
    item.mIsUnlocked = 1;
    item.mXPos = 120;
    item.mYPos = static_cast<s16>(y);
    wcsncpy(item.mOptionNameStr, pName, 50);
    item.mMenuPageTarget = static_cast<s16>(action == kGoTo ? arg : 268);
    pPage->mOptionStates[i].mIsOptionUnlocked = 1;
    ItemAt(page, i) = Item{action, arg};
}

void ChangeValue(Frontend* pFrontend, const Item& item, s32 delta)
{
    switch (item.mAction)
    {
        case kSfxVolume:
        case kMusicVolume:
        {
            const char* name = item.mAction == kSfxVolume ? "SFXVol" : "CDVol";
            s32 step = VolumeStep(gRegistry.Get_Sound_Setting(name, 127)) + delta;
            step = step < 0 ? 0 : (step > 10 ? 10 : step);
            const s32 volume = step * 127 / 10;
            gRegistry.Set_Sound_Setting(name, volume);
            if (item.mAction == kSfxVolume)
            {
                gRoot_sound.SetSfxVol(static_cast<u8>(volume));
            }
            else
            {
                gRoot_sound.SetCDVol(static_cast<u8>(volume));
            }
            break;
        }
        case kDisplay:
            // As Alt+Enter: start_mode 1 is full screen, 0 a window.
            if (IsWindowedModeAvailable())
            {
                SetStartMode(gRegistry.Get_Screen_Setting("start_mode", 1) ? 0 : 1);
                SetVideoModeFromSettings();
                OnVideoModeChanged();
            }
            break;
        case kFullSize:
        case kWindowSize:
        {
            const bool full = item.mAction == kFullSize;
            const char* wName = full ? "full_width" : "window_width";
            const char* hName = full ? "full_height" : "window_height";
            const s32 n = static_cast<s32>(sModes.size());
            const s32 i = (ModeIndex(gRegistry.Get_Screen_Setting(wName, 640), gRegistry.Get_Screen_Setting(hName, 480)) + delta + n) % n;
            gRegistry.Set_Screen_Setting(wName, sModes[i].mWidth);
            gRegistry.Set_Screen_Setting(hName, sModes[i].mHeight);
            break;
        }
        case kBrightness:
        {
            s32 gamma = gRegistry.Get_Screen_Setting("gamma", 10) + delta;
            gamma = gamma < 1 ? 1 : (gamma > 20 ? 20 : gamma);
            gRegistry.Set_Screen_Setting("gamma", gamma);
            SetGamma(gamma);
            break;
        }
        default:
            break;
    }
}

// The first key or pad input pressed since every key was released; -1 if none.
s32 CapturedBinding(Frontend* pFrontend)
{
    bool anyDown = false;
    s32 result = -1;
    for (s32 i = 0; i < 256; i++)
    {
        const bool down = (pFrontend->mKeys[i] & 0x80) != 0;
        anyDown |= down;
        if (down && !(sPrevKeys[i] & 0x80) && sCaptureArmed && result < 0)
        {
            result = i;
        }
    }
    memcpy(sPrevKeys, pFrontend->mKeys, sizeof(sPrevKeys));

    struct
    {
        LONG mAxes[8];
        DWORD mPov[4];
        u8 mButtons[32];
    } pad;
    if (gGamePadDevice && SUCCEEDED(gGamePadDevice->GetDeviceState(sizeof(pad), &pad)))
    {
        for (s32 b = 0; b < 32; b++)
        {
            const bool down = (pad.mButtons[b] & 0x80) != 0;
            anyDown |= down;
            if (down && !(sPrevPadButtons[b] & 0x80) && sCaptureArmed && result < 0)
            {
                result = kPadFlag | b;
            }
            sPrevPadButtons[b] = pad.mButtons[b];
        }
        if (sCaptureArmed && result < 0)
        {
            if (pad.mAxes[0] <= -750)
            {
                result = kPadFlag | kPadLeft;
            }
            else if (pad.mAxes[0] >= 750)
            {
                result = kPadFlag | kPadRight;
            }
            else if (pad.mAxes[1] <= -750)
            {
                result = kPadFlag | kPadUp;
            }
            else if (pad.mAxes[1] >= 750)
            {
                result = kPadFlag | kPadDown;
            }
        }
    }
    if (!anyDown)
    {
        sCaptureArmed = true;
    }
    return result;
}

u16 ParentPage(u16 page)
{
    switch (page)
    {
        case kOptionsPage:
            return MENUPAGE_START_MENU;
        case kControls2Page:
            return kControlsPage;
        default:
            return kOptionsPage;
    }
}
} // namespace

void Setup(Frontend* pFrontend)
{
    for (u16 page : {kOptionsPage, kScreenPage, kControlsPage, kControls2Page})
    {
        MenuPage* pPage = &pFrontend->mMenuPagesArray[page];
        pPage->mNumberOfOptions = 0;
        pPage->mCurrentOptionIdx = 0;
        pPage->mDefaultOptionIdx = 0;
    }

    const s16 title = pFrontend->mTitleFont;
    SetTitle(&pFrontend->mMenuPagesArray[kOptionsPage], L"OPTIONS", title);
    AddOption(pFrontend, kOptionsPage, L"SCREEN", kGoTo, kScreenPage);
    AddOption(pFrontend, kOptionsPage, L"CONTROLS", kGoTo, kControlsPage);
    AddOption(pFrontend, kOptionsPage, L"SOUND EFFECTS", kSfxVolume, 0, true);
    AddOption(pFrontend, kOptionsPage, L"MUSIC", kMusicVolume, 0);
    AddOption(pFrontend, kOptionsPage, L"BACK", kGoTo, MENUPAGE_START_MENU, true);

    SetTitle(&pFrontend->mMenuPagesArray[kScreenPage], L"SCREEN", title);
    AddOption(pFrontend, kScreenPage, L"DISPLAY", kDisplay, 0);
    AddOption(pFrontend, kScreenPage, L"RESOLUTION", kFullSize, 0);
    AddOption(pFrontend, kScreenPage, L"WINDOW SIZE", kWindowSize, 0);
    AddOption(pFrontend, kScreenPage, L"BRIGHTNESS", kBrightness, 0);
    AddOption(pFrontend, kScreenPage, L"BACK", kGoTo, kOptionsPage, true);

    SetTitle(&pFrontend->mMenuPagesArray[kControlsPage], L"CONTROLS", title);
    for (s32 i = 0; i < 8; i++)
    {
        AddOption(pFrontend, kControlsPage, kControlNames[i], kBinding, i);
    }
    AddOption(pFrontend, kControlsPage, L"MORE", kGoTo, kControls2Page, true);
    AddOption(pFrontend, kControlsPage, L"BACK", kGoTo, kOptionsPage);

    SetTitle(&pFrontend->mMenuPagesArray[kControls2Page], L"CONTROLS", title);
    for (s32 i = 8; i < kNumControls; i++)
    {
        AddOption(pFrontend, kControls2Page, kControlNames[i], kBinding, i);
    }
    AddOption(pFrontend, kControls2Page, L"DEFAULT KEYS", kDefaultKeys, 0, true);
    AddOption(pFrontend, kControls2Page, L"BACK", kGoTo, kControlsPage);

    // The start menu's "Options" opened GTA2 Manager.
    pFrontend->mMenuPagesArray[MENUPAGE_START_MENU].mOptionsArray[1].mMenuPageTarget = kOptionsPage;
}

bool Update(Frontend* pFrontend)
{
    const u16 pageIdx = pFrontend->mCurrentPageIdx;
    if (!IsOurs(pageIdx))
    {
        return false;
    }
    if (sModes.empty())
    {
        LoadModes();
    }
    if (sKeyNames.empty())
    {
        LoadKeyNames();
    }
    pFrontend->mMenuScreen = RedBar_16;
    MenuPage* pPage = &pFrontend->mMenuPagesArray[pageIdx];
    const Item& item = ItemAt(pageIdx, pPage->mCurrentOptionIdx);

    if (sCapturing >= 0)
    {
        const s32 binding = CapturedBinding(pFrontend);
        if (binding == DIK_ESCAPE)
        {
            sCapturing = -1;
            snd1.mObjectType = 6;
        }
        else if (binding >= 0)
        {
            Bind(sCapturing, binding);
            sCapturing = -1;
            snd1.mObjectType = 5;
        }
        // Keys pressed during capture must not navigate.
        pFrontend->mReturnPressed = false;
        pFrontend->mEscapePressed = false;
        return true;
    }

    if (pFrontend->mReturnPressed)
    {
        if (item.mAction == kGoTo)
        {
            pFrontend->ChangeMenuPage(static_cast<u16>(item.mArg));
            snd1.mObjectType = 5;
        }
        else if (item.mAction == kBinding)
        {
            sCapturing = item.mArg;
            sCaptureArmed = false;
            memcpy(sPrevKeys, pFrontend->mKeys, sizeof(sPrevKeys));
            memset(sPrevPadButtons, 0, sizeof(sPrevPadButtons));
            snd1.mObjectType = 5;
        }
        else if (item.mAction == kDefaultKeys)
        {
            for (s32 i = 0; i < kNumControls; i++)
            {
                SetControl(i, kDefaultControls[i]);
            }
            snd1.mObjectType = 5;
        }
        else
        {
            ChangeValue(pFrontend, item, 1);
            snd1.mObjectType = 4;
        }
    }
    else if (pFrontend->mEscapePressed)
    {
        pFrontend->ChangeMenuPage(ParentPage(pageIdx));
        snd1.mObjectType = 6;
    }
    else if (pFrontend->mUpPressed && pPage->SelectPrevOption())
    {
        snd1.mObjectType = 1;
    }
    else if (pFrontend->mDownPressed && pPage->SelectNextOption())
    {
        snd1.mObjectType = 2;
    }
    else if ((pFrontend->mLeftPressed || pFrontend->mRightPressed) && item.mAction != kGoTo &&
             item.mAction != kBinding && item.mAction != kDefaultKeys)
    {
        ChangeValue(pFrontend, item, pFrontend->mLeftPressed ? -1 : 1);
        snd1.mObjectType = pFrontend->mLeftPressed ? 3 : 4;
    }
    return true;
}

bool GetOptionText(Frontend* pFrontend, u16 page, u16 option, wchar_t** ppText)
{
    if (!IsOurs(page))
    {
        return false;
    }
    const Item& item = ItemAt(page, option);
    const MenuItem& menuItem = pFrontend->mMenuPagesArray[page].mOptionsArray[option];
    wchar_t value[40] = {};
    switch (item.mAction)
    {
        case kSfxVolume:
        case kMusicVolume:
            swprintf(value, L"%d", VolumeStep(gRegistry.Get_Sound_Setting(item.mAction == kSfxVolume ? "SFXVol" : "CDVol", 127)));
            break;
        case kDisplay:
            wcscpy(value, gRegistry.Get_Screen_Setting("start_mode", 1) ? L"FULL SCREEN" : L"WINDOW");
            break;
        case kFullSize:
        case kWindowSize:
        {
            const bool full = item.mAction == kFullSize;
            swprintf(value, L"%d X %d", gRegistry.Get_Screen_Setting(full ? "full_width" : "window_width", 640),
                     gRegistry.Get_Screen_Setting(full ? "full_height" : "window_height", 480));
            break;
        }
        case kBrightness:
            swprintf(value, L"%d", gRegistry.Get_Screen_Setting("gamma", 10));
            break;
        case kBinding:
            if (sCapturing == item.mArg)
            {
                wcscpy(value, L"PRESS A KEY");
            }
            else
            {
                BindingName(GetControl(item.mArg), value);
            }
            break;
        default:
            break;
    }

    // The value in a second column at x = 380.
    wcscpy(sText, menuItem.mOptionNameStr);
    const u16 font = pFrontend->mNormalFont;
    size_t len = wcslen(sText);
    do
    {
        sText[len++] = L' ';
        sText[len] = 0;
    } while (menuItem.mXPos + Frontend::GetMaxTextWidth(sText, font) < 380 && len < 58);
    wcsncpy(sText + len, value, 99 - len);
    sText[99] = 0;
    *ppText = sText;
    return true;
}
} // namespace PortOptionsMenu
