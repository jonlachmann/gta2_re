// Port/net (docs/netcode.md, "Joining from the frontend"): a network page in the game's frontend. The main menu gets
// "Network game" (between options and quit); its page takes a server's address and a name, and joins in the
// background (NetMenu_Join, NetMenu_Poll: the menu runs on meanwhile). Once the server welcomes us the network game is
// set up as the command line's --connect does, and the frontend boots it as it boots a level (Booting_Map_2), with
// the game's own network start filling in the session (Start_NetworkGame_5E5A30). The page is one of the frontend's
// unused ones (12); the game draws it, this file takes its input (gPortFrontendInput) and writes its labels. The
// address and the name are kept in gta2.ini, beside the game's own network settings.
#include "net_main.hpp"
#include "Port/port_hooks.hpp"
#include "Port/registry_ini.hpp"
#include "Source/Frontend.hpp"
#include "Source/SoundObject.hpp"
#include "Source/winmain.hpp"
#include <ctype.h>
#include <dinput.h>
#include <string>
#include <string.h>

EXTERN_GLOBAL(HINSTANCE, gHInstance_708220);
EXTERN_GLOBAL(SoundObject, snd1_67D818); // the frontend's sound: 1 up, 2 down, 5 enter, 6 back, 7 a key, 8 a deletion


namespace
{
const u16 kPage = MENUPAGE_12;
const u16 kNoTarget = 268; // an ENTERABLE option's target the game does nothing for (UpdatePageFromUserInput_4AE2D0)
enum
{
    kAddress,
    kName,
    kJoin,
    kOptions
};

std::string gAddress = "127.0.0.1", gName, gStatus;
int gEditing = -1; // the field being typed into (kAddress, kName), or -1
std::string gBefore; // the field as it was: Esc gives it back
bool gJoining;
unsigned gBlink;
char gPrevKeys[256];

const char* const kSettings = "SOFTWARE\\DMA Design Ltd\\GTA2\\Network";

std::string Load(const char* name)
{
    HKEY key;
    char text[64] = {};
    DWORD size = sizeof(text) - 1;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, kSettings, 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS)
    {
        return "";
    }
    if (RegQueryValueExA(key, name, 0, 0, (LPBYTE)text, &size) != ERROR_SUCCESS)
    {
        text[0] = 0;
    }
    RegCloseKey(key);
    return text;
}

void Save()
{
    HKEY key;
    DWORD disposition;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, kSettings, 0, 0, 0, KEY_ALL_ACCESS, 0, &key, &disposition) != ERROR_SUCCESS)
    {
        return;
    }
    RegSetValueExA(key, "ServerAddress", 0, REG_SZ, (const BYTE*)gAddress.c_str(), (DWORD)gAddress.size() + 1);
    RegSetValueExA(key, "PlayerName", 0, REG_SZ, (const BYTE*)gName.c_str(), (DWORD)gName.size() + 1); // the game's own
    RegCloseKey(key);
}

// Into one of the frontend's 50-character labels. By hand, not with the C library's wide functions or
// strings: the game's wchar_t is 2 bytes (-fshort-wchar), the library's 4
void SetText(wchar_t* out, const std::string& text)
{
    size_t n = 0;
    for (; n < text.size() && n < 49; n++)
    {
        out[n] = (wchar_t)(unsigned char)toupper((unsigned char)text[n]);
    }
    out[n] = 0;
}

// The characters a key types: an address's and a name's (the game's own keyboard table has no shift, and the name
// entry drops what its font has no width for); uppercase, as the frontend's fonts draw them
char KeyChar(int key, bool shift)
{
    static const char* kRow1 = "QWERTYUIOP"; // DIK_Q..DIK_P
    static const char* kRow2 = "ASDFGHJKL"; // DIK_A..DIK_L
    static const char* kRow3 = "ZXCVBNM"; // DIK_Z..DIK_M
    if (key >= DIK_1 && key <= DIK_9)
    {
        return (char)('1' + key - DIK_1);
    }
    switch (key)
    {
        case DIK_0:
        case DIK_NUMPAD0:
            return '0';
        case DIK_NUMPAD1: return '1';
        case DIK_NUMPAD2: return '2';
        case DIK_NUMPAD3: return '3';
        case DIK_NUMPAD4: return '4';
        case DIK_NUMPAD5: return '5';
        case DIK_NUMPAD6: return '6';
        case DIK_NUMPAD7: return '7';
        case DIK_NUMPAD8: return '8';
        case DIK_NUMPAD9: return '9';
        case DIK_DECIMAL: return '.';
        case DIK_PERIOD: return shift ? ':' : '.'; // ':' is shift and '.' on many layouts
        case DIK_SEMICOLON: return ':'; // and shift and ';' on a US one
        case DIK_MINUS: return shift ? '_' : '-';
    }
    if (key >= DIK_Q && key <= DIK_P)
    {
        return kRow1[key - DIK_Q];
    }
    if (key >= DIK_A && key <= DIK_L)
    {
        return kRow2[key - DIK_A];
    }
    if (key >= DIK_Z && key <= DIK_M)
    {
        return kRow3[key - DIK_Z];
    }
    return 0;
}

