#include "menu.hpp"
#include "Draw.hpp"
#include "Frontend.hpp"
#include "Function.hpp"
#include "Globals.hpp"
#include "InputRecorder_67F8B0.hpp"
#include "KeyboardLayout.hpp"
#include "input.hpp"
#include "registry.hpp"
#include "settings.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

// No std::wstring: with -fshort-wchar its traits would call the C library's 4-byte wide functions.

bool port_GameRunning();
#if defined(GTA2_PORT_SDL)
bool port_PadMode();
int port_GetPadBinding(int control);
void port_SetPadBinding(int control, int code);
int port_DefaultPadBinding(int control);
void port_CaptureStart();
int port_CapturedKey();
int port_CapturedPad();
#endif

namespace PortMenu
{
namespace
{
using PortSettings::Translate;

enum Page
{
    kMainPage,
    kScreenPage,
    kGraphicsPage,
    kControlsPage,
    kControls2Page,
    kNumPages,
};

enum Kind
{
    kGoTo,
    kSetting,
    kBinding,
    kDefaultKeys,
    kBack,
    kResume,
    kQuit,
};

struct Item
{
    Kind mKind;
    s32 mArg; // page, setting or control
    bool mGap; // set apart from the item above
};

const s32 kMaxItems = 12;
const wchar_t* const kTitles[kNumPages] = {L"OPTIONS", L"SCREEN", L"GRAPHICS", L"CONTROLS", L"CONTROLS"};

const s32 kNumControls = 12;
const s32 kDefaultControls[kNumControls] = {DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT, DIK_LCONTROL, DIK_RETURN,
                                            DIK_SPACE, DIK_Z, DIK_X, DIK_TAB, DIK_LMENU, DIK_RSHIFT};
const wchar_t* const kControlNames[kNumControls] = {L"FORWARD", L"BACKWARD", L"LEFT", L"RIGHT", L"ATTACK",
                                                    L"ENTER OR EXIT", L"JUMP OR BRAKE", L"PREVIOUS WEAPON",
                                                    L"NEXT WEAPON", L"SPECIAL 1", L"SPECIAL 2", L"SPECIAL 3"};
const s32 kOnFirstPage = 8;
// Pad binding codes (compat/src/pad.cpp): buttons 0 to 11, then the stick and d-pad directions.
const wchar_t* const kPadButtonNames[12] = {L"A", L"B", L"X", L"Y", L"LB", L"RB", L"BACK", L"START", L"L3", L"R3", L"LT", L"RT"};
const s32 kPadB = 1;
const s32 kPadBack = 6;
const s32 kPadLeft = 224;
const s32 kPadRight = 225;
const s32 kPadUp = 226;
const s32 kPadDown = 227;

// The frontend's user sprites: a disc behind a pad button's name, an arrow.
const s16 kDiscSprite = 2;
const s16 kArrowSprite = 38;

Page sPage;
s32 sSelected[kNumPages];
s32 sColumn; // 0 keyboard, 1 gamepad
s32 sCapturing = -1; // control waiting for a key or pad input
#if !defined(GTA2_PORT_SDL)
char_type sPrevKeys[256];
#endif

struct KeyName
{
    wchar_t mName[24];
};
std::vector<KeyName> sKeyNames;

s32 AddSettings(Item* pItems, s32 n, const PortSettings::Id* pIds, s32 count, bool gap)
{
    for (s32 i = 0; i < count; i++)
    {
        pItems[n++] = Item{kSetting, pIds[i], gap && i == 0};
    }
    return n;
}

s32 AddBindings(Item* pItems, s32 n, s32 first, s32 last)
{
    for (s32 i = first; i < last; i++)
    {
        pItems[n++] = Item{kBinding, i, false};
    }
    return n;
}

s32 Items(Page page, Item* pItems)
{
    const bool inGame = port_GameRunning();
    s32 n = 0;
    switch (page)
    {
        case kMainPage:
            if (inGame)
            {
                pItems[n++] = Item{kResume, 0, false};
            }
            pItems[n++] = Item{kGoTo, kScreenPage, false};
            pItems[n++] = Item{kGoTo, kGraphicsPage, false};
            pItems[n++] = Item{kGoTo, kControlsPage, false};
            n = AddSettings(pItems, n, PortSettings::kGeneral, PortSettings::kNumGeneral, true);
            pItems[n++] = Item{inGame ? kQuit : kBack, 0, true};
            break;
        case kScreenPage:
            n = AddSettings(pItems, n, PortSettings::kScreen, PortSettings::kNumScreen, false);
            pItems[n++] = Item{kBack, 0, true};
            break;
        case kGraphicsPage:
            n = AddSettings(pItems, n, PortSettings::kGraphics, PortSettings::kNumGraphics, false);
            pItems[n++] = Item{kBack, 0, true};
            break;
        case kControlsPage:
            n = AddBindings(pItems, n, 0, kOnFirstPage);
            pItems[n++] = Item{kGoTo, kControls2Page, true};
            pItems[n++] = Item{kBack, 0, false};
            break;
        default:
            n = AddBindings(pItems, n, kOnFirstPage, kNumControls);
            pItems[n++] = Item{kDefaultKeys, 0, true};
            pItems[n++] = Item{kBack, 0, false};
            break;
    }
    return n;
}

const wchar_t* EnglishName(const Item& item)
{
    switch (item.mKind)
    {
        case kGoTo:
            return item.mArg == kControls2Page ? L"MORE" : kTitles[item.mArg];
        case kSetting:
            return PortSettings::Name(static_cast<PortSettings::Id>(item.mArg));
        case kBinding:
            return kControlNames[item.mArg];
        case kDefaultKeys:
            return L"DEFAULT KEYS";
        case kResume:
            return L"RESUME";
        case kQuit:
            return L"QUIT GAME";
        default:
            return L"BACK";
    }
}

const wchar_t* ItemName(const Item& item)
{
    return Translate(EnglishName(item));
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

void KeyName(s32 dik, wchar_t* pOut)
{
    dik &= 0xFF;
    if (dik < static_cast<s32>(sKeyNames.size()) && sKeyNames[dik].mName[0])
    {
        wcscpy(pOut, sKeyNames[dik].mName);
    }
    else
    {
        swprintf(pOut, L"KEY %d", dik);
    }
}

void PadName(s32 code, wchar_t* pOut)
{
    static const wchar_t* const kDirections[] = {L"LEFT", L"RIGHT", L"UP", L"DOWN"};
    if (code >= kPadLeft && code <= kPadDown)
    {
        wcscpy(pOut, Translate(kDirections[code - kPadLeft]));
    }
    else
    {
        wcscpy(pOut, code >= 0 && code < 12 ? kPadButtonNames[code] : L"-");
    }
}

s32 GetKey(s32 control)
{
    return gRegistry.Get_Control_Setting(control, kDefaultControls[control]) & 0xFF;
}

void SetKey(s32 control, s32 key)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\DMA Design Ltd\\GTA2\\Control", 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                        &hKey, NULL) != ERROR_SUCCESS)
    {
        return;
    }
    char name[4];
    sprintf(name, "%d", control);
    const DWORD data = static_cast<DWORD>(key);
    RegSetValueExA(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(hKey);
}

// Column 0 the keyboard, 1 the gamepad.
s32 GetBinding(s32 column, s32 control)
{
#if defined(GTA2_PORT_SDL)
    return column ? port_GetPadBinding(control) : GetKey(control);
#else
    return column ? -1 : GetKey(control);
#endif
}

void SetBinding(s32 column, s32 control, s32 binding)
{
#if defined(GTA2_PORT_SDL)
    column ? port_SetPadBinding(control, binding) : SetKey(control, binding);
#else
    column ? (void)0 : SetKey(control, binding);
#endif
}

// The game reads the keyboard's bindings when it starts, the pad's at each press.
void ReloadKeys()
{
    if (gInputDevices)
    {
        gInputDevices->get_registry_controls();
    }
}

// A key or pad input already used by another control moves to the one being changed.
void Bind(s32 column, s32 control, s32 binding)
{
    const s32 old = GetBinding(column, control);
    for (s32 i = 0; i < kNumControls; i++)
    {
        if (i != control && GetBinding(column, i) == binding)
        {
            SetBinding(column, i, old);
        }
    }
    SetBinding(column, control, binding);
    ReloadKeys();
}

void DefaultKeys()
{
    for (s32 i = 0; i < kNumControls; i++)
    {
        SetKey(i, kDefaultControls[i]);
#if defined(GTA2_PORT_SDL)
        port_SetPadBinding(i, port_DefaultPadBinding(i));
#endif
    }
    ReloadKeys();
}

#if !defined(GTA2_PORT_SDL)
// The DirectX build has no SDL layer: the first key pressed in the frontend's key state.
s32 FrontendKeyPressed()
{
    s32 result = -1;
    for (s32 i = 0; i < 256 && result < 0; i++)
    {
        if ((gFrontend->mKeys[i] & 0x80) && !(sPrevKeys[i] & 0x80))
        {
            result = i;
        }
    }
    memcpy(sPrevKeys, gFrontend->mKeys, sizeof(sPrevKeys));
    return result;
}
#endif

void GoTo(Page page)
{
    if (page == kControlsPage && sPage == kMainPage)
    {
        sColumn = PadMode() ? 1 : 0;
    }
    sPage = page;
    sSelected[page] = 0;
}

Result Back()
{
    if (sPage == kMainPage)
    {
        return kClosed;
    }
    sPage = sPage == kControls2Page ? kControlsPage : kMainPage;
    return kWentBack;
}

// Drawing: the layout's coordinates to the screen's.
f32 sScale;
f32 sLeft;
f32 sTop;

Fix16 Fx(f32 v)
{
    return Fix16(static_cast<s32>(v * 16384.0f), 0);
}

s32 Width(const wchar_t* pText, s16 font)
{
    return Frontend::GetMaxTextWidth(const_cast<wchar_t*>(pText), font);
}

void Text(const wchar_t* pText, f32 x, f32 y, s16 font)
{
    DrawStringDefaultPalette(pText, Fx(sLeft + x * sScale), Fx(sTop + y * sScale), font, Fx(sScale));
}

void Sprite(s16 sprite, f32 x, f32 y)
{
    DrawFigure(sprite_types_enum::user_6, sprite, Fx(sLeft + x * sScale), Fx(sTop + y * sScale), Ang16(0), Fx(sScale),
               palette_types_enum::sprites_2, 0, 0, 0, false);
}

f32 Max(f32 a, f32 b)
{
    return a > b ? a : b;
}

void Panel(f32 left, f32 top, f32 right, f32 bottom)
{
    Vert verts[4] = {};
    verts[0].x = verts[3].x = sLeft + left * sScale;
    verts[1].x = verts[2].x = sLeft + right * sScale;
    verts[0].y = verts[1].y = sTop + top * sScale;
    verts[2].y = verts[3].y = sTop + bottom * sScale;
    pgbh_DrawFlatRect(verts, 0);
}

// A setting's value, or a binding's key and pad input; empty for other items.
void ItemValues(const Item& item, wchar_t* pValue, wchar_t* pPad)
{
    pValue[0] = 0;
    pPad[0] = 0;
    if (item.mKind == kSetting)
    {
        PortSettings::Value(static_cast<PortSettings::Id>(item.mArg), pValue);
    }
    else if (item.mKind == kBinding)
    {
        KeyName(GetKey(item.mArg), pValue);
        PadName(GetBinding(1, item.mArg), pPad);
        if (sCapturing == item.mArg)
        {
            wcscpy(sColumn ? pPad : pValue, Translate(L"PRESS"));
        }
    }
}

// The keys at the bottom, or the pad's buttons in the frontend's red discs once the pad was used last.
void DrawHints()
{
    struct Hint
    {
        const wchar_t* mKey;
        const wchar_t* mButton;
        const wchar_t* mLabel;
    };
    const Hint kNavigate[] = {{L"ENTER", L"A", L"SELECT"}, {L"ESC", L"B", L"BACK"}};
    const bool pad = PadMode();
    // Capture: B cancels on the keyboard column, Back on the pad column (B can be bound there).
    const Hint kCapture[] = {{pad && sColumn ? L"BACK" : L"ESC", pad && !sColumn ? L"B" : NULL, L"CANCEL"}};
    const bool capturing = sCapturing >= 0;
    const Hint* pHints = capturing ? kCapture : kNavigate;
    const s32 count = capturing ? 1 : 2;
    const s16 font = gFrontendNormalFont;
    const s16 buttonFont = gFrontendHeadingFont;
    // Between the frontend's two logos (x 70 to 550): longer translations start further left, then
    // come closer together.
    const f32 kLeft = 70;
    const f32 kRight = 550;
    const f32 kDisc = 34;
    f32 width = 0;
    for (s32 i = 0; i < count; i++)
    {
        const bool button = pad && pHints[i].mButton;
        width += (button ? kDisc : Width(Translate(pHints[i].mKey), font) + 10) + Width(Translate(pHints[i].mLabel), font);
    }
    f32 gap = 40;
    f32 x = kRight - width - gap * (count - 1);
    x = x > 120 ? 120 : (x < kLeft ? kLeft : x);
    if (count > 1 && x + width + gap > kRight)
    {
        gap = kRight - x - width > 10 ? kRight - x - width : 10;
    }

    const f32 y = 440;
    for (s32 i = 0; i < count; i++)
    {
        if (pad && pHints[i].mButton)
        {
            Sprite(kDiscSprite, x + 10, y + 12);
            Text(pHints[i].mButton, x + 10 - Width(pHints[i].mButton, buttonFont) / 2, y, buttonFont);
            x += kDisc;
        }
        else
        {
            const wchar_t* pKey = Translate(pHints[i].mKey);
            Text(pKey, x, y, font);
            x += Width(pKey, font) + 10;
        }
        const wchar_t* pLabel = Translate(pHints[i].mLabel);
        Text(pLabel, x, y, font);
        x += Width(pLabel, font) + gap;
    }
}
} // namespace

bool PadMode()
{
#if defined(GTA2_PORT_SDL)
    return port_PadMode();
#else
    return false;
#endif
}

void Open()
{
    sPage = kMainPage;
    sSelected[kMainPage] = 0;
    sCapturing = -1;
}

Result HandleInput(Input input)
{
    Item items[kMaxItems];
    const s32 count = Items(sPage, items);
    s32& selected = sSelected[sPage];
    const Item& item = items[selected];
    switch (input)
    {
        case kUp:
            selected = (selected + count - 1) % count;
            return kMovedUp;
        case kDown:
            selected = (selected + 1) % count;
            return kMovedDown;
        case kLeft:
        case kRight:
            if (item.mKind == kSetting)
            {
                PortSettings::Change(static_cast<PortSettings::Id>(item.mArg), input == kLeft ? -1 : 1);
                return input == kLeft ? kLowered : kRaised;
            }
#if defined(GTA2_PORT_SDL)
            if (item.mKind == kBinding)
            {
                sColumn = input == kLeft ? 0 : 1;
                return input == kLeft ? kLowered : kRaised;
            }
#endif
            return kNothing;
        case kSelect:
            switch (item.mKind)
            {
                case kGoTo:
                    GoTo(static_cast<Page>(item.mArg));
                    return kEntered;
                case kSetting:
                    PortSettings::Change(static_cast<PortSettings::Id>(item.mArg), 1);
                    return kRaised;
                case kBinding:
#if defined(GTA2_PORT_SDL)
                    port_CaptureStart();
#else
                    if (!gFrontend)
                    {
                        return kNothing;
                    }
                    memcpy(sPrevKeys, gFrontend->mKeys, sizeof(sPrevKeys));
#endif
                    sCapturing = item.mArg;
                    return kEntered;
                case kDefaultKeys:
                    DefaultKeys();
                    return kEntered;
                case kBack:
                    return Back();
                case kResume:
                    return kClosed;
                default:
                    return kQuitGame;
            }
        default:
            return Back();
    }
}

bool IsCapturing()
{
    return sCapturing >= 0;
}

Result UpdateCapture()
{
    if (sCapturing < 0)
    {
        return kNothing;
    }
    // Escape cancels; B cancels on the keyboard column, Back on the pad column.
#if defined(GTA2_PORT_SDL)
    const s32 key = port_CapturedKey();
    const s32 pad = port_CapturedPad();
#else
    const s32 key = FrontendKeyPressed();
    const s32 pad = -1;
#endif
    if (key == DIK_ESCAPE || (sColumn == 0 && pad == kPadB) || (sColumn == 1 && pad == kPadBack))
    {
        sCapturing = -1;
        return kWentBack;
    }
    const s32 binding = sColumn ? pad : key;
    if (binding < 0 || (sColumn == 0 && binding == 0))
    {
        return kNothing;
    }
    Bind(sColumn, sCapturing, binding);
    sCapturing = -1;
    return kEntered;
}

const wchar_t* Title()
{
    return Translate(kTitles[sPage]);
}

void Draw(f32 scale, f32 left, f32 top, bool panel)
{
    sScale = scale;
    sLeft = left;
    sTop = top;
    if (sKeyNames.empty())
    {
        LoadKeyNames();
    }
    Item items[kMaxItems];
    const s32 count = Items(sPage, items);
    const bool controls = sPage == kControlsPage || sPage == kControls2Page;
#if defined(GTA2_PORT_SDL)
    const bool padColumn = controls;
#else
    const bool padColumn = false;
#endif
    const wchar_t* pKeyboard = Translate(L"KEYBOARD");
    const wchar_t* pGamepad = Translate(L"GAMEPAD");

    // The columns at their places, or further right for longer texts, a gap after the longest.
    const s16 normal = gFrontendNormalFont;
    f32 nameWidth = 0;
    f32 valueWidth = controls ? Width(pKeyboard, normal) : 0;
    f32 padWidth = padColumn ? Width(pGamepad, normal) : 0;
    wchar_t value[24];
    wchar_t pad[24];
    for (s32 i = 0; i < count; i++)
    {
        if (items[i].mKind == kSetting || items[i].mKind == kBinding)
        {
            ItemValues(items[i], value, pad);
            nameWidth = Max(nameWidth, Width(ItemName(items[i]), normal));
            valueWidth = Max(valueWidth, Width(value, normal));
            padWidth = Max(padWidth, Width(pad, normal));
        }
    }
    const f32 kGap = 24;
    f32 x = controls ? 60.0f : 120.0f;
    f32 valueX = Max(controls ? 300.0f : 380.0f, x + nameWidth + kGap);
    f32 padX = Max(470, valueX + valueWidth + kGap);
    f32 right = padColumn ? padX + padWidth : valueX + valueWidth;
    // Inside the frontend's screen: the page moves left, down to x 20.
    f32 shift = right - 630;
    shift = shift < 0 ? 0 : (shift > x - 20 ? x - 20 : shift);
    x -= shift;
    valueX -= shift;
    padX -= shift;
    right -= shift;

    if (panel)
    {
        Panel(x < 60 ? x - 20 : 40, 130, Max(600, right + 20), 470);
    }
    if (controls)
    {
        Text(pKeyboard, valueX, 140, normal);
        if (padColumn)
        {
            Text(pGamepad, padX, 140, normal);
        }
    }
    f32 y = 160;
    for (s32 i = 0; i < count; i++)
    {
        const Item& item = items[i];
        if (i)
        {
            y += item.mGap ? 34 : 22;
        }
        const bool selected = i == sSelected[sPage];
        const s16 font = selected ? gFrontendSelectedFont : normal;
        ItemValues(item, value, pad);
        Text(ItemName(item), x, y, font);
        Text(value, valueX, y, font);
        if (padColumn && item.mKind == kBinding)
        {
            Text(pad, padX, y, font);
            if (selected)
            {
                Sprite(kArrowSprite, (sColumn ? padX : valueX) - 12, y + 6);
            }
        }
    }
    DrawHints();
}
} // namespace PortMenu
