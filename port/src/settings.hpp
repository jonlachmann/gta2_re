// The settings both option menus show (frontend pages and in-game pause menu): their names, the
// text of their values, and changing them (stored in the registry, applied at once where possible).
#pragma once

#include "types.hpp"

namespace PortSettings
{
enum Id
{
    kSfxVolume,
    kMusicVolume,
    k3dSound,
    kTextSpeed,
    kLanguage,
    kDisplay,
    kBrightness,
    kLighting,
    kExplodingScores,
    kFrameLimit,
    kTrueColour,
    kBloom,
    kPerPixelLighting,
    kHeadlights,
    kVsync,
    kDisplayMode,
    kZoom,
    kHudScale,
    kSafeArea,
    kLightEffects,
    kTextureFilter,
    kAntiAliasing,
    kShadows,
    kPreset,
    kDayLength,
    kStartTime,
    kClock,
    kControls,
    kAimCamera,
    kBlood,
    kUpdateCheck,
    kCount,
};

// The pages: sound and text settings sit on the options page itself.
extern const Id kGeneral[];
extern const s32 kNumGeneral;
extern const Id kScreen[];
extern const s32 kNumScreen;
extern const Id kGraphics[];
extern const s32 kNumGraphics;
extern const Id kView[];
extern const s32 kNumView;

// The port's menu texts in the language of the loaded text table, from their English text.
const wchar_t* Translate(const wchar_t* pEnglish);

// data\<code>.gxt is there: the 2004 freeware has only English and Spanish.
bool LanguageInstalled(char code);

// In English, as the key to Translate.
const wchar_t* Name(Id id);
void Value(Id id, wchar_t* pOut);
// The numeric settings, shown as sliders: false for the others.
bool Range(Id id, s32* pMin, s32* pMax, s32* pValue);
// The setting another one only works with, kCount if none.
Id Requirement(Id id);
// False while the setting's requirement is off: the menu shows it greyed and doesn't change it, and its
// stored value is kept.
bool Enabled(Id id);
// Changing the language reloads the text table and lays the frontend's or the HUD's texts out again.
void Change(Id id, s32 delta);

// Car headlight beams, and the other added lights (lights.hpp), with the dusk and pixel lighting.
bool Headlights();
bool LightEffects();

// LIGHTING's DAY-NIGHT CYCLE (day_night.hpp), its CYCLE LENGTH in minutes and its START TIME, the hour a
// level starts at.
bool DayNightCycle();
s32 DayLength();
s32 StartTime();
// CLOCK: the cycle's hour on the HUD.
bool Clock();

// HUD SCALE: 1 for the original's size.
f32 HudScale();

// CONTROLS: TWIN STICK moves and aims apart on foot (twin_stick.hpp).
bool TwinStick();
// AIM CAMERA: with TWIN STICK, the view looks ahead toward the aim.
bool AimCamera();

// CHECK FOR UPDATES: the start menu shows a newer release (options_menu.hpp).
bool UpdateCheck();

// SAFE AREA: the part of the screen's width or height kept clear at each edge, 0 to 0.1.
f32 SafeArea();

// FRAME LIMIT: frames drawn per second, 30 the original's pacing, 0 no limit.
s32 MaxFps();

// The renderer's options from the registry, at start-up.
void ApplyRenderOptions();
} // namespace PortSettings
