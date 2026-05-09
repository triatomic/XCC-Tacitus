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