std::string& Field(int f)
{
    return f == kAddress ? gAddress : gName;
}

// A key pressed this frame into the field being typed into; Enter keeps it, Esc gives back what it was
void Type(Frontend* pFrontend, const bool* pressed)
{
    std::string& text = Field(gEditing);
    const size_t maxLength = gEditing == kAddress ? 40 : 15;
    const bool shift = (pFrontend->field_8_keys[DIK_LSHIFT] & 0x80) || (pFrontend->field_8_keys[DIK_RSHIFT] & 0x80);
    for (int k = 0; k < 256; k++)
    {
        if (!pressed[k])
        {
            continue;
        }
        if (k == DIK_RETURN || k == DIK_NUMPADENTER)
        {
            gEditing = -1;
            Save();
            snd1_67D818.field_0_object_type = 5;
            return;
        }
        if (k == DIK_ESCAPE)
        {
            text = gBefore;
            gEditing = -1;
            snd1_67D818.field_0_object_type = 6;
            return;
        }
        if (k == DIK_BACK)
        {
            if (!text.empty())
            {
                text.erase(text.size() - 1);
                snd1_67D818.field_0_object_type = 8;
            }
            continue;
        }
        const char c = KeyChar(k, shift);
        const bool allowed = gEditing == kAddress ? c && c != '_' : c && c != ':' && c != '.';
        if (allowed && text.size() < maxLength)
        {
            text += c;
            snd1_67D818.field_0_object_type = 7;
        }
    }
}

// The page's labels for this frame: the fields' text (and the cursor where one is typed into), the join's state
void Refresh(Frontend* pFrontend)
{
    MenuPage& page = pFrontend->field_136_menu_pages_array[kPage];
    const bool cursor = (++gBlink / 8) % 2;
    SetText(page.field_4_options_array[kAddress].field_6_option_name_str, "Address   " + gAddress + (gEditing == kAddress && cursor ? "_" : ""));
    SetText(page.field_4_options_array[kName].field_6_option_name_str, "Name   " + gName + (gEditing == kName && cursor ? "_" : ""));
    SetText(page.field_4_options_array[kJoin].field_6_option_name_str, gJoining ? "Connecting..." : "Join game");
    SetText(page.field_518_elements_array[0].field_6_element_name_str, gStatus);
}

// The server welcomed us: the game's network start (as -N does before the frontend), then the frontend boots it
void StartGame(Frontend* pFrontend)
{
    bStartNetworkGame_7081F0 = 1;
    if (!Start_NetworkGame_5E5A30(gHInstance_708220))
    {
        bStartNetworkGame_7081F0 = 0;
        NetMenu_Cancel();
        gStatus = "the network game didn't start";
        return;
    }
    pFrontend->field_EE08_menu_screen = RedBar_16;
    pFrontend->field_110_state = FrontendState::Booting_Map_2;
}

void Setup(Frontend* pFrontend)
{
    // The main menu: "Network game" after options, quit moved down a line
    MenuPage& main = pFrontend->field_136_menu_pages_array[MENUPAGE_START_MENU];
    main.field_4_options_array[3] = main.field_4_options_array[2];
    main.field_4_options_array[3].field_4_y_pos += 20;
    main.field_B8A_option_states[3] = main.field_B8A_option_states[2];
    MenuItem& item = main.field_4_options_array[2];
    SetText(item.field_6_option_name_str, "Network game");
    item.field_80_menu_page_target = kPage;
    main.field_0_number_of_options = 4;

    MenuPage& page = pFrontend->field_136_menu_pages_array[kPage];
    page.field_0_number_of_options = kOptions;
    // Where the main menu's and the play page's are: right of the picture, on the black
    const s16 ys[kOptions] = {250, 270, 310};
    for (int i = 0; i < kOptions; i++)
    {
        page.field_4_options_array[i].field_0_option_type = ENTERABLE_1;
        page.field_4_options_array[i].field_2_x_pos = 300;
        page.field_4_options_array[i].field_4_y_pos = ys[i];
        page.field_4_options_array[i].field_80_menu_page_target = kNoTarget;
    }
    page.field_BC6_current_option_idx = kJoin;
    page.field_BC8_default_option_idx = kJoin;
    page.field_2_number_of_elements = 1;
    MenuElement& status = page.field_518_elements_array[0];
    status.field_0_element_type = STRING_TEXT_1;
    status.field_2_xpos = 300;
    status.field_4_ypos = 350;
    status.field_6A_font_type = pFrontend->field_11C_normal_font;
    status.field_6C_font_palette = 0xFFFF;
    static bool loaded;
    if (!loaded)
    {
        loaded = true;
        const std::string address = Load("ServerAddress");
        gAddress = address.empty() ? gAddress : address;
        gName = NetMenu_DefaultName().empty() ? Load("PlayerName") : NetMenu_DefaultName();
    }
    gName = gName.empty() ? "PLAYER" : gName;
    for (char& c : gName)
    {
        c = (char)toupper((unsigned char)c);
    }
    Refresh(pFrontend);
}

