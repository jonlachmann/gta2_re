#include "settings.hpp"
#include "frame_loop.hpp"
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
void port_ApplyVsync();
int port_FullscreenModeCount();
int port_FullscreenModeIndex();
void port_FullscreenModeSize(int index, int* pW, int* pH, int* pCentiHz);
void port_SetFullscreenModeIndex(int index);
#endif

namespace PortSettings
{
const Id kGeneral[] = {kSfxVolume, kMusicVolume, k3dSound, kTextSpeed, kLanguage};
const s32 kNumGeneral = sizeof(kGeneral) / sizeof(kGeneral[0]);
#if defined(GTA2_PORT_SDL)
const Id kScreen[] = {kDisplay, kDisplayMode, kVsync, kFrameLimit, kBrightness, kLighting, kExplodingScores};
#else
const Id kScreen[] = {kDisplay, kFrameLimit, kBrightness, kLighting, kExplodingScores};
#endif
const s32 kNumScreen = sizeof(kScreen) / sizeof(kScreen[0]);
#if defined(GTA2_PORT_SDL)
const Id kGraphics[] = {kTrueColour, kBloom, kPerPixelLighting, kHeadlights, kLightEffects};
#else
const Id kGraphics[] = {kHeadlights, kLightEffects};
#endif
const s32 kNumGraphics = sizeof(kGraphics) / sizeof(kGraphics[0]);

namespace
{
const wchar_t* const kNames[kCount] = {L"SOUND EFFECTS", L"MUSIC", L"3D SOUND", L"TEXT SPEED", L"LANGUAGE",
                                       L"DISPLAY", L"BRIGHTNESS", L"LIGHTING", L"EXPLODING SCORES", L"FRAME LIMIT",
                                       L"TRUE COLOUR", L"BLOOM", L"PIXEL LIGHTING", L"CAR HEADLIGHTS",
                                       L"VSYNC", L"DISPLAY MODE", L"LIGHT EFFECTS"};

struct Language
{
    char mCode;
    const wchar_t* mName;
};
const Language kLanguages[] = {{'e', L"ENGLISH"}, {'f', L"FRANCAIS"}, {'g', L"DEUTSCH"}, {'i', L"ITALIANO"}, {'s', L"ESPANOL"}};
const s32 kNumLanguages = sizeof(kLanguages) / sizeof(kLanguages[0]);

const wchar_t* const kTextSpeeds[] = {L"VERY FAST", L"FAST", L"NORMAL", L"SLOW", L"VERY SLOW"};
const s32 kMaxFpsValues[] = {30, 60, 120, 144, 240, 0};
const s32 kNumMaxFpsValues = sizeof(kMaxFpsValues) / sizeof(kMaxFpsValues[0]);

// A text of the port's menus: a key of the game's text table, or the text in French, German, Italian
// and Spanish (Latin-1 letters, uppercase as the game writes them), many from GTA2 Manager's data\bob_?.gxt.
struct Translation
{
    const wchar_t* mEnglish;
    const char* mGameKey;
    const wchar_t* mText[4];
};
const Translation kTranslations[] = {
    {L"OPTIONS", "options", {}},
    {L"BACK", "back", {}},
    {L"CANCEL", "psxcanc", {}},
    {L"ESC", "esc_i", {}},
    {L"UP", "up_i", {}},
    {L"DOWN", "down_i", {}},
    {L"LEFT", "left_i", {}},
    {L"RIGHT", "right_i", {}},
    {L"FAST", "psxfast", {}},
    {L"NORMAL", "psxnorm", {}},
    {L"SLOW", "psxslow", {}},
    {L"RESUME", "psxret", {}},
    {L"QUIT GAME", "psxquit", {}},
    {L"ENTER", NULL, {L"ENTREE", L"EINGABE", L"INVIO", L"INTRO"}},
    {L"SELECT", NULL, {L"CHOISIR", L"WÄHLEN", L"SCEGLI", L"ELEGIR"}},
    {L"VERY FAST", NULL, {L"TRES RAPIDE", L"SEHR SCHNELL", L"MOLTO VELOCE", L"MUY RÁPIDA"}},
    {L"VERY SLOW", NULL, {L"TRES LENTE", L"SEHR LANGSAM", L"MOLTO LENTA", L"MUY LENTA"}},
    {L"SOUND EFFECTS", NULL, {L"EFFETS SONORES", L"SOUNDEFFEKTE", L"EFFETTI SONORI", L"EFECTOS SONOROS"}},
    {L"MUSIC", NULL, {L"MUSIQUE", L"MUSIK", L"MUSICA", L"MÚSICA"}},
    {L"3D SOUND", NULL, {L"SON 3D", L"3D-SOUND", L"SUONO 3D", L"SONIDO 3D"}},
    {L"TEXT SPEED", NULL, {L"VITESSE TEXTE", L"TEXTTEMPO", L"VELOCITÀ TESTO", L"VELOCIDAD TEXTO"}},
    {L"LANGUAGE", NULL, {L"LANGUE", L"SPRACHE", L"LINGUA", L"IDIOMA"}},
    {L"DISPLAY", NULL, {L"AFFICHAGE", L"ANZEIGE", L"SCHERMO", L"PANTALLA"}},
    {L"FULL SCREEN", NULL, {L"PLEIN ECRAN", L"VOLLBILD", L"INTERO", L"COMPLETA"}},
    {L"WINDOW", NULL, {L"FENETRE", L"FENSTER", L"FINESTRA", L"VENTANA"}},
    {L"BRIGHTNESS", NULL, {L"VALEUR GAMMA", L"GAMMA-WERT", L"VALORE GAMMA", L"VALOR GAMMA"}},
    {L"LIGHTING", NULL, {L"ECLAIRAGE", L"LICHT", L"ILLUMINAZIONE", L"LUCES"}},
    {L"DUSK", NULL, {L"CREPUSCULE", L"DÄMMERUNG", L"CREPUSCOLO", L"ATARDECER"}},
    {L"NOON", NULL, {L"MIDI", L"MITTAG", L"MEZZOGIORNO", L"MEDIODÍA"}},
    {L"EXPLODING SCORES", NULL, {L"SCORES EXPLOSIFS", L"PUNKTE-EXPLOSION", L"PUNTI ESPLOSIVI", L"PUNTOS EXPLOSIVOS"}},
    {L"FRAME LIMIT", NULL, {L"LIMITE IMAGES", L"BILDRATENLIMIT", L"LIMITE FOTOGRAMMI", L"LÍMITE IMÁGENES"}},
    {L"VSYNC", NULL, {L"VSYNC", L"VSYNC", L"VSYNC", L"VSYNC"}},
    {L"ORIGINAL", NULL, {L"ORIGINAL", L"ORIGINAL", L"ORIGINALE", L"ORIGINAL"}},
    {L"UNLIMITED", NULL, {L"ILLIMITE", L"UNBEGRENZT", L"ILLIMITATO", L"ILIMITADO"}},
    {L"DISPLAY MODE", NULL, {L"MODE VIDEO", L"BILDSCHIRMMODUS", L"MODALITÀ VIDEO", L"MODO DE VÍDEO"}},
    {L"DESKTOP", NULL, {L"BUREAU", L"DESKTOP", L"DESKTOP", L"ESCRITORIO"}},
    {L"TRUE COLOUR", NULL, {L"VRAIES COULEURS", L"ECHTFARBEN", L"COLORI REALI", L"COLOR REAL"}},
    {L"PIXEL LIGHTING", NULL, {L"ECLAIRAGE PIXEL", L"PIXEL-LICHT", L"LUCE PER PIXEL", L"LUZ POR PÍXEL"}},
    {L"CAR HEADLIGHTS", NULL, {L"PHARES", L"SCHEINWERFER", L"FARI", L"FAROS"}},
    {L"LIGHT EFFECTS", NULL, {L"EFFETS LUMINEUX", L"LICHTEFFEKTE", L"EFFETTI LUCE", L"EFECTOS DE LUZ"}},
    {L"ON", NULL, {L"OUI", L"AN", L"SÌ", L"SÍ"}},
    {L"OFF", NULL, {L"NON", L"AUS", L"NO", L"NO"}},
    {L"SCREEN", NULL, {L"ECRAN", L"BILDSCHIRM", L"SCHERMO", L"PANTALLA"}},
    {L"GRAPHICS", NULL, {L"GRAPHISMES", L"GRAFIK", L"GRAFICA", L"GRÁFICOS"}},
    {L"CONTROLS", NULL, {L"COMMANDES", L"STEUERUNG", L"COMANDI", L"CONTROLES"}},
    {L"MORE", NULL, {L"SUITE", L"MEHR", L"ALTRI", L"MÁS"}},
    {L"DEFAULT KEYS", NULL, {L"CONTROLES PAR DEFAUT", L"STANDARDSTEUERUNG", L"COMANDI PREDEFINITI", L"CONTROLES PREDETERMINADOS"}},
    {L"KEYBOARD", NULL, {L"CLAVIER", L"TASTATUR", L"TASTIERA", L"TECLADO"}},
    {L"GAMEPAD", NULL, {L"MANETTE", L"GAMEPAD", L"GAMEPAD", L"MANDO"}},
    {L"PRESS", NULL, {L"APPUYEZ", L"DRÜCKEN", L"PREMI", L"PULSA"}},
    {L"FORWARD", NULL, {L"AVANT", L"VORWÄRTS", L"AVANTI", L"ADELANTE"}},
    {L"BACKWARD", NULL, {L"ARRIERE", L"ZURÜCK", L"INDIETRO", L"ATRÁS"}},
    {L"ATTACK", NULL, {L"ATTAQUE", L"ANGRIFF", L"ATTACCA", L"ATACAR"}},
    {L"ENTER OR EXIT", NULL, {L"ENTREE/SORTIE", L"EIN-/AUSSTEIGEN", L"ENTRA/ESCI", L"ENTRAR/SALIR"}},
    {L"JUMP OR BRAKE", NULL, {L"FREIN/SAUT", L"SPRUNG/BREMSE", L"FRENA/SALTA", L"FRENO/SALTAR"}},
    {L"PREVIOUS WEAPON", NULL, {L"ARME PRECEDENTE", L"VORIGE WAFFE", L"ARMA PRECEDENTE", L"ARMA ANTERIOR"}},
    {L"NEXT WEAPON", NULL, {L"ARME SUIVANTE", L"NÄCHSTE WAFFE", L"ARMA SUCCESSIVA", L"ARMA SIGUIENTE"}},
    {L"SPECIAL 1", NULL, {L"SPECIAL 1", L"SPEZIAL 1", L"SPECIALE 1", L"ESPECIAL 1"}},
    {L"SPECIAL 2", NULL, {L"SPECIAL 2", L"SPEZIAL 2", L"SPECIALE 2", L"ESPECIAL 2"}},
    {L"SPECIAL 3", NULL, {L"SPECIAL 3", L"SPEZIAL 3", L"SPECIALE 3", L"ESPECIAL 3"}},
};
const s32 kNumTranslations = sizeof(kTranslations) / sizeof(kTranslations[0]);

// The texts in the game's font codes, for sCacheLang.
wchar_t sCache[kNumTranslations][32];
char sCacheLang;

s32 TranslationLanguage(char code)
{
    switch (code)
    {
        case 'f':
            return 0;
        case 'g':
            return 1;
        case 'i':
            return 2;
        case 's':
            return 3;
        default:
            return -1;
    }
}

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
            return "port_max_fps";
        case kVsync:
            return "port_vsync";
        case kTrueColour:
            return "gl_true_colour";
        case kBloom:
            return "gl_bloom";
        case kPerPixelLighting:
            return "gl_pixel_lighting";
        case kLightEffects:
            return "light_effects";
        default:
            return "car_headlights";
    }
}

