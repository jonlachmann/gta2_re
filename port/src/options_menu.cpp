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

void port_UpdateGameResolution();
#if defined(GTA2_PORT_SDL)
bool port_PadMode();
bool port_PadInput(BYTE* pButtons, LONG* pX, LONG* pY);
#endif

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
    k3dSound,
    kTextSpeed,
    kLanguage,
    kDisplay,
    kViewSize,
    kBrightness,
    kLighting,
    kExplodingScores,
    kFrameLimit,
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

// view_height: lines of the game resolution, its width following the screen's shape; 0 is the
// screen's own resolution.
const s32 kViewHeights[] = {480, 540, 600, 720, 768, 900, 1080, 0};
const s32 kNumViewHeights = sizeof(kViewHeights) / sizeof(kViewHeights[0]);

struct Language
{
    char mCode;
    const wchar_t* mName;
};
const Language kLanguages[] = {{'e', L"ENGLISH"}, {'f', L"FRANCAIS"}, {'g', L"DEUTSCH"}, {'i', L"ITALIANO"}, {'s', L"ESPANOL"}};
const s32 kNumLanguages = sizeof(kLanguages) / sizeof(kLanguages[0]);

const wchar_t* const kTextSpeeds[] = {L"VERY FAST", L"FAST", L"NORMAL", L"SLOW", L"VERY SLOW"};

void SetOptionValue(const char* name, DWORD type, const void* pData, DWORD size)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Option", 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                        &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExA(hKey, name, 0, type, static_cast<const BYTE*>(pData), size);
        RegCloseKey(hKey);
    }
}

s32 LanguageIndex()
{
    char code[16] = "e";
    gRegistry.Get_Option("language", reinterpret_cast<BYTE*>(code), sizeof(code));
    for (s32 i = 0; i < kNumLanguages; i++)
    {
        if (kLanguages[i].mCode == code[0])
        {
            return i;
        }
    }
    return 0;
}

s32 ViewIndex()
{
    const s32 view = gRegistry.Get_Screen_Setting("view_height", 720);
    for (s32 i = 0; i < kNumViewHeights; i++)
    {
        if (kViewHeights[i] == view)
        {
            return i;
        }
    }
    return 3;
}

s32 Wrap(s32 i, s32 n)
{
    return (i % n + n) % n;
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
        case k3dSound:
            gRegistry.Clear_Or_Delete_Sound_Setting("do_3d_sound", !gRegistry.Sound_Setting_Exists("do_3d_sound"));
            break;
        case kTextSpeed:
        {
            s32 speed = gRegistry.Get_Option_Int("text_speed", 3) + delta;
            speed = speed < 1 ? 1 : (speed > 5 ? 5 : speed);
            const DWORD value = speed;
            SetOptionValue("text_speed", REG_DWORD, &value, sizeof(value));
            break;
        }
        case kLanguage:
        {
            const char code[2] = {kLanguages[Wrap(LanguageIndex() + delta, kNumLanguages)].mCode, 0};
            SetOptionValue("language", REG_SZ, code, sizeof(code));
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
        case kViewSize:
            gRegistry.Set_Screen_Setting("view_height", kViewHeights[Wrap(ViewIndex() + delta, kNumViewHeights)]);
            port_UpdateGameResolution();
            break;
        case kBrightness:
        {
            s32 gamma = gRegistry.Get_Screen_Setting("gamma", 10) + delta;
            gamma = gamma < 1 ? 1 : (gamma > 20 ? 20 : gamma);
            gRegistry.Set_Screen_Setting("gamma", gamma);
            SetGamma(gamma);
            break;
        }
        case kLighting:
        case kExplodingScores:
        case kFrameLimit:
        {
            const char* name = item.mAction == kLighting ? "lighting" : (item.mAction == kExplodingScores ? "exploding_on" : "max_frame_rate");
            gRegistry.Set_Screen_Setting(name, gRegistry.Get_Screen_Setting(name, 1) ? 0 : 1);
            break;
        }
        default:
            break;
    }
}

