#include "stdafx.h"
#include "XCC Mixer.h"

#include "MainFrm.h"
#include "XCC MixerDoc.h"
#include "XCC MixerView.h"

#include <gdiplus.h>
#include <shlobj.h>
#include <id_log.h>
#include "ListCtrlEx.h"
#include "mix_cache.h"
#include "theme.h"
#include "xcc_dirs.h"
#include "xcc_log.h"

// Bridge between the Library's CListCtrlEx custom-draw and the Mixer's theme
// module. Installed once at startup so every CListCtrlEx instance — across
// all Mixer dialogs — pulls dark-mode row colors from the same source.
static bool listctrl_is_dark() { return theme::is_dark(); }
static COLORREF listctrl_row_bg() { return theme::bg(); }
static COLORREF listctrl_row_bg_alt() { return theme::bg_alt(); }
static COLORREF listctrl_text() { return theme::text(); }
static COLORREF listctrl_grid() { return theme::border(); }
static bool listctrl_show_grid() { return theme::show_grid(); }
static const CListCtrlEx_theme g_listctrl_theme_hook = {
	listctrl_is_dark,
	listctrl_row_bg,
	listctrl_row_bg_alt,
	listctrl_text,
	listctrl_grid,
	listctrl_show_grid,
};

// ETSLayoutDialog repaints its own non-child background in OnEraseBkgnd,
// bypassing WM_CTLCOLORDLG. Hand it a dark brush in dark mode so the
// dialog body stops painting light.
extern HBRUSH (*ETSLayout_theme_brush)();
static HBRUSH ets_dark_brush()
{
	return theme::is_dark() ? theme::bg_brush() : NULL;
}

BEGIN_MESSAGE_MAP(CXCCMixerApp, CWinApp)
	//{{AFX_MSG_MAP(CXCCMixerApp)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

CXCCMixerApp theApp;

// Tracks which source provided the mix-name database for this session.
// Read by CMainFrame to suffix the window title so the user can see at a
// glance whether names came from <data dir>\global mix database.dat (any
// user-edited names included) or from the dat baked into the exe as a
// fallback. mix_db_source_none means lookup will always miss and rows
// will display 8-hex IDs.
e_mix_db_source g_mix_db_source = mix_db_source_none;

// ---------- Settings directory pointer ----------
//
// Lives in HKCU outside the INI itself (chicken-and-egg: we need to know
// where the INI lives before we can read anything from it). One small string
// value; default empty = AppData.
namespace
{
	const char* k_settings_ptr_key = "Software\\XCC\\Mixer";
	const char* k_settings_ptr_val = "settings_dir";

