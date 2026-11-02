#include "options_menu.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "SoundObject.hpp"
#include "menu.hpp"
#include "settings.hpp"
#include <string.h>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

EXTERN_GLOBAL(SoundObject, snd1);

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
        return PortMenu::UpdateCapture();
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
    return PortMenu::kNothing;
}
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
        sShown = false;
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
        PortMenu::Draw(1.0f, 0.0f, 0.0f);
    }
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
