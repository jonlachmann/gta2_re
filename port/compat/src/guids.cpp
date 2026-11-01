// Definitions of the DirectX GUIDs the game references.
#include <windows.h>
#include <initguid.h>
#define DIRECTINPUT_VERSION 0x0700
#include <ddraw.h>
#include <dinput.h>
#include <dplay.h>
#include <dplobby.h>

// <windows.h> already included unknwn.h without initguid.h.
extern "C" const IID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