// Pad buttons 0..31 and the two axes (-1000..1000), as the game's pad bindings read them.
bool ReadPad(u8* pButtons, LONG* pX, LONG* pY)
{
#if defined(GTA2_PORT_SDL)
    return port_PadInput(pButtons, pX, pY);
#else
    struct
    {
        LONG mAxes[8];
        DWORD mPov[4];
        u8 mButtons[32];
    } pad;
    if (!gGamePadDevice || FAILED(gGamePadDevice->GetDeviceState(sizeof(pad), &pad)))
    {
        return false;
    }
    memcpy(pButtons, pad.mButtons, 32);
    *pX = pad.mAxes[0];
    *pY = pad.mAxes[1];
    return true;
#endif
}

// The first key or pad input pressed since everything was released; -1 if none. The pad comes
// first: it also drives the menus through the arrows, Enter and Escape.
s32 CapturedBinding(Frontend* pFrontend)
{
    bool anyDown = false;
    s32 keyResult = -1;
    for (s32 i = 0; i < 256; i++)
    {
        const bool down = (pFrontend->mKeys[i] & 0x80) != 0;
        anyDown |= down;
        if (down && !(sPrevKeys[i] & 0x80) && keyResult < 0)
        {
            keyResult = i;
        }
    }
    memcpy(sPrevKeys, pFrontend->mKeys, sizeof(sPrevKeys));

    s32 padResult = -1;
    u8 buttons[32];
    LONG x;
    LONG y;
    if (ReadPad(buttons, &x, &y))
    {
        for (s32 b = 0; b < 32; b++)
        {
            const bool down = (buttons[b] & 0x80) != 0;
            anyDown |= down;
            if (down && !(sPrevPadButtons[b] & 0x80) && padResult < 0)
            {
                padResult = kPadFlag | b;
            }
            sPrevPadButtons[b] = buttons[b];
        }
        anyDown |= x <= -750 || x >= 750 || y <= -750 || y >= 750;
        if (padResult < 0)
        {
            if (x <= -750)
            {
                padResult = kPadFlag | kPadLeft;
            }
            else if (x >= 750)
            {
                padResult = kPadFlag | kPadRight;
            }
            else if (y <= -750)
            {
                padResult = kPadFlag | kPadUp;
            }
            else if (y >= 750)
            {
                padResult = kPadFlag | kPadDown;
            }
        }
    }
    if (!sCaptureArmed)
    {
        sCaptureArmed = !anyDown;
        return -1;
    }
    return padResult >= 0 ? padResult : keyResult;
}

bool PadMode()
{
#if defined(GTA2_PORT_SDL)
    return port_PadMode();
#else
    return false;
#endif
}

void SetText(MenuElement& e, s16 x, s16 y, const wchar_t* pText, u16 font)
{
    e.mElementType = STRING_TEXT_1;
    e.mIsItDisplayed = 1;
    e.mXpos = x;
    e.mYpos = y;
    wcsncpy(e.mElementNameStr, pText, 50);
    e.mFontType = font;
    e.mFontPalette = 0xFFFF;
}

