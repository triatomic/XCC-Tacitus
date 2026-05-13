#pragma once

// Undocumented Application Hooks (UAH) menu-bar messages. Ported from
// Notepad++'s DarkMode/UAHMenuBar.h (MIT, (c) 2021 adzm / Adam D. Walling).
// Lets a window draw its menu-bar strip itself instead of accepting the
// system's light-themed default. The popups themselves are still drawn by
// Windows — UAH only covers the strip behind File/View/...

#define WM_UAHDESTROYWINDOW    0x0090 // handled by DefWindowProc
#define WM_UAHDRAWMENU         0x0091 // lParam = UAHMENU*
#define WM_UAHDRAWMENUITEM     0x0092 // lParam = UAHDRAWMENUITEM*
#define WM_UAHINITMENU         0x0093 // handled by DefWindowProc
#define WM_UAHMEASUREMENUITEM  0x0094 // lParam = UAHMEASUREMENUITEM*
#define WM_UAHNCPAINTMENUPOPUP 0x0095 // handled by DefWindowProc

typedef union tagUAHMENUITEMMETRICS
{
	struct { DWORD cx; DWORD cy; } rgsizeBar[2];
	struct { DWORD cx; DWORD cy; } rgsizePopup[4];
} UAHMENUITEMMETRICS;

typedef struct tagUAHMENUPOPUPMETRICS
{
	DWORD rgcx[4];
	DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;

typedef struct tagUAHMENU
{
	HMENU hmenu;
	HDC hdc;
	DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM
{
	int iPosition; // 0-based position of menu item in menubar
	UAHMENUITEMMETRICS umim;
	UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct tagUAHDRAWMENUITEM
{
	DRAWITEMSTRUCT dis; // itemID is uninitialized; use umi.iPosition
	UAHMENU um;
	UAHMENUITEM umi;
} UAHDRAWMENUITEM;

typedef struct tagUAHMEASUREMENUITEM
{
	MEASUREITEMSTRUCT mis;
	UAHMENU um;
	UAHMENUITEM umi;
} UAHMEASUREMENUITEM;
