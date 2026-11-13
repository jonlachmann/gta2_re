#include "pause_menu.hpp"
#include "Camera.hpp"
#include "Draw.hpp"
#include "Frontend.hpp"
#include "Function.hpp"
#include "Game.hpp"
#include "Globals.hpp"
#include "Hud.hpp"
#include "TextTable.hpp"
#include "TextureCache.hpp"
#include "cSampleManager.hpp"
#include "debug.hpp"
#include "gbh_graphics.hpp"
#include "gtx_0x106C.hpp"
#include "menu.hpp"
#include "registry.hpp"
#include "settings.hpp"
#include <stdio.h>
#include <vector>
#include "winmain.hpp"

EXTERN_GLOBAL(s16, word_7064D8);

namespace PortPauseMenu
{
namespace
{
bool sOpen;

// The frontend's style, for the menu's fonts and sprites, loaded with the city (Preload).
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
    if (!sStyle)
    {
        return;
    }
    UseFrontendStyle(true);
    GTA2_DELETE_AND_NULL(sTextures);
    GTA2_DELETE_AND_NULL(sStyle);
    UseGameStyle();
}

// The frontend's menu sounds, fstyle's samples 0 to 11: a left and a right one for each result up to
// kWentBack. The game's sound bank is the level's, so they play on sample handles of their own.
SdtEntry sSoundEntries[12];
std::vector<u8> sSoundData;
s32 sSoundFirst;
// Pairs used in turn: like the frontend's, a sound plays to its end over the next ones (about a
// second), as cutting it short clicks.
const s32 kSoundPairs = 8;
HSAMPLE sSoundHandles[kSoundPairs * 2];
s32 sNextPair;

bool LoadSoundData()
{
    FILE* pSdt = fopen("data\\audio\\FSTYLE.SDT", "rb");
    FILE* pRaw = fopen("data\\audio\\FSTYLE.RAW", "rb");
    bool ok = pSdt && pRaw && fread(sSoundEntries, sizeof(SdtEntry), 12, pSdt) == 12;
    if (ok)
    {
        sSoundFirst = sSoundEntries[0].mOffset;
        s32 end = 0;
        for (const SdtEntry& entry : sSoundEntries)
        {
            sSoundFirst = entry.mOffset < sSoundFirst ? entry.mOffset : sSoundFirst;
            end = entry.mOffset + entry.mSampleLength > end ? entry.mOffset + entry.mSampleLength : end;
        }
        sSoundData.resize(end - sSoundFirst);
        ok = fseek(pRaw, sSoundFirst, SEEK_SET) == 0 && fread(sSoundData.data(), 1, sSoundData.size(), pRaw) == sSoundData.size();
    }
    if (pSdt)
    {
        fclose(pSdt);
    }
    if (pRaw)
    {
        fclose(pRaw);
    }
    if (!ok)
    {
        sSoundData.clear();
    }
    return ok;
}

void OpenSounds()
{
    if (!gSampManager.mhDriver || (sSoundData.empty() && !LoadSoundData()))
    {
        return;
    }
    for (HSAMPLE& handle : sSoundHandles)
    {
        if (!handle)
        {
            handle = AIL_allocate_sample_handle(gSampManager.mhDriver);
        }
    }
}

// Once the menu closed, as each sound ends.
void ReleaseSounds()
{
    for (HSAMPLE& handle : sSoundHandles)
    {
        if (handle && AIL_sample_status(handle) != SMP_PLAYING)
        {
            AIL_release_sample_handle(handle);
            handle = NULL;
        }
    }
}

// As the frontend plays them, at the SOUND EFFECTS volume.
void PlaySound(PortMenu::Result result)
{
    if (result < PortMenu::kMovedUp || result > PortMenu::kWentBack)
    {
        return;
    }
    const s32 volume = gRegistry.Get_Sound_Setting("SFXVol", 127);
    for (s32 i = 0; i < 2; i++)
    {
        HSAMPLE handle = sSoundHandles[sNextPair * 2 + i];
        if (!handle)
        {
            continue;
        }
        const SdtEntry& entry = sSoundEntries[(result - PortMenu::kMovedUp) * 2 + i];
        AIL_end_sample(handle);
        AIL_init_sample(handle);
        AIL_set_sample_type(handle, DIG_F_MONO_16, DIG_PCM_SIGN);
        AIL_set_sample_address(handle, &sSoundData[entry.mOffset - sSoundFirst], entry.mSampleLength);
        AIL_set_sample_playback_rate(handle, entry.mPlayBackRate);
        AIL_set_sample_volume(handle, volume);
        AIL_set_sample_pan(handle, i ? 127 : 0);
        AIL_set_sample_loop_count(handle, 1);
        AIL_start_sample(handle);
    }
    sNextPair = (sNextPair + 1) % kSoundPairs;
}

void Close()
{
    sOpen = false;
    PortMenu::Close();
    if (gGame_0x40->IsPaused())
    {
        gGame_0x40->Unpause();
    }
}

void Apply(PortMenu::Result result)
{
    PlaySound(result);
    if (result == PortMenu::kClosed || result == PortMenu::kQuitGame)
    {
        Close();
    }
    if (result == PortMenu::kQuitGame)
    {
        gGame_0x40->ExitGameNoBonus(1, GameExitType::PlayerQuit_2);
    }
}
} // namespace