// The game's own settings are on by default; the port's graphics additions off.
s32 ScreenDefault(Id id)
{
    if (id == kFrameLimit)
    {
        return 30;
    }
    return id == kLighting || id == kExplodingScores || id == kVsync ? 1 : 0;
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

const wchar_t* Translate(const wchar_t* pEnglish)
{
    const s32 lang = gText_0x14 ? TranslationLanguage(gText_0x14->mLangCode) : -1;
    if (lang < 0)
    {
        return pEnglish;
    }
    if (sCacheLang != gText_0x14->mLangCode)
    {
        sCacheLang = gText_0x14->mLangCode;
        for (s32 i = 0; i < kNumTranslations; i++)
        {
            const wchar_t* pText = kTranslations[i].mText[lang];
            wcsncpy(sCache[i], pText ? pText : kTranslations[i].mEnglish, 31);
            sCache[i][31] = 0;
            gText_0x14->RemapExtendedChars(sCache[i]);
        }
    }
    for (s32 i = 0; i < kNumTranslations; i++)
    {
        if (!wcscmp(kTranslations[i].mEnglish, pEnglish))
        {
            const char* pKey = kTranslations[i].mGameKey;
            if (pKey)
            {
                return gText_0x14->KeyExists(pKey) ? gText_0x14->Find(pKey) : pEnglish;
            }
            return sCache[i];
        }
    }
    return pEnglish;
}

const wchar_t* Name(Id id)
{
    return kNames[id];
}

bool Headlights()
{
    return Screen(kHeadlights) != 0;
}

bool LightEffects()
{
    return Screen(kLightEffects) != 0;
}

// The original's max_frame_rate is not read: off, it ran the game as fast as the machine.
s32 MaxFps()
{
    const s32 fps = Screen(kFrameLimit);
    for (s32 i = 0; i < kNumMaxFpsValues; i++)
    {
        if (kMaxFpsValues[i] == fps)
        {
            return fps;
        }
    }
    return 30;
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
            wcscpy(pOut, Translate(gRegistry.Sound_Setting_Exists("do_3d_sound") ? L"ON" : L"OFF"));
            break;
        case kTextSpeed:
            wcscpy(pOut, Translate(kTextSpeeds[Clamp(gRegistry.Get_Option_Int("text_speed", 3), 1, 5) - 1]));
            break;
        case kLanguage:
            wcscpy(pOut, kLanguages[LanguageIndex()].mName);
            break;
        case kDisplay:
            wcscpy(pOut, Translate(gRegistry.Get_Screen_Setting("start_mode", 1) ? L"FULL SCREEN" : L"WINDOW"));
            break;
        case kBrightness:
            swprintf(pOut, L"%d", gRegistry.Get_Screen_Setting("gamma", 10));
            break;
        case kLighting:
            wcscpy(pOut, Translate(Screen(id) ? L"DUSK" : L"NOON"));
            break;
        case kBloom:
            if (Screen(id))
            {
                swprintf(pOut, L"%d", Screen(id));
            }
            else
            {
                wcscpy(pOut, Translate(L"OFF"));
            }
            break;
        case kFrameLimit:
            if (MaxFps() == 30)
            {
                wcscpy(pOut, L"30 ");
                wcscpy(pOut + 3, Translate(L"ORIGINAL"));
            }
            else if (MaxFps())
            {
                swprintf(pOut, L"%d", MaxFps());
            }
            else
            {
                wcscpy(pOut, Translate(L"UNLIMITED"));
            }
            break;
#if defined(GTA2_PORT_SDL)
        case kDisplayMode:
        {
            const int index = port_FullscreenModeIndex();
            int w;
            int h;
            int centiHz;
            port_FullscreenModeSize(index, &w, &h, &centiHz);
            if (!index)
            {
                wcscpy(pOut, Translate(L"DESKTOP"));
            }
            else
            {
                swprintf(pOut, L"%dX%d %dHZ", w, h, (centiHz + 50) / 100);
            }
            break;
        }
#endif
        default:
            wcscpy(pOut, Translate(Screen(id) ? L"ON" : L"OFF"));
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
        case kLightEffects:
            gRegistry.Set_Screen_Setting(ScreenName(id), Screen(id) ? 0 : 1);
            break;
        case kFrameLimit:
        {
            s32 i = 0;
            while (i < kNumMaxFpsValues - 1 && kMaxFpsValues[i] != MaxFps())
            {
                i++;
            }
            gRegistry.Set_Screen_Setting(ScreenName(id), kMaxFpsValues[Wrap(i + delta, kNumMaxFpsValues)]);
            PortFrameLoop::ApplySettings();
            break;
        }
#if defined(GTA2_PORT_SDL)
        case kVsync:
            gRegistry.Set_Screen_Setting(ScreenName(id), Screen(id) ? 0 : 1);
            port_ApplyVsync();
            break;
        case kDisplayMode:
            port_SetFullscreenModeIndex(Wrap(port_FullscreenModeIndex() + delta, port_FullscreenModeCount()));
            if (gRegistry.Get_Screen_Setting("start_mode", 1))
            {
                SetVideoModeFromSettings();
                OnVideoModeChanged();
            }
            break;
#endif
        default:
            gRegistry.Set_Screen_Setting(ScreenName(id), Screen(id) ? 0 : 1);
            if (id == kLighting || id == kExplodingScores)
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
