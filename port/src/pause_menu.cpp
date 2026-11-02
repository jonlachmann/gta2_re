#include "pause_menu.hpp"
#include "Camera.hpp"
#include "Draw.hpp"
#include "Frontend.hpp"
#include "Function.hpp"
#include "Game.hpp"
#include "Globals.hpp"
#include "TextTable.hpp"
#include "TextureCache.hpp"
#include "debug.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "menu.hpp"

EXTERN_GLOBAL(s16, word_7064D8);

namespace PortPauseMenu
{
namespace
{
bool sOpen;

// The frontend's style while the menu is open, for its fonts and sprites.
gtx_0x106C* sStyle;
TextureCache* sTextures;

// Its palettes go after the game's in the renderer's one palette table.
s32 sPaletteBase;
T_gbh_RegisterPalette sRegisterPalette;
T_gbh_FreePalette sFreePalette;
T_gbh_RegisterTexture sRegisterTexture;

u32 CC RegisterPalette(s32 palId, DWORD* pData)
{
    return sRegisterPalette(sPaletteBase + palId, pData);
}

void CC FreePalette(s32 palId)
{
    sFreePalette(sPaletteBase + palId);
}

STexture* CC RegisterTexture(s32 width, s32 height, BYTE* pData, s32 palId, char_type flag)
{
    return sRegisterTexture(width, height, pData, sPaletteBase + palId, flag);
}

gtx_0x106C* sGameStyle;
TextureCache* sGameTextures;

void UseFrontendStyle(bool registering)
{
    sGameStyle = gGtx_0x106C;
    sGameTextures = gTextureCache_15D8;
    gGtx_0x106C = sStyle;
    gTextureCache_15D8 = sTextures;
    if (registering)
    {
        sPaletteBase = sGameTextures->mPalCount;
        sRegisterPalette = pgbh_RegisterPalette;
        sFreePalette = pgbh_FreePalette;
        sRegisterTexture = pgbh_RegisterTexture;
        pgbh_RegisterPalette = RegisterPalette;
        pgbh_FreePalette = FreePalette;
        pgbh_RegisterTexture = RegisterTexture;
    }
}

void UseGameStyle()
{
    gGtx_0x106C = sGameStyle;
    gTextureCache_15D8 = sGameTextures;
    if (pgbh_RegisterPalette == RegisterPalette)
    {
        pgbh_RegisterPalette = sRegisterPalette;
        pgbh_FreePalette = sFreePalette;
        pgbh_RegisterTexture = sRegisterTexture;
    }
}

void LoadStyle()
{
    // Loading a style sets the font globals; the game's must stay.
    s16* const pFonts[] = {&gStyleMessageFont, &gStyleTextFont,  &gStyleCarNameFont, &gStyleZoneNameFont,
                           &gStylePauseFont,   &gKanjiSmallFont, &gKanjiLargeFont};
    s16 fonts[7];
    for (s32 i = 0; i < 7; i++)
    {
        fonts[i] = *pFonts[i];
    }
    sStyle = new gtx_0x106C();
    sTextures = new TextureCache();
    UseFrontendStyle(true);
    sStyle->LoadSty("data\\fstyle.sty");
    sTextures->LoadStyleTextures();
    ConvertColourBanks();
    UseGameStyle();
    for (s32 i = 0; i < 7; i++)
    {
        *pFonts[i] = fonts[i];
    }
}

void FreeStyle()
{
    UseFrontendStyle(true);
    GTA2_DELETE_AND_NULL(sTextures);
    GTA2_DELETE_AND_NULL(sStyle);
    UseGameStyle();
}

void Close()
{
    sOpen = false;
    FreeStyle();
    if (gGame_0x40->IsPaused())
    {
        gGame_0x40->Unpause();
    }
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
        LoadStyle();
        PortMenu::Open();
        if (!gGame_0x40->IsPaused())
        {
            gGame_0x40->Pause();
        }
        return true;
    }
    if (PortMenu::IsCapturing())
    {
        return true;
    }
    const struct
    {
        s32 mDik;
        PortMenu::Input mInput;
    } kInputs[] = {{DIK_UP, PortMenu::kUp},       {DIK_DOWN, PortMenu::kDown},     {DIK_LEFT, PortMenu::kLeft},
                   {DIK_RIGHT, PortMenu::kRight}, {DIK_RETURN, PortMenu::kSelect}, {DIK_ESCAPE, PortMenu::kCancel}};
    s32 i = 0;
    while (i < 6 && kInputs[i].mDik != action)
    {
        i++;
    }
    if (i == 6)
    {
        return true;
    }
    switch (PortMenu::HandleInput(kInputs[i].mInput))
    {
        case PortMenu::kClosed:
            Close();
            break;
        case PortMenu::kQuitGame:
            Close();
            gGame_0x40->ExitGameNoBonus(1, GameExitType::PlayerQuit_2);
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
    PortMenu::UpdateCapture();

    // The 640x480 layout centred on the screen, the menu then the original's banner over its panel, higher
    // to leave room for the menu.
    const f32 scale = gViewCamera->mUiScale.mValue / 16384.0f;
    const f32 left = (gViewCamera->mScreenPxWidth - 640.0f * scale) / 2;
    const f32 top = (gViewCamera->mScreenPxHeight - 480.0f * scale) / 2;
    UseFrontendStyle(false);
    PortMenu::Draw(scale, left, top, true);
    UseGameStyle();
    DrawFigureScaled(sprite_types_enum::user_6, 134, 227, 100, Ang16(0), palette_types_enum::sprites_2, 0, 0, 0);
    DrawFigureScaled(sprite_types_enum::user_6, 136, 320, 100, Ang16(0), palette_types_enum::sprites_2, 0, 0, 0);
    DrawFigureScaled(sprite_types_enum::user_6, 135, 413, 100, Ang16(0), palette_types_enum::sprites_2, 0, 0, 0);
    wchar_t* pPause = gText_0x14->Find("pause");
    DrawStringScaledPalette(pPause, (640 - Frontend::GetMaxTextWidth(pPause, static_cast<u16>(word_7064D8))) / 2, 78,
                            static_cast<u16>(word_7064D8), palette_types_enum::sprites_2, 0, 0, 0);
}
} // namespace PortPauseMenu
