#include "stdafx.h"
#include "XCC Mixer.h"

#include "MainFrm.h"
#include "XCC MixerDoc.h"
#include "XCC MixerView.h"

#include <gdiplus.h>
#include <id_log.h>
#include "mix_cache.h"
#include "theme.h"
#include "xcc_dirs.h"
#include "xcc_log.h"

BEGIN_MESSAGE_MAP(CXCCMixerApp, CWinApp)
	//{{AFX_MSG_MAP(CXCCMixerApp)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

CXCCMixerApp theApp;

BOOL CXCCMixerApp::InitInstance()
{
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	AfxEnableControlContainer();
	SetRegistryKey("XCC");
	LoadStdProfileSettings(0);
	theme::load();
	theme::apply_app_mode();
	Cmix_file::enable_ft_support();
	xcc_dirs::load_from_registry();
	xcc_log::attach_file("XCC Mixer log.txt");
	if (mix_database::load())
	{
		xcc_dirs::reset_data_dir();
		mix_database::load();
	}
	mix_cache::load();

	CSingleDocTemplate* pDocTemplate;
	pDocTemplate = new CSingleDocTemplate(
		IDR_MAINFRAME,
		RUNTIME_CLASS(CXCCMixerDoc),
		RUNTIME_CLASS(CMainFrame),       // main SDI frame window
		RUNTIME_CLASS(CXCCMixerView));
	AddDocTemplate(pDocTemplate);

	EnableShellOpen();
	RegisterShellFileTypes();

	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	if (!ProcessShellCommand(cmdInfo))
		return FALSE;
	m_pMainWnd->ShowWindow(SW_SHOWMAXIMIZED);
	m_pMainWnd->UpdateWindow();

	return TRUE;
}

int CXCCMixerApp::ExitInstance() 
{
	mix_cache::save();
	xcc_dirs::save_to_registry();
	return CWinApp::ExitInstance();
}

BOOL CXCCMixerApp::OnIdle(LONG lCount) 
{
	return static_cast<CMainFrame*>(GetMainWnd())->OnIdle(lCount) || CWinApp::OnIdle(lCount);
}