bool IsOpen()
{
    return sOpen;
}

void Preload()
{
    if (!sStyle)
    {
        LoadStyle();
    }
    if (gSampManager.mhDriver && sSoundData.empty())
    {
        LoadSoundData();
    }
}

void Release()
{
    sOpen = false;
    PortMenu::Close();
    FreeStyle();
}

bool HandleKey(s32 action, Player* pPlayer)
{
    if (bStartNetworkGame)
    {
        return false; // the server goes on without a pause: Escape is the network game's leave prompt
    }
    if (!sOpen)
    {
        if (action != DIK_ESCAPE || pPlayer->mShowQuitMessage || bSkip_quit_confirm || !pPlayer->IsUser())
        {
            return false;
        }
        sOpen = true;
        if (!sStyle)
        {
            LoadStyle();
        }
        OpenSounds();
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
    Apply(PortMenu::HandleInput(kInputs[i].mInput));
    return true;
}

Fix16 Fx(f32 v)
{
    return Fix16(static_cast<s32>(v * 16384.0f), 0);
}

static bool sDrawingStats;
static f32 sStatScale;
static f32 sStatLeft;
static f32 sStatTop;

bool IsDrawingStats()
{
    return sDrawingStats;
}

f32 PlaceUi(f32 pos, bool y)
{
    return (y ? sStatTop : sStatLeft) + pos * sStatScale;
}

// One row per section, in UpdatePauseSection's order and with its checks, packed between the HUD's
// multiplier (long lines reach it above y 43) and the panel, lowered to 140.
static void DrawStats(f32 scale, f32 left, f32 top)
{
    Hud_PauseScreen& pause = gHud_2B00->mPauseScreen;
    const Hud_PauseScreen saved = pause;
    u8 sections[7];
    s32 count = 0;
    pause.mCurrentPauseSection = HudPauseSection::tokens_collected_6;
    do
    {
        pause.mTimer = 1;
        pause.UpdatePauseSection();
        sections[count++] = pause.mCurrentPauseSection;
    } while (pause.mCurrentPauseSection != HudPauseSection::tokens_collected_6);

    const Fix16 hudScale = gViewCamera->mUiScale;
    gViewCamera->mUiScale = Fx(scale);
    sDrawingStats = true;
    sStatScale = scale;
    sStatLeft = left;
    // By section, the distance to the next row: icons are taller than text, the frenzy icon the tallest.
    const s32 kPitch[7] = {14, 17, 17, 16, 16, 15, 0};
    s32 y = 118;
    for (s32 i = 0; i < count - 1; i++)
    {
        y -= kPitch[sections[i]];
    }
    for (s32 i = 0; i < count; i++)
    {
        sStatTop = top + (y - 220) * scale;
        pause.mCurrentPauseSection = sections[i];
        pause.DrawPause();
        y += kPitch[sections[i]];
    }
    sDrawingStats = false;
    gViewCamera->mUiScale = hudScale;
    pause = saved;
}

void Draw()
{
    if (!sOpen)
    {
        ReleaseSounds();
        return;
    }
    PlaySound(PortMenu::UpdateCapture());

    // The 640x480 layout centred on the screen, inside the safe area but without the HUD scale, the menu
    // 10 lower (its panel then ends at the bottom) and above it the lines of the original's pause screen.
    const f32 scale = PortScreenUiScale().ToFloat() * (1 - 2 * PortSettings::SafeArea());
    const f32 left = (gViewCamera->mScreenPxWidth - 640.0f * scale) / 2;
    const f32 top = (gViewCamera->mScreenPxHeight - 480.0f * scale) / 2;
    const f32 menuTop = top + 10 * scale;
    UseFrontendStyle(false);
    const PortMenu::Result result = PortMenu::HandleMouse(scale, left, menuTop);
    if (result == PortMenu::kClosed || result == PortMenu::kQuitGame)
    {
        UseGameStyle();
        Apply(result);
        return;
    }
    PlaySound(result);
    PortMenu::Draw(scale, left, menuTop, true);
    UseGameStyle();
    DrawStats(scale, left, top);
}
} // namespace PortPauseMenu