// Key hints at the bottom of our pages (elements 1 to 6): the keys, or the pad's buttons in the
// frontend's red discs once the pad was used last.
void UpdateHints(Frontend* pFrontend, MenuPage* pPage)
{
    struct Hint
    {
        const wchar_t* mKey;
        const wchar_t* mButton;
        const wchar_t* mLabel;
    };
    const Hint kNavigate[] = {{L"ENTER", L"A", L"SELECT"}, {L"ESC", L"B", L"BACK"}};
    const Hint kCapture[] = {{L"ESC", NULL, L"CANCEL"}};
    const bool capturing = sCapturing >= 0;
    const Hint* pHints = capturing ? kCapture : kNavigate;
    const s32 count = capturing ? 1 : 2;
    const bool pad = PadMode();
    const u16 font = pFrontend->mNormalFont;
    const u16 buttonFont = static_cast<u16>(gFrontendHeadingFont);

    pPage->mNumberOfElements = 7;
    for (s32 i = 1; i < 7; i++)
    {
        pPage->mElementsArray[i].mIsItDisplayed = 0;
    }
    s32 x = 120;
    const s32 y = 440;
    for (s32 i = 0; i < count; i++)
    {
        MenuElement* pElems = &pPage->mElementsArray[1 + i * 3];
        if (pad && pHints[i].mButton)
        {
            pElems[0].mElementType = GEOMETRIC_SHAPE_3;
            pElems[0].mIsItDisplayed = 1;
            pElems[0].mXpos = static_cast<s16>(x + 10);
            pElems[0].mYpos = static_cast<s16>(y + 12);
            pElems[0].mGeometricShapeType = 0;
            wchar_t button[4];
            wcscpy(button, pHints[i].mButton);
            SetText(pElems[1], static_cast<s16>(pFrontend->GetCenteredXPos(button, buttonFont, x + 10)), y, button, buttonFont);
            x += 34;
        }
        else
        {
            wchar_t key[16];
            wcscpy(key, pHints[i].mKey);
            SetText(pElems[1], static_cast<s16>(x), y, key, font);
            x += Frontend::GetMaxTextWidth(key, font) + 10;
        }
        wchar_t label[16];
        wcscpy(label, pHints[i].mLabel);
        SetText(pElems[2], static_cast<s16>(x), y, label, font);
        x += Frontend::GetMaxTextWidth(label, font) + 40;
    }
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
    AddOption(pFrontend, kOptionsPage, L"3D SOUND", k3dSound, 0);
    AddOption(pFrontend, kOptionsPage, L"TEXT SPEED", kTextSpeed, 0, true);
    AddOption(pFrontend, kOptionsPage, L"LANGUAGE", kLanguage, 0);
    AddOption(pFrontend, kOptionsPage, L"BACK", kGoTo, MENUPAGE_START_MENU, true);

    SetTitle(&pFrontend->mMenuPagesArray[kScreenPage], L"SCREEN", title);
    AddOption(pFrontend, kScreenPage, L"DISPLAY", kDisplay, 0);
    AddOption(pFrontend, kScreenPage, L"GAME RESOLUTION", kViewSize, 0);
    AddOption(pFrontend, kScreenPage, L"BRIGHTNESS", kBrightness, 0);
    AddOption(pFrontend, kScreenPage, L"LIGHTING", kLighting, 0, true);
    AddOption(pFrontend, kScreenPage, L"EXPLODING SCORES", kExplodingScores, 0);
    AddOption(pFrontend, kScreenPage, L"FRAME LIMIT", kFrameLimit, 0);
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
    if (sKeyNames.empty())
    {
        LoadKeyNames();
    }
    pFrontend->mMenuScreen = RedBar_16;
    MenuPage* pPage = &pFrontend->mMenuPagesArray[pageIdx];
    UpdateHints(pFrontend, pPage);
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
        case k3dSound:
            wcscpy(value, gRegistry.Sound_Setting_Exists("do_3d_sound") ? L"ON" : L"OFF");
            break;
        case kTextSpeed:
        {
            const s32 speed = gRegistry.Get_Option_Int("text_speed", 3);
            wcscpy(value, kTextSpeeds[speed < 1 ? 0 : (speed > 5 ? 4 : speed - 1)]);
            break;
        }
        case kLanguage:
            wcscpy(value, kLanguages[LanguageIndex()].mName);
            break;
        case kDisplay:
            wcscpy(value, gRegistry.Get_Screen_Setting("start_mode", 1) ? L"FULL SCREEN" : L"WINDOW");
            break;
        case kViewSize:
        {
            const bool full = gRegistry.Get_Screen_Setting("start_mode", 1) != 0;
            swprintf(value, L"%d X %d", gRegistry.Get_Screen_Setting(full ? "full_width" : "window_width", 640),
                     gRegistry.Get_Screen_Setting(full ? "full_height" : "window_height", 480));
            break;
        }
        case kLighting:
            wcscpy(value, gRegistry.Get_Screen_Setting("lighting", 1) ? L"DUSK" : L"NOON");
            break;
        case kExplodingScores:
            wcscpy(value, gRegistry.Get_Screen_Setting("exploding_on", 1) ? L"ON" : L"OFF");
            break;
        case kFrameLimit:
            wcscpy(value, gRegistry.Get_Screen_Setting("max_frame_rate", 1) ? L"ON" : L"OFF");
            break;
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
