#include "settings.hpp"
#include "Frontend.hpp"
#include "Function.hpp"
#include "Game.hpp"
#include "Globals.hpp"
#include "Hud.hpp"
#include "TextTable.hpp"
#include "map_0x370.hpp"
#include "registry.hpp"
#include "root_sound.hpp"
#include <stdio.h>
#include <string.h>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

void __stdcall SetStartMode(s32 startMode);
void __stdcall SetVideoModeFromSettings();
void __stdcall Init_FrameRateLightingAndExplodingScores();
void OnVideoModeChanged();
char_type IsWindowedModeAvailable();
bool port_GameRunning();
#if defined(GTA2_PORT_SDL)
void port_SetRenderOptions(bool trueColour, int bloomTenths, bool perPixelLighting);
#endif

namespace PortSettings
{
const Id kGeneral[] = {kSfxVolume, kMusicVolume, k3dSound, kTextSpeed, kLanguage};
const s32 kNumGeneral = sizeof(kGeneral) / sizeof(kGeneral[0]);
const Id kScreen[] = {kDisplay, kBrightness, kLighting, kExplodingScores, kFrameLimit};
const s32 kNumScreen = sizeof(kScreen) / sizeof(kScreen[0]);
#if defined(GTA2_PORT_SDL)
const Id kGraphics[] = {kTrueColour, kBloom, kPerPixelLighting, kHeadlights};
#else
const Id kGraphics[] = {kHeadlights};
#endif
const s32 kNumGraphics = sizeof(kGraphics) / sizeof(kGraphics[0]);

namespace
{
const wchar_t* const kNames[kCount] = {L"SOUND EFFECTS", L"MUSIC", L"3D SOUND", L"TEXT SPEED", L"LANGUAGE",
                                       L"DISPLAY", L"BRIGHTNESS", L"LIGHTING", L"EXPLODING SCORES", L"FRAME LIMIT",
                                       L"TRUE COLOUR", L"BLOOM", L"PIXEL LIGHTING", L"CAR HEADLIGHTS"};

struct Language
{
    char mCode;
    const wchar_t* mName;
};
const Language kLanguages[] = {{'e', L"ENGLISH"}, {'f', L"FRANCAIS"}, {'g', L"DEUTSCH"}, {'i', L"ITALIANO"}, {'s', L"ESPANOL"}};
const s32 kNumLanguages = sizeof(kLanguages) / sizeof(kLanguages[0]);

const wchar_t* const kTextSpeeds[] = {L"VERY FAST", L"FAST", L"NORMAL", L"SLOW", L"VERY SLOW"};
const wchar_t* const kHeadlightNames[] = {L"OFF", L"GLOW", L"BEAMS"};

s32 Clamp(s32 v, s32 lo, s32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

s32 Wrap(s32 v, s32 n)
{
    return (v % n + n) % n;
}

void SetOption(const char* name, DWORD type, const void* pData, DWORD size)
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

// Volumes are shown 0 to 10, stored 0 to 127.
s32 VolumeStep(s32 volume)
{
    return (volume * 10 + 63) / 127;
}

const char* ScreenName(Id id)
{
    switch (id)
    {
        case kLighting:
            return "lighting";
        case kExplodingScores:
            return "exploding_on";
        case kFrameLimit:
            return "max_frame_rate";
        case kTrueColour:
            return "gl_true_colour";
        case kBloom:
            return "gl_bloom";
        case kPerPixelLighting:
            return "gl_pixel_lighting";
        default:
            return "car_headlights";
    }
}

// The game's own settings are on by default; the port's graphics additions off.
s32 ScreenDefault(Id id)
{
    return id == kLighting || id == kExplodingScores || id == kFrameLimit ? 1 : 0;
}

s32 Screen(Id id)
{
    return gRegistry.Get_Screen_Setting(ScreenName(id), ScreenDefault(id));
}

// The zone name on screen is a copy of the text table's.
void RefreshZoneName()
{
    Hud_MapZone& zone = gHud_2B00->mZoneName;
    gmp_map_zone* pZone = zone.mLocalNavZone ? zone.mLocalNavZone : zone.mNavZone;
    wchar_t* pName = zone.mTimer && pZone ? pZone->get_zone_str() : NULL;
    if (pName)
    {
        wcsncpy(zone.mWstr, gText_0x14->StrToUpper(pName), 64);
        zone.mWstr[64] = 0;
    }
}

// The frontend copies its texts into its pages: set them up again, keeping the selected options.
void RefreshFrontend()
{
    const s32 numPages = sizeof(gFrontend->mMenuPagesArray) / sizeof(gFrontend->mMenuPagesArray[0]);
    u16 selected[numPages];
    for (s32 i = 0; i < numPages; i++)
    {
        selected[i] = gFrontend->mMenuPagesArray[i].mCurrentOptionIdx;
    }
    gFrontend->SetupMenuStringsOptionsElements();
    for (s32 i = 0; i < numPages; i++)
    {
        gFrontend->mMenuPagesArray[i].mCurrentOptionIdx = selected[i];
    }
}
} // namespace

const wchar_t* Name(Id id)
{
    return kNames[id];
}

s32 Headlights()
{
    return Screen(kHeadlights);
}

void ApplyRenderOptions()
{
#if defined(GTA2_PORT_SDL)
    port_SetRenderOptions(Screen(kTrueColour) != 0, Screen(kBloom), Screen(kPerPixelLighting) != 0);
#endif
}

void Value(Id id, wchar_t* pOut)
{
    pOut[0] = 0;
    switch (id)
    {
        case kSfxVolume:
        case kMusicVolume:
            swprintf(pOut, L"%d", VolumeStep(gRegistry.Get_Sound_Setting(id == kSfxVolume ? "SFXVol" : "CDVol", 127)));
            break;
        case k3dSound:
            wcscpy(pOut, gRegistry.Sound_Setting_Exists("do_3d_sound") ? L"ON" : L"OFF");
            break;
        case kTextSpeed:
            wcscpy(pOut, kTextSpeeds[Clamp(gRegistry.Get_Option_Int("text_speed", 3), 1, 5) - 1]);
            break;
        case kLanguage:
            wcscpy(pOut, kLanguages[LanguageIndex()].mName);
            break;
        case kDisplay:
            wcscpy(pOut, gRegistry.Get_Screen_Setting("start_mode", 1) ? L"FULL SCREEN" : L"WINDOW");
            break;
        case kBrightness:
            swprintf(pOut, L"%d", gRegistry.Get_Screen_Setting("gamma", 10));
            break;
        case kLighting:
            wcscpy(pOut, Screen(id) ? L"DUSK" : L"NOON");
            break;
        case kBloom:
            if (Screen(id))
            {
                swprintf(pOut, L"%d", Screen(id));
            }
            else
            {
                wcscpy(pOut, L"OFF");
            }
            break;
        case kHeadlights:
            wcscpy(pOut, kHeadlightNames[Clamp(Screen(id), 0, 2)]);
            break;
        default:
            wcscpy(pOut, Screen(id) ? L"ON" : L"OFF");
            break;
    }
}

void Change(Id id, s32 delta)
{
    const bool inGame = port_GameRunning();
    switch (id)
    {
        case kSfxVolume:
        case kMusicVolume:
        {
            const char* name = id == kSfxVolume ? "SFXVol" : "CDVol";
            const s32 volume = Clamp(VolumeStep(gRegistry.Get_Sound_Setting(name, 127)) + delta, 0, 10) * 127 / 10;
            gRegistry.Set_Sound_Setting(name, volume);
            if (id == kSfxVolume)
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
            if (inGame)
            {
                // As the game does when it starts: the setting stays on only if a 3D provider opened.
                gRoot_sound.Set3DSound(!gRegistry.Sound_Setting_Exists("do_3d_sound"));
                gRegistry.Clear_Or_Delete_Sound_Setting("do_3d_sound", gRoot_sound.Get3DSound());
            }
            else
            {
                gRegistry.Clear_Or_Delete_Sound_Setting("do_3d_sound", !gRegistry.Sound_Setting_Exists("do_3d_sound"));
            }
            break;
        case kTextSpeed:
        {
            const DWORD speed = static_cast<DWORD>(Clamp(gRegistry.Get_Option_Int("text_speed", 3) + delta, 1, 5));
            SetOption("text_speed", REG_DWORD, &speed, sizeof(speed));
            if (inGame)
            {
                gHud_2B00->GetTextSpeed();
            }
            break;
        }
        case kLanguage:
        {
            const char code[2] = {kLanguages[Wrap(LanguageIndex() + delta, kNumLanguages)].mCode, 0};
            SetOption("language", REG_SZ, code, sizeof(code));
            delete gText_0x14;
            gText_0x14 = new TextTable();
            gText_0x14->Load();
            if (inGame)
            {
                RefreshZoneName();
                gHud_2B00->RecalcTextLayout();
            }
            else if (gFrontend)
            {
                RefreshFrontend();
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
        case kBrightness:
        {
            const s32 gamma = Clamp(gRegistry.Get_Screen_Setting("gamma", 10) + delta, 1, 20);
            gRegistry.Set_Screen_Setting("gamma", gamma);
            SetGamma(gamma);
            break;
        }
        case kBloom:
            gRegistry.Set_Screen_Setting(ScreenName(id), Clamp(Screen(id) + delta, 0, 10));
            ApplyRenderOptions();
            break;
        case kHeadlights:
            gRegistry.Set_Screen_Setting(ScreenName(id), Wrap(Screen(id) + delta, 3));
            break;
        default:
            gRegistry.Set_Screen_Setting(ScreenName(id), Screen(id) ? 0 : 1);
            if (id == kLighting || id == kExplodingScores || id == kFrameLimit)
            {
                Init_FrameRateLightingAndExplodingScores();
            }
            else
            {
                ApplyRenderOptions();
            }
            break;
    }
}
} // namespace PortSettings
