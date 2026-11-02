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
    kCount,
};

// The pages: sound and text settings sit on the options page itself.
extern const Id kGeneral[];
extern const s32 kNumGeneral;
extern const Id kScreen[];
extern const s32 kNumScreen;
extern const Id kGraphics[];
extern const s32 kNumGraphics;

// The port's menu texts in the language of the loaded text table, from their English text.
const wchar_t* Translate(const wchar_t* pEnglish);

// In English, as the key to Translate.
const wchar_t* Name(Id id);
void Value(Id id, wchar_t* pOut);
// Changing the language reloads the text table and lays the frontend's or the HUD's texts out again.
void Change(Id id, s32 delta);

// Headlights: 0 none, 1 glow (the game's point lights), 2 beams.
s32 Headlights();

// The renderer's options from the registry, at start-up.
void ApplyRenderOptions();
} // namespace PortSettings
