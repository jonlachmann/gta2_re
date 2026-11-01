// The list view and track bar messages of the network lobby, which the port does not show.
#pragma once

#include "windows.h"

#define LVM_FIRST 0x1000
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_INSERTITEMA (LVM_FIRST + 7)
#define LVM_DELETEITEM (LVM_FIRST + 8)
#define LVM_DELETEALLITEMS (LVM_FIRST + 9)
#define LVM_GETNEXTITEM (LVM_FIRST + 12)
#define LVM_FINDITEMA (LVM_FIRST + 13)
#define LVM_GETITEMTEXTA (LVM_FIRST + 45)
#define LVM_SETITEMTEXTA (LVM_FIRST + 46)
#define LVIF_TEXT 0x0001
#define LVFI_STRING 0x0002
#define LVNI_SELECTED 0x0002
#define TBM_GETPOS (WM_USER)
#define TBM_SETRANGE (WM_USER + 6)

typedef struct tagLVITEMA
{
    UINT mask;
    INT iItem;
    INT iSubItem;
    UINT state;
    UINT stateMask;
    LPSTR pszText;
    INT cchTextMax;
    INT iImage;
    LPARAM lParam;
    INT iIndent;
} LVITEMA;

typedef struct tagLVFINDINFOA
{
    UINT flags;
    LPCSTR psz;
    LPARAM lParam;
    POINT pt;
    UINT vkDirection;
} LVFINDINFOA;