bool Input(Frontend* pFrontend)
{
    if (pFrontend->field_132_current_page_idx == MENUPAGE_START_MENU)
    {
        // The picture beside the main menu (UpdateMenuScreen_4B6780 knows three options): ours shows the options' one
        const u16 option = pFrontend->field_136_menu_pages_array[MENUPAGE_START_MENU].field_BC6_current_option_idx;
        pFrontend->field_EE08_menu_screen = option == 2 ? Options_0 : option == 3 ? Quit_2 : pFrontend->field_EE08_menu_screen;
        if (option == 2 && pFrontend->field_C9D0_return_pressed)
        {
            // Ours too: the game's page change draws a level's picture for a frame (UpdateMenuScreen_4B6780)
            pFrontend->ChangeMenuPage_4B3170(kPage);
            pFrontend->field_EE08_menu_screen = Options_0;
            snd1_67D818.field_0_object_type = 5;
            return true;
        }
        return false;
    }
    if (pFrontend->field_132_current_page_idx != kPage)
    {
        return false;
    }
    pFrontend->field_EE08_menu_screen = Options_0; // the main menu's picture for "Network game" (the game's: a level's)
    bool pressed[256];
    for (int k = 0; k < 256; k++)
    {
        const bool down = (pFrontend->field_8_keys[k] & 0x80) != 0;
        pressed[k] = down && !(gPrevKeys[k] & 0x80);
        gPrevKeys[k] = pFrontend->field_8_keys[k];
    }
    MenuPage& page = pFrontend->field_136_menu_pages_array[kPage];
    bool handled = true;
    if (gJoining)
    {
        std::string error;
        const int result = NetMenu_Poll(&error);
        if (result > 0)
        {
            gJoining = false;
            gStatus = "";
            StartGame(pFrontend);
        }
        else if (result < 0)
        {
            gJoining = false;
            gStatus = error;
        }
        else if (pFrontend->field_C9D1_escape_pressed)
        {
            NetMenu_Cancel();
            gJoining = false;
            gStatus = "cancelled";
            snd1_67D818.field_0_object_type = 6;
        }
    }
    else if (gEditing >= 0)
    {
        Type(pFrontend, pressed);
    }
    else if (pFrontend->field_C9D0_return_pressed)
    {
        const u16 option = page.field_BC6_current_option_idx;
        if (option == kAddress || option == kName)
        {
            gEditing = option;
            gBefore = Field(option);
        }
        else if (gAddress.empty() || gName.empty())
        {
            gStatus = gAddress.empty() ? "type the server's address" : "type a name";
        }
        else
        {
            std::string error;
            Save();
            gJoining = NetMenu_Join(gAddress, gName, &error);
            gStatus = gJoining ? "" : error;
        }
        snd1_67D818.field_0_object_type = 5;
    }
    else if (pFrontend->field_C9D1_escape_pressed)
    {
        pFrontend->ChangeMenuPage_4B3170(MENUPAGE_START_MENU);
        pFrontend->field_EE08_menu_screen = Options_0; // "Network game" still chosen there
        snd1_67D818.field_0_object_type = 6;
    }
    else
    {
        handled = false; // up and down: the game's
    }
    Refresh(pFrontend);
    return handled;
}
} // namespace

void NetMenu_SetStatus(const std::string& status)
{
    gStatus = status;
}

int NetMenu_Page()
{
    return kPage;
}

void NetMenu_Install()
{
    gPortFrontendSetup = Setup;
    gPortFrontendInput = Input;
}
