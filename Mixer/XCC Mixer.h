#pragma once

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

using namespace std;

// Where this session's mix-name database came from. Set during
// InitInstance and read by CMainFrame to annotate the window title.
enum e_mix_db_source
{
	mix_db_source_none,      // every load attempt failed (defensive)
	mix_db_source_on_disk,   // <data dir>\global mix database.dat
	mix_db_source_embedded,  // baked-in RCDATA fallback
};
extern e_mix_db_source g_mix_db_source;

// Where MFC's profile API (GetProfileInt/WriteProfileInt etc.) reads and
// writes settings. Selected at startup from the HKCU pointer key
// HKCU\Software\XCC\Mixer\settings_dir, falling back to settings_appdata.
// The choice is exposed to the user via Keybinds → Settings Directory.
enum e_settings_dir
{
	settings_appdata = 0, // %APPDATA%\XCC\Mixer\settings.ini (default)
	settings_exe = 1,     // <exe folder>\settings.ini
};
e_settings_dir settings_dir_get();
// Persist a new choice to the HKCU pointer key. The new location takes
// effect on next launch; current session keeps using the old file.
void settings_dir_set(e_settings_dir v);
// Absolute path to the settings.ini for a given choice. Used by the menu
// handler to check "does a settings file already exist at the destination?"
// before flipping the pointer.
std::string settings_dir_path(e_settings_dir v);

class CXCCMixerApp : public CWinApp
{
public:
	//{{AFX_VIRTUAL(CXCCMixerApp)
	public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	virtual BOOL OnIdle(LONG lCount);
	//}}AFX_VIRTUAL

	//{{AFX_MSG(CXCCMixerApp)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
