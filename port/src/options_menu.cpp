#include "options_menu.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "SoundObject.hpp"
#include "menu.hpp"
#include "reg2_version.h"
#include "settings.hpp"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

EXTERN_GLOBAL(SoundObject, snd1);

#if defined(GTA2_PORT_SDL)
bool port_TakeMouse(float* pX, float* pY, int* pLeft, int* pRight, int* pWheel);
#endif

namespace PortOptionsMenu
{
namespace
{
const u16 kMenuPage = 12;
bool sShown;

s32 Wrap(s32 i, s32 n)
{
    return (i % n + n) % n;
}

PortMenu::Result HandleInput(Frontend* pFrontend)
{
    if (PortMenu::IsCapturing())
    {
        const PortMenu::Result result = PortMenu::UpdateCapture();
        return result != PortMenu::kNothing ? result : PortMenu::HandleMouse(1.0f, 0.0f, 0.0f);
    }
    const struct
    {
        bool Frontend::*mPressed;
        PortMenu::Input mInput;
    } kInputs[] = {{&Frontend::mReturnPressed, PortMenu::kSelect}, {&Frontend::mEscapePressed, PortMenu::kCancel},
                   {&Frontend::mUpPressed, PortMenu::kUp},          {&Frontend::mDownPressed, PortMenu::kDown},
                   {&Frontend::mLeftPressed, PortMenu::kLeft},      {&Frontend::mRightPressed, PortMenu::kRight}};
    for (const auto& input : kInputs)
    {
        if (pFrontend->*input.mPressed)
        {
            return PortMenu::HandleInput(input.mInput);
        }
    }
    return PortMenu::HandleMouse(1.0f, 0.0f, 0.0f);
}

#if defined(GTA2_PORT_SDL)
bool Inside(f32 x, f32 y, f32 left, f32 top, f32 right, f32 bottom)
{
    return x >= left && x < right && y >= top && y < bottom;
}

// The original pages with the mouse, as their keys: pointing at an option selects it, a click enters
// it, the right button goes back, the wheel and the arrows beside an option change it.
void OriginalPageMouse(Frontend* pFrontend)
{
    float x;
    float y;
    int left;
    int right;
    int wheel;
    const bool moved = port_TakeMouse(&x, &y, &left, &right, &wheel);
    if (right)
    {
        pFrontend->mEscapePressed = true;
        return;
    }
    MenuPage* pPage = &pFrontend->mMenuPagesArray[pFrontend->mCurrentPageIdx];
    for (s32 i = 0; i < pPage->mNumberOfElements && left; i++)
    {
        const MenuElement& element = pPage->mElementsArray[i];
        const u16 shape = element.mGeometricShapeType;
        if (element.mElementType == GEOMETRIC_SHAPE_3 && element.mIsItDisplayed && (shape == 1 || shape == 2) &&
            Inside(x, y, element.mXpos - 12.0f, element.mYpos - 12.0f, element.mXpos + 12.0f, element.mYpos + 12.0f))
        {
            (shape == 1 ? pFrontend->mLeftPressed : pFrontend->mRightPressed) = true;
            return;
        }
    }
    s32 option = -1;
    for (s32 i = 0; i < pPage->mNumberOfOptions && option < 0; i++)
    {
        MenuItem& item = pPage->mOptionsArray[i];
        if (!item.mIsUnlocked)
        {
            continue;
        }
        wchar_t* pText = item.mOptionNameStr;
        if (item.mOptionType == HORIZONTAL_SELECTION_2)
        {
            pFrontend->GetOptionText(pFrontend->mCurrentPageIdx, static_cast<u16>(i), &pText);
        }
        const u16 font = i == pPage->mCurrentOptionIdx ? pFrontend->mSelectedFont
                                                        : (item.mFontType != 0xFFFF ? item.mFontType : pFrontend->mNormalFont);
        const f32 width = static_cast<f32>(Frontend::GetMaxTextWidth(pText, font));
        if (Inside(x, y, item.mXPos - 4.0f, item.mYPos - 2.0f, item.mXPos + width + 4.0f, item.mYPos + 18.0f))
        {
            option = i;
        }
    }
    if (option < 0 || !(moved || left || wheel))
    {
        return;
    }
    if (option != pPage->mCurrentOptionIdx)
    {
        snd1.mObjectType = option < pPage->mCurrentOptionIdx ? PortMenu::kMovedUp : PortMenu::kMovedDown;
        pPage->mCurrentOptionIdx = static_cast<u16>(option);
    }
    if (wheel)
    {
        (wheel < 0 ? pFrontend->mLeftPressed : pFrontend->mRightPressed) = true;
    }
    else if (left)
    {
        pFrontend->mReturnPressed = true;
    }
}
#endif
} // namespace

void Setup(Frontend* pFrontend)
{
    MenuPage* pPage = &pFrontend->mMenuPagesArray[kMenuPage];
    // The menu draws its own lines; ChangeMenuPage only needs option 0 to be selectable.
    pPage->mNumberOfOptions = 0;
    pPage->mOptionsArray[0].mIsUnlocked = 1;
    pPage->mCurrentOptionIdx = 0;
    pPage->mDefaultOptionIdx = 0;

    MenuElement& title = pPage->mElementsArray[0];
    pPage->mNumberOfElements = 1;
    title.mElementType = STRING_TEXT_1;
    title.mXpos = 35;
    title.mYpos = 11;
    title.mIsItDisplayed = 1;
    title.mFontType = pFrontend->mTitleFont;
    title.mFontPalette = 5;

    PortSettings::ApplyRenderOptions();

    // The start menu's "Options" opened GTA2 Manager.
    pFrontend->mMenuPagesArray[MENUPAGE_START_MENU].mOptionsArray[1].mMenuPageTarget = kMenuPage;
}

bool Update(Frontend* pFrontend)
{
    if (pFrontend->mCurrentPageIdx != kMenuPage)
    {
        if (sShown)
        {
            PortMenu::Close();
        }
        sShown = false;
#if defined(GTA2_PORT_SDL)
        OriginalPageMouse(pFrontend);
#endif
        return false;
    }
    if (!sShown)
    {
        PortMenu::Open();
        sShown = true;
    }
    pFrontend->mMenuScreen = RedBar_16;
    const PortMenu::Result result = HandleInput(pFrontend);
    if (result == PortMenu::kClosed)
    {
        pFrontend->ChangeMenuPage(MENUPAGE_START_MENU);
        snd1.mObjectType = PortMenu::kWentBack;
    }
    else if (result != PortMenu::kNothing)
    {
        snd1.mObjectType = result;
    }
    wcsncpy(pFrontend->mMenuPagesArray[kMenuPage].mElementsArray[0].mElementNameStr, PortMenu::Title(), 50);
    return true;
}

void Draw(Frontend* pFrontend)
{
    if (pFrontend->mCurrentPageIdx == kMenuPage)
    {
        PortMenu::Draw(1.0f, 0.0f, 0.0f, false);
    }
}

void VersionText(wchar_t* pOut)
{
    char text[48];
    snprintf(text, sizeof(text), "REG2 %s", GTA2_VERSION);
    // The font has no hyphen and no lower case.
    s32 i = 0;
    for (; text[i]; i++)
    {
        pOut[i] = static_cast<wchar_t>(text[i] == '-' ? ' ' : toupper(static_cast<unsigned char>(text[i])));
    }
    pOut[i] = 0;
}

bool UpdateNameEntry(Frontend* pFrontend)
{
    if (!PortMenu::PadMode())
    {
        return false;
    }
    static const wchar_t kLetters[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    const s32 numLetters = static_cast<s32>(wcslen(kLetters));
    wchar_t* pName = pFrontend->mCurrPlyrName;
    u8& len = pFrontend->mCurrPlyrNameLength;
    if (len == 0)
    {
        pName[len++] = L'A';
    }
    if (pFrontend->mReturnPressed || pFrontend->mEscapePressed)
    {
        pFrontend->mState = FrontendState::Menu_Navigation_1;
        pFrontend->StripPlayerNameToCurrLength();
        if (pFrontend->mReturnPressed)
        {
            pFrontend->SaveAndUpdatePlayerName();
        }
        pFrontend->mMenuPagesArray[MENUPAGE_PLAY].mCurrentOptionIdx = 0;
        snd1.mObjectType = pFrontend->mReturnPressed ? 5 : 6;
        return true;
    }
    if (pFrontend->mUpPressed || pFrontend->mDownPressed)
    {
        s32 i = 0;
        while (i < numLetters && kLetters[i] != pName[len - 1])
        {
            i++;
        }
        i = Wrap((i < numLetters ? i : 0) + (pFrontend->mUpPressed ? 1 : -1), numLetters);
        pName[len - 1] = kLetters[i];
        snd1.mObjectType = 7;
    }
    else if (pFrontend->mRightPressed && len < 8)
    {
        pName[len] = pName[len - 1] == L' ' ? L'A' : pName[len - 1];
        len++;
        snd1.mObjectType = 7;
    }
    else if (pFrontend->mLeftPressed && len > 1)
    {
        len--;
        snd1.mObjectType = 8;
    }
    pFrontend->StripPlayerNameToCurrLength();
    return true;
}

void PadKeyHint(u8 hint, wchar_t* pText)
{
    if (!PortMenu::PadMode())
    {
        return;
    }
    switch (hint)
    {
        case 0: // ESC
            wcscpy(pText, L"B");
            break;
        case 1: // DEL
            wcscpy(pText, L"X");
            break;
        case 2: // ENTER
            wcscpy(pText, L"A");
            break;
        case 3: // BACKSPACE
            wcscpy(pText, L"LEFT");
            break;
        default:
            break;
    }
}
} // namespace PortOptionsMenu