	std::string read_settings_dir_pointer()
	{
		HKEY hk = NULL;
		if (::RegOpenKeyExA(HKEY_CURRENT_USER, k_settings_ptr_key, 0,
				KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
			return "";
		char buf[64] = {0};
		DWORD cb = sizeof(buf) - 1, type = 0;
		std::string out;
		if (::RegQueryValueExA(hk, k_settings_ptr_val, NULL, &type,
				reinterpret_cast<BYTE*>(buf), &cb) == ERROR_SUCCESS
			&& type == REG_SZ)
		{
			out = buf;
		}
		::RegCloseKey(hk);
		return out;
	}

	std::string appdata_settings_path()
	{
		char appdata[MAX_PATH] = {0};
		::SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
		std::string dir = std::string(appdata) + "\\XCC\\Mixer";
		::SHCreateDirectoryExA(NULL, dir.c_str(), NULL);
		return dir + "\\settings.ini";
	}

	std::string exe_settings_path()
	{
		char exe[MAX_PATH] = {0};
		::GetModuleFileNameA(NULL, exe, MAX_PATH);
		std::string p = exe;
		auto slash = p.find_last_of("\\/");
		std::string dir = (slash == std::string::npos) ? "." : p.substr(0, slash);
		return dir + "\\settings.ini";
	}
}

e_settings_dir settings_dir_get()
{
	return read_settings_dir_pointer() == "exe" ? settings_exe : settings_appdata;
}

void settings_dir_set(e_settings_dir v)
{
	HKEY hk = NULL;
	DWORD disp = 0;
	if (::RegCreateKeyExA(HKEY_CURRENT_USER, k_settings_ptr_key, 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, &disp) != ERROR_SUCCESS)
		return;
	const char* s = (v == settings_exe) ? "exe" : "appdata";
	::RegSetValueExA(hk, k_settings_ptr_val, 0, REG_SZ,
		reinterpret_cast<const BYTE*>(s),
		static_cast<DWORD>(strlen(s) + 1));
	::RegCloseKey(hk);
}

std::string settings_dir_path(e_settings_dir v)
{
	return v == settings_exe ? exe_settings_path() : appdata_settings_path();
}

BOOL CXCCMixerApp::InitInstance()
{
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	AfxEnableControlContainer();
	// Settings live in an INI file, not the registry. Path is selected by the
	// HKCU pointer key (see settings_dir_get) — either %APPDATA%\XCC\Mixer\
	// settings.ini (default) or <exe folder>\settings.ini. MFC's profile API
	// routes to GetPrivateProfile* when m_pszRegistryKey is null and
	// m_pszProfileName holds an absolute path, so every existing call site
	// (theme, PalPaths, MainFrame window placement, keybinds, etc.) lands in
	// the chosen INI without further changes.
	{
		std::string ini = settings_dir_path(settings_dir_get());
		// CWinApp owns m_pszProfileName via free(); allocate with malloc to match.
		if (m_pszProfileName)
			free((void*)m_pszProfileName);
		m_pszProfileName = _strdup(ini.c_str());
	}
	LoadStdProfileSettings(0);
	theme::load();
	theme::apply_app_mode();
	CListCtrlEx_set_theme(&g_listctrl_theme_hook);
	ETSLayout_theme_brush = &ets_dark_brush;
	Cmix_file::enable_ft_support();
	xcc_dirs::load_from_registry();
	xcc_log::attach_file("XCC Mixer log.txt");
	g_mix_db_source = mix_db_source_none;
	if (mix_database::load())
	{
		xcc_dirs::reset_data_dir();
		if (mix_database::load())
		{
			// All on-disk attempts failed. Fall back to the dat blob baked
			// into the exe as RCDATA so the listview still shows filenames
			// instead of 8-hex-digit IDs.
			HRSRC res = ::FindResource(NULL, "GLOBAL_MIX_DATABASE", RT_RCDATA);
			if (res)
			{
				HGLOBAL g = ::LoadResource(NULL, res);
				DWORD sz = ::SizeofResource(NULL, res);
				if (g && sz)
				{
					if (mix_database::load_from_buffer(::LockResource(g), static_cast<int>(sz)) == 0)
						g_mix_db_source = mix_db_source_embedded;
				}
			}
		}
		else
			g_mix_db_source = mix_db_source_on_disk;
	}
	else
		g_mix_db_source = mix_db_source_on_disk;
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

	// Restore the window placement saved by CMainFrame::OnDestroy. First run
	// has no saved values (sentinel INT_MIN on win_left) and falls back to the
	// historical "start maximized" behavior. Otherwise SetWindowPlacement
	// gives back the exact restored-state rect, and showCmd controls whether
	// to come up maximized or normal.
	const int sentinel = INT_MIN;
	int left = GetProfileInt("MainFrame", "win_left", sentinel);
	if (left == sentinel)
	{
		m_pMainWnd->ShowWindow(SW_SHOWMAXIMIZED);
	}
	else
	{
		WINDOWPLACEMENT wp = {};
		wp.length = sizeof(wp);
		wp.rcNormalPosition.left   = left;
		wp.rcNormalPosition.top    = GetProfileInt("MainFrame", "win_top",    0);
		wp.rcNormalPosition.right  = GetProfileInt("MainFrame", "win_right",  left + 800);
		wp.rcNormalPosition.bottom = GetProfileInt("MainFrame", "win_bottom", 600);
		wp.showCmd = GetProfileInt("MainFrame", "win_maximized", 0)
			? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
		m_pMainWnd->SetWindowPlacement(&wp);
		m_pMainWnd->ShowWindow(wp.showCmd);
	}
	m_pMainWnd->UpdateWindow();

	return TRUE;
}

// Defined in XCC MixerView.cpp; deletes any temp files written by the
// "Use external programs" double-click flow.
namespace ext_open { void cleanup(); }

int CXCCMixerApp::ExitInstance()
{
	mix_cache::save();
	xcc_dirs::save_to_registry();
	ext_open::cleanup();
	return CWinApp::ExitInstance();
}

BOOL CXCCMixerApp::OnIdle(LONG lCount) 
{
	return static_cast<CMainFrame*>(GetMainWnd())->OnIdle(lCount) || CWinApp::OnIdle(lCount);
}
