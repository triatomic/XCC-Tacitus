#include "stdafx.h"
#include "XCC Mixer.h"
#include "MainFrm.h"
#include "XCCFileView.h"
#include "XSE_dlg.h"
#include "XSTE_dlg.h"
#include <fstream>
#include "aud_file.h"
#include "directoriesdlg.h"
#include "fname.h"
#include "searchfiledlg.h"
#include "SearchInPaneDlg.h"
#include "PalPathsDlg.h"
#include "KeybindsDlg.h"
#include "keybinds.h"
#include "SelectPaletteDlg.h"
#include "VxlLightingDlg.h"
#include "string_conversion.h"
#include "theme.h"
#include "theme_ts_ini_reader.h"
#include "wav_file.h"
#include "xcc_dirs.h"
#include "xcc_log.h"
#include "xste.h"

// Custom-fps prompt: tiny modal with a single edit field.
namespace {
	class CFpsCustomDlg : public CDialog
	{
	public:
		CFpsCustomDlg(int initial, CWnd* parent)
			: CDialog(IDD_FPS_CUSTOM, parent), m_value(initial) {}
		int m_value;
	protected:
		virtual void DoDataExchange(CDataExchange* pDX) override
		{
			CDialog::DoDataExchange(pDX);
			DDX_Text(pDX, IDC_FPS_CUSTOM_EDIT, m_value);
			DDV_MinMaxInt(pDX, m_value, 1, 9999);
		}
		virtual BOOL OnInitDialog() override
		{
			CDialog::OnInitDialog();
			theme::apply_dialog(GetSafeHwnd());
			// Pre-select the edit text so the user can type a replacement.
			if (CEdit* e = (CEdit*)GetDlgItem(IDC_FPS_CUSTOM_EDIT))
			{
				e->SetFocus();
				e->SetSel(0, -1);
			}
			return FALSE;	// FALSE = we manually set focus above
		}
		afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
		{
			if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(),
				pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
				return br;
			return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
		}
		DECLARE_MESSAGE_MAP()
	};
	BEGIN_MESSAGE_MAP(CFpsCustomDlg, CDialog)
		ON_WM_CTLCOLOR()
	END_MESSAGE_MAP()
}

int prompt_fps_value(CWnd* parent, int initial)
{
	CFpsCustomDlg dlg(initial, parent);
	if (dlg.DoModal() != IDOK) return -1;
	return dlg.m_value;
}

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
	ON_COMMAND_RANGE(ID_VIEW_PALETTE_PAL000, ID_VIEW_PALETTE_PAL999, OnViewPalette)
	ON_UPDATE_COMMAND_UI_RANGE(ID_VIEW_PALETTE_PAL000, ID_VIEW_PALETTE_PAL999, OnUpdateViewPalette)
	ON_WM_CREATE()
	ON_COMMAND(ID_VIEW_GAME_TD, OnViewGameTD)
	ON_COMMAND(ID_VIEW_GAME_RA, OnViewGameRA)
	ON_COMMAND(ID_VIEW_GAME_TS, OnViewGameTS)
	ON_COMMAND(ID_VIEW_GAME_RA2, OnViewGameRA2)
	ON_UPDATE_COMMAND_UI(ID_VIEW_GAME_TD, OnUpdateViewGameTD)
	ON_UPDATE_COMMAND_UI(ID_VIEW_GAME_RA, OnUpdateViewGameRA)
	ON_UPDATE_COMMAND_UI(ID_VIEW_GAME_TS, OnUpdateViewGameTS)
	ON_UPDATE_COMMAND_UI(ID_VIEW_GAME_RA2, OnUpdateViewGameRA2)
	ON_UPDATE_COMMAND_UI(ID_FILE_FOUND_UPDATE, OnUpdateFileFoundUpdate)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PALETTE_UPDATE, OnUpdateViewPaletteUpdate)
	ON_COMMAND(ID_VIEW_GAME_AUTO, OnViewGameAuto)
	ON_UPDATE_COMMAND_UI(ID_VIEW_GAME_AUTO, OnUpdateViewGameAuto)
	ON_COMMAND(ID_VIEW_PALETTE_AUTO, OnViewPaletteAuto)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PALETTE_AUTO, OnUpdateViewPaletteAuto)
	ON_COMMAND(ID_VIEW_PALETTE_PREV, OnViewPalettePrev)
	ON_COMMAND(ID_VIEW_PALETTE_NEXT, OnViewPaletteNext)
	ON_COMMAND(ID_VIEW_PALETTE_USE_FOR_CONVERSION, OnViewPaletteUseForConversion)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PALETTE_USE_FOR_CONVERSION, OnUpdateViewPaletteUseForConversion)
	ON_COMMAND(ID_VIEW_PALETTE_CONVERT_FROM_TD, OnViewPaletteConvertFromTD)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PALETTE_CONVERT_FROM_TD, OnUpdateViewPaletteConvertFromTD)
	ON_COMMAND(ID_VIEW_PALETTE_CONVERT_FROM_RA, OnViewPaletteConvertFromRA)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PALETTE_CONVERT_FROM_RA, OnUpdateViewPaletteConvertFromRA)
	ON_COMMAND(ID_VIEW_VOXEL_NORMAL, OnViewVoxelNormal)
	ON_UPDATE_COMMAND_UI(ID_VIEW_VOXEL_NORMAL, OnUpdateViewVoxelNormal)
	ON_COMMAND(ID_VIEW_VOXEL_SURFACE_NORMALS, OnViewVoxelSurfaceNormals)
	ON_UPDATE_COMMAND_UI(ID_VIEW_VOXEL_SURFACE_NORMALS, OnUpdateViewVoxelSurfaceNormals)
	ON_COMMAND(ID_VIEW_VOXEL_DEPTH_INFORMATION, OnViewVoxelDepthInformation)
	ON_UPDATE_COMMAND_UI(ID_VIEW_VOXEL_DEPTH_INFORMATION, OnUpdateViewVoxelDepthInformation)
	ON_COMMAND(ID_CONVERSION_SPLIT_SHADOWS, OnConversionSplitShadows)
	ON_UPDATE_COMMAND_UI(ID_CONVERSION_SPLIT_SHADOWS, OnUpdateConversionSplitShadows)
	ON_COMMAND(ID_VIEW_DIRECTORIES, OnViewDirectories)
	ON_COMMAND(ID_FILE_SEARCH, OnFileSearch)
	ON_COMMAND(ID_FILE_SEARCH_IN_MIX, OnFileSearchInMix)
	ON_COMMAND(ID_CONVERSION_ENABLE_COMPRESSION, OnConversionEnableCompression)
	ON_UPDATE_COMMAND_UI(ID_CONVERSION_ENABLE_COMPRESSION, OnUpdateConversionEnableCompression)
	ON_WM_DESTROY()
	ON_COMMAND(ID_LAUNCH_XTW_TS, OnLaunchXTW_TS)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XTW_TS, OnUpdateLaunchXTW_TS)
	ON_COMMAND(ID_LAUNCH_XTW_RA2, OnLaunchXTW_RA2)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XTW_RA2, OnUpdateLaunchXTW_RA2)
	ON_COMMAND(ID_CONVERSION_COMBINE_SHADOWS, OnConversionCombineShadows)
	ON_UPDATE_COMMAND_UI(ID_CONVERSION_COMBINE_SHADOWS, OnUpdateConversionCombineShadows)
	ON_COMMAND(ID_VIEW_REPORT, OnViewReport)
	ON_UPDATE_COMMAND_UI(ID_VIEW_REPORT, OnUpdateViewReport)
	ON_COMMAND(ID_LAUNCH_XSTE_RA2, OnLaunchXSTE_RA2)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XSTE_RA2, OnUpdateLaunchXSTE_RA2)
	ON_COMMAND(ID_LAUNCH_XSTE_RA2_YR, OnLaunchXSTE_RA2_YR)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XSTE_RA2_YR, OnUpdateLaunchXSTE_RA2_YR)
	ON_COMMAND(ID_LAUNCH_XTW_RA2_YR, OnLaunchXTW_RA2_YR)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XTW_RA2_YR, OnUpdateLaunchXTW_RA2_YR)
	ON_COMMAND(ID_VIEW_PALETTE_SELECT, OnViewPaletteSelect)
	ON_COMMAND(ID_VIEW_PALETTE_AUTO_SELECT, OnViewPaletteAutoSelect)
	ON_COMMAND(ID_VIEW_PALETTE_PREV_SIBLING, OnViewPalettePrevSibling)
	ON_COMMAND(ID_VIEW_PALETTE_NEXT_SIBLING, OnViewPaletteNextSibling)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PALETTE_AUTO_SELECT, OnUpdateViewPaletteAutoSelect)
	ON_COMMAND(ID_LAUNCH_XSTE_GR, OnLaunchXSTE_GR)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XSTE_GR, OnUpdateLaunchXSTE_GR)
	ON_COMMAND(ID_LAUNCH_XSTE_GR_ZH, OnLaunchXSTE_GR_ZH)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XSTE_GR_ZH, OnUpdateLaunchXSTE_GR_ZH)
	ON_COMMAND(ID_LAUNCH_XSTE_OPEN, OnLaunchXSTE_Open)
	ON_COMMAND(ID_CONVERSION_FIX_SHADOWS, OnConversionFixShadows)
	ON_UPDATE_COMMAND_UI(ID_CONVERSION_FIX_SHADOWS, OnUpdateConversionFixShadows)
	ON_COMMAND(ID_LAUNCH_XSE_OPEN, OnLaunchXSE_Open)
	ON_COMMAND(ID_LAUNCH_XSE_RA2, OnLaunchXSE_RA2)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XSE_RA2, OnUpdateLaunchXSE_RA2)
	ON_COMMAND(ID_LAUNCH_XSE_RA2_YR, OnLaunchXSE_RA2_YR)
	ON_UPDATE_COMMAND_UI(ID_LAUNCH_XSE_RA2_YR, OnUpdateLaunchXSE_RA2_YR)
	ON_COMMAND(ID_THEME_LIGHT, OnThemeLight)
	ON_COMMAND(ID_THEME_DARK, OnThemeDark)
	ON_COMMAND(ID_THEME_SYSTEM, OnThemeSystem)
	ON_UPDATE_COMMAND_UI(ID_THEME_SYSTEM, OnUpdateThemeSystem)
	ON_WM_SETTINGCHANGE()
	ON_COMMAND(ID_SETTINGS_DIR_APPDATA, OnSettingsDirAppData)
	ON_COMMAND(ID_SETTINGS_DIR_EXE, OnSettingsDirExe)
	ON_UPDATE_COMMAND_UI(ID_SETTINGS_DIR_APPDATA, OnUpdateSettingsDirAppData)
	ON_UPDATE_COMMAND_UI(ID_SETTINGS_DIR_EXE, OnUpdateSettingsDirExe)
	ON_COMMAND(ID_THEME_SHOW_GRID, OnThemeShowGrid)
	ON_COMMAND(ID_THEME_ALPHA_COLOR, OnThemeAlphaColor)
	ON_COMMAND(ID_THEME_SHP_TRANSPARENCY, OnThemeShpTransparency)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHP_TRANSPARENCY, OnUpdateThemeShpTransparency)
	ON_COMMAND(ID_THEME_USE_CHECKERBOARD, OnThemeUseCheckerboard)
	ON_UPDATE_COMMAND_UI(ID_THEME_USE_CHECKERBOARD, OnUpdateThemeUseCheckerboard)
	ON_COMMAND(ID_THEME_USE_EXTERNAL_PROGRAMS, OnThemeUseExternalPrograms)
	ON_UPDATE_COMMAND_UI(ID_THEME_USE_EXTERNAL_PROGRAMS, OnUpdateThemeUseExternalPrograms)
	ON_UPDATE_COMMAND_UI(ID_THEME_LIGHT, OnUpdateThemeLight)
	ON_UPDATE_COMMAND_UI(ID_THEME_DARK, OnUpdateThemeDark)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHOW_GRID, OnUpdateThemeShowGrid)
	ON_COMMAND(ID_THEME_INTERP_NEAREST,  OnThemeInterpNearest)
	ON_COMMAND(ID_THEME_INTERP_BILINEAR, OnThemeInterpBilinear)
	ON_COMMAND(ID_THEME_INTERP_BICUBIC,  OnThemeInterpBicubic)
	ON_COMMAND(ID_THEME_INTERP_LANCZOS,  OnThemeInterpLanczos)
	ON_UPDATE_COMMAND_UI(ID_THEME_INTERP_NEAREST,  OnUpdateThemeInterpNearest)
	ON_UPDATE_COMMAND_UI(ID_THEME_INTERP_BILINEAR, OnUpdateThemeInterpBilinear)
	ON_UPDATE_COMMAND_UI(ID_THEME_INTERP_BICUBIC,  OnUpdateThemeInterpBicubic)
	ON_UPDATE_COMMAND_UI(ID_THEME_INTERP_LANCZOS,  OnUpdateThemeInterpLanczos)
	ON_COMMAND(ID_THEME_SHARPEN_0,   OnThemeSharpen0)
	ON_COMMAND(ID_THEME_SHARPEN_25,  OnThemeSharpen25)
	ON_COMMAND(ID_THEME_SHARPEN_50,  OnThemeSharpen50)
	ON_COMMAND(ID_THEME_SHARPEN_75,  OnThemeSharpen75)
	ON_COMMAND(ID_THEME_SHARPEN_100, OnThemeSharpen100)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHARPEN_0,   OnUpdateThemeSharpen0)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHARPEN_25,  OnUpdateThemeSharpen25)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHARPEN_50,  OnUpdateThemeSharpen50)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHARPEN_75,  OnUpdateThemeSharpen75)
	ON_UPDATE_COMMAND_UI(ID_THEME_SHARPEN_100, OnUpdateThemeSharpen100)
	ON_COMMAND(ID_THEME_FPS_30,        OnThemeFps30)
	ON_COMMAND(ID_THEME_FPS_60,        OnThemeFps60)
	ON_COMMAND(ID_THEME_FPS_120,       OnThemeFps120)
	ON_COMMAND(ID_THEME_FPS_UNLIMITED, OnThemeFpsUnlimited)
	ON_COMMAND(ID_THEME_FPS_CUSTOM,    OnThemeFpsCustom)
	ON_UPDATE_COMMAND_UI(ID_THEME_FPS_30,        OnUpdateThemeFps30)
	ON_UPDATE_COMMAND_UI(ID_THEME_FPS_60,        OnUpdateThemeFps60)
	ON_UPDATE_COMMAND_UI(ID_THEME_FPS_120,       OnUpdateThemeFps120)
	ON_UPDATE_COMMAND_UI(ID_THEME_FPS_UNLIMITED, OnUpdateThemeFpsUnlimited)
	ON_UPDATE_COMMAND_UI(ID_THEME_FPS_CUSTOM,    OnUpdateThemeFpsCustom)
	ON_WM_MEASUREITEM()
	ON_WM_DRAWITEM()
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_USER + 0x101, &CMainFrame::OnThemeRebuildMenu)
	ON_MESSAGE(WM_USER + 0x102, &CMainFrame::OnPostPickerRetheme)
	ON_MESSAGE(WM_USER + 0x103, &CMainFrame::OnDeferredAccelRebuild)
	ON_COMMAND(ID_THEME_PANES_ONE, OnThemePanesOne)
	ON_COMMAND(ID_THEME_PANES_TWO, OnThemePanesTwo)
	ON_UPDATE_COMMAND_UI(ID_THEME_PANES_ONE, OnUpdateThemePanesOne)
	ON_UPDATE_COMMAND_UI(ID_THEME_PANES_TWO, OnUpdateThemePanesTwo)
	ON_COMMAND(ID_THEME_SIZE_FORMAT_AUTO, OnThemeSizeFormatAuto)
	ON_COMMAND(ID_THEME_SIZE_FORMAT_BYTES, OnThemeSizeFormatBytes)
	ON_UPDATE_COMMAND_UI(ID_THEME_SIZE_FORMAT_AUTO, OnUpdateThemeSizeFormatAuto)
	ON_UPDATE_COMMAND_UI(ID_THEME_SIZE_FORMAT_BYTES, OnUpdateThemeSizeFormatBytes)
	ON_COMMAND(ID_THEME_VXL_SS_OFF, OnThemeVxlSsOff)
	ON_COMMAND(ID_THEME_VXL_SS_2, OnThemeVxlSs2)
	ON_COMMAND(ID_THEME_VXL_SS_4, OnThemeVxlSs4)
	ON_COMMAND(ID_THEME_VXL_SS_8, OnThemeVxlSs8)
	ON_COMMAND(ID_THEME_VXL_SS_16, OnThemeVxlSs16)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_SS_OFF, OnUpdateThemeVxlSsOff)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_SS_2, OnUpdateThemeVxlSs2)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_SS_4, OnUpdateThemeVxlSs4)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_SS_8, OnUpdateThemeVxlSs8)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_SS_16, OnUpdateThemeVxlSs16)
	ON_COMMAND(ID_THEME_VXL_SHADING, OnThemeVxlShading)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_SHADING, OnUpdateThemeVxlShading)
	ON_COMMAND(ID_THEME_VXL_LIGHTING, OnThemeVxlLighting)
	ON_COMMAND(ID_THEME_PARALLEL_EXTRACT, OnThemeParallelExtract)
	ON_UPDATE_COMMAND_UI(ID_THEME_PARALLEL_EXTRACT, OnUpdateThemeParallelExtract)
	ON_COMMAND(ID_THEME_LIMIT_VXL_CPU, OnThemeLimitVxlCpu)
	ON_UPDATE_COMMAND_UI(ID_THEME_LIMIT_VXL_CPU, OnUpdateThemeLimitVxlCpu)
	ON_COMMAND(ID_THEME_VXL_FULL_HIER, OnThemeVxlFullHier)
	ON_UPDATE_COMMAND_UI(ID_THEME_VXL_FULL_HIER, OnUpdateThemeVxlFullHier)
	ON_COMMAND(ID_KEYBINDS_CONFIGURE, OnKeybindsConfigure)
END_MESSAGE_MAP()


static UINT indicators[] =
{
	ID_SEPARATOR,           // status line indicator
};

CMainFrame::CMainFrame()
{
	m_game = static_cast<t_game>(-1);
	m_lists_initialized = GetAsyncKeyState(VK_SHIFT) < 0;

	m_combine_shadows = AfxGetApp()->GetProfileInt(m_reg_key, "combine_shadows", false);
	m_enable_compression = AfxGetApp()->GetProfileInt(m_reg_key, "enable_compression", true);
	m_palette_i = AfxGetApp()->GetProfileInt(m_reg_key, "palette_i", -1);
	m_split_shadows = AfxGetApp()->GetProfileInt(m_reg_key, "split_shadows", false);
	m_use_palette_for_conversion = AfxGetApp()->GetProfileInt(m_reg_key, "use_palette_for_conversion", false);
	m_two_panes = AfxGetApp()->GetProfileInt(m_reg_key, "two_panes", 1) != 0;
}

CMainFrame::~CMainFrame()
{
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;
	if (!m_wndStatusBar.Create(this)
		|| !m_wndStatusBar.SetIndicators(indicators, sizeof(indicators) / sizeof(UINT)))
	{
		TRACE0("Failed to create status bar\n");
		return -1;
	}
	theme::apply_titlebar(GetSafeHwnd());
	rebuild_menu_owner_draw();
	// Load user-overridden keybinds from registry; defer the accel table
	// rebuild until after LoadFrame finishes. LoadFrame calls Create (which
	// dispatches WM_CREATE -> this OnCreate) and *then* calls LoadAccelTable
	// with IDR_MAINFRAME. If we replace m_hAccelTable here, the subsequent
	// LoadAccelTable asserts in debug builds because m_hAccelTable != NULL.
	// Posting WM_USER+0x103 means rebuild_accel runs after LoadFrame returns
	// to the message loop, by which point MFC has already set the default
	// accel and we can swap it cleanly.
	keybinds::load_from_registry();
	PostMessage(WM_USER + 0x103);
	refresh_menu_shortcuts();
	// Suffix the frame caption with the mix-database source so the user can
	// see at a glance whether names are coming from <data dir>\global mix
	// database.dat (with their own additions, if any) or from the embedded
	// fallback. mix_db_source_none means lookup will miss and rows will show
	// 8-hex IDs — flagged loudly to make the cause obvious.
	{
		const char* tag = nullptr;
		switch (g_mix_db_source)
		{
		case mix_db_source_on_disk:  tag = " [DB: on-disk]"; break;
		case mix_db_source_embedded: tag = " [DB: embedded]"; break;
		case mix_db_source_none:     tag = " [DB: missing]"; break;
		}
		if (tag)
		{
			CString t;
			GetWindowText(t);
			t += tag;
			SetWindowText(t);
		}
	}
	return 0;
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	cs.style &= ~FWS_ADDTOTITLE;
	return CFrameWnd::PreCreateWindow(cs);
}

BOOL CMainFrame::OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext) 
{
	if (!m_wndSplitter.CreateStatic(this, 1, 3))
	{
		TRACE0("Failed to CreateStaticSplitter\n");
		return FALSE;
	}

	if (!m_wndSplitter.CreateView(0, 0,	pContext->m_pNewViewClass, CSize(400, 0), pContext))
	{
		TRACE0("Failed to create first pane\n");
		return FALSE;
	}

	if (!m_wndSplitter.CreateView(0, 1,	pContext->m_pNewViewClass, CSize(400, 0), pContext))
	{
		TRACE0("Failed to create second pane\n");
		return FALSE;
	}

	if (!m_wndSplitter.CreateView(0, 2,	RUNTIME_CLASS(CXCCFileView), CSize(0, 0), pContext))
	{
		TRACE0("Failed to create third pane\n");
		return FALSE;
	}

	m_left_mix_pane = reinterpret_cast<CXCCMixerView*>(m_wndSplitter.GetPane(0, 0));
	m_right_mix_pane = reinterpret_cast<CXCCMixerView*>(m_wndSplitter.GetPane(0, 1));
	m_file_info_pane = reinterpret_cast<CXCCFileView*>(m_wndSplitter.GetPane(0, 2));

	m_left_mix_pane->set_other_panes(m_file_info_pane, m_right_mix_pane);
	m_right_mix_pane->set_other_panes(m_file_info_pane, m_left_mix_pane);

	m_left_mix_pane->set_reg_key("left_mix_pane");
	m_right_mix_pane->set_reg_key("right_mix_pane");

	SetActiveView(reinterpret_cast<CView*>(m_left_mix_pane));

	apply_theme_to_children();
	if (!m_two_panes)
		set_pane_layout(false);
	return true;
}

void CMainFrame::OnViewGameAuto() 
{
	m_game = static_cast<t_game>(-1);
}

void CMainFrame::OnViewGameTD() 
{
	m_game = game_td;
}

void CMainFrame::OnViewGameRA() 
{
	m_game = game_ra;
}

void CMainFrame::OnViewGameTS() 
{
	m_game = game_ts;
}

void CMainFrame::OnViewGameRA2()
{
	m_game = game_ra2;
}

void CMainFrame::OnUpdateViewGameAuto(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_game == -1);
}

void CMainFrame::OnUpdateViewGameTD(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_game == game_td);
}

void CMainFrame::OnUpdateViewGameRA(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_game == game_ra);
}

void CMainFrame::OnUpdateViewGameTS(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_game == game_ts);
}

void CMainFrame::OnUpdateViewGameRA2(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_game == game_ra2);
}

t_game CMainFrame::get_game()
{
	return m_game;
}

void CMainFrame::set_msg(const string& s)
{
	SetMessageText(s.c_str());
}

int CMainFrame::mix_list_create_map(string name, string fname, int file_id, int parent)
{
	int id = m_mix_map_list.size();
	t_mix_map_list_entry& e = m_mix_map_list[id];
	e.name = name;
	e.fname = fname;
	e.id = file_id;
	e.parent = parent;
	return id;
}

int CMainFrame::pal_list_create_map(string name, int parent)
{
	// Use max-key + 1 instead of size() so we don't collide with surviving
	// IDs after clean_pal_map_list / reload_pal_paths erase entries. Erasure
	// punches gaps in the map, and using size() to mint a new ID can land on
	// a still-living game-side node, overwriting it via operator[]. The bug
	// surfaced as the Select Palette tree losing its game-side mix children
	// after the second OK from PAL Paths dialog.
	int id = m_pal_map_list.empty() ? 0 : (m_pal_map_list.rbegin()->first + 1);
	t_pal_map_list_entry& e = m_pal_map_list[id];
	e.name = name;
	e.parent = parent;
	return id;
}

void CMainFrame::clean_pal_map_list()
{
	set<int> used_set;
	for (auto& i : m_pal_list)
		used_set.insert(i.parent);
	for (auto& i : m_pal_map_list)
	{
		if (!used_set.count(i.first))
			continue;
		int p = i.second.parent;
		while (p != -1)
		{
			used_set.insert(p);
			p = find_ref(m_pal_map_list, p).parent;
		}
	}
  t_pal_map_list& map = m_pal_map_list;
  for (auto i = map.begin(); i != map.end(); )
  {
    if (!used_set.count(i->first))
      i = map.erase(i);
    else
      i++;
  }
}

void CMainFrame::do_mix(Cmix_file& f, const string& mix_name, int mix_parent, int pal_parent)
{
	xcc_log::write_line("do_mix starts: " + mix_name, 1);
	set_msg("Reading " + mix_name);
	if (mix_name.find(" - ") == string::npos)
		m_mix_list.push_back(mix_name);
	for (int i = 0; i < f.get_c_files(); i++)
	{
		const int id = f.get_id(i);
		string name = f.get_name(id);
		if (name.empty())
			name = nh(8, id);
		switch (f.get_type(id))
		{
		case ft_mix:
      {
      	Cmix_file g;
        if (!g.open(id, f))
          do_mix(g, mix_name + " - " + name, mix_list_create_map(name, "", id, mix_parent), pal_list_create_map(name, pal_parent));
			}
			set_msg("Ready");
			break;
		case ft_pal:
			{
				t_pal_list_entry e;
				e.name = static_cast<Cfname>(mix_name).get_fname() + " - " + name;
        Cpal_file h;
				h.open(id, f);
				memcpy(e.palette, h.get_data(), sizeof(t_palette));
				e.parent = pal_parent;
				m_pal_list.push_back(e);
				set_msg("Ready");
				break;
			}
		}
	}
	xcc_log::write_line("do_mixs ends", -1);
}

void CMainFrame::find_mixs(const string& dir, t_game game, string filter)
{
	xcc_log::write_line("find_mixs starts: " + dir, 1);
	if (!dir.empty())
	{
		WIN32_FIND_DATA fd;
		HANDLE findhandle = FindFirstFile((dir + filter).c_str(), &fd);
		if (findhandle != INVALID_HANDLE_VALUE)
		{
			int mix_parent = mix_list_create_map(game_name[game], "", 0, -1);
			int pal_parent = pal_list_create_map(game_name[game], -1);
			do
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				const string fname = to_lower(string(fd.cFileName));
				xcc_log::write_line("finds: " + fname, 1);
				Cmix_file f;
				if (!f.open(dir + fname))
					do_mix(f, dir + fname, mix_list_create_map(fname, dir + fname, 0, mix_parent), pal_list_create_map(fname, pal_parent));
				xcc_log::indent(-1);
			}
			while (FindNextFile(findhandle, &fd));
			FindClose(findhandle);
		}
	}
	m_mix_i[game] = m_mix_list.size();
	m_pal_i[game] = m_pal_list.size();
	xcc_log::write_line("find_mixs ends", -1);
}

using t_sort_list = map<string, int>;

string escape_menu_name(string v)
{
	for (size_t i = 0; i < v.size(); i++)
	{
		if (v[i] == '&')
			v.insert(++i, "&");
	}
	return v;
}

void CMainFrame::OnUpdateFileFoundUpdate(CCmdUI* pCmdUI) 
{       
	xcc_log::write_line("OnUpdateFileFoundUpdate starts");
	if (CMenu* menu = pCmdUI->m_pSubMenu)
	{
		menu->DeleteMenu(0, MF_BYPOSITION);
		initialize_lists();
		int j = 0;
		int k = 0;
		for (int i = 0; i < game_unknown; i++)
		{
			if (j == m_mix_i[i])
				continue;
			CMenu sub_menu;
			sub_menu.CreatePopupMenu();
			t_sort_list sort_list;
			for (; j < m_mix_i[i]; j++)
				sort_list[static_cast<Cfname>(m_mix_list[j]).get_fname()] = j;
			if (sort_list.empty())
				continue;
			for (auto& l : sort_list)
				sub_menu.AppendMenu(MF_STRING, ID_FILE_FOUND_MIX000 + l.second, escape_menu_name(l.first).c_str());
			menu->InsertMenu(k++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<DWORD>(sub_menu.GetSafeHmenu()), game_name[i]);
			sub_menu.Detach();
		}
	}
	xcc_log::write_line("OnUpdateFileFoundUpdate ends");
}

void CMainFrame::OnUpdateViewPaletteUpdate(CCmdUI* pCmdUI) 
{
	xcc_log::write_line("OnUpdateViewPaletteUpdate starts");
	CMenu* menu = pCmdUI->m_pSubMenu;
	if (menu)
	{
		menu->DeleteMenu(0, MF_BYPOSITION);
		initialize_lists();
		int j = 0;
		int k = 0;
		for (int i = 0; i < game_unknown; i++)
		{
			if (j == m_pal_i[i])
				continue;
			CMenu sub_menu;
			sub_menu.CreatePopupMenu();
			for (; j < m_pal_i[i]; j++)
				sub_menu.AppendMenu(MF_STRING, ID_VIEW_PALETTE_PAL000 + j, m_pal_list[j].name.c_str());
			menu->InsertMenu(k++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<DWORD>(sub_menu.GetSafeHmenu()), game_name[i]);
			sub_menu.Detach();
		}
	}
	xcc_log::write_line("OnUpdateViewPaletteUpdate ends");
}

void CMainFrame::initialize_lists()
{
	if (m_lists_initialized)
		return;
	CWaitCursor wait;
	xcc_log::write_line("initialize_lists starts");
	xcc_log::write_line("primary dir: " + xcc_dirs::get_dir(game_td));
	xcc_log::write_line("secondary dir: " + xcc_dirs::get_td_secondary_dir());
	xcc_log::write_line("ra dir: " + xcc_dirs::get_dir(game_ra));
	xcc_log::write_line("ts dir: " + xcc_dirs::get_dir(game_ts));
	xcc_log::write_line("ra2 dir: " + xcc_dirs::get_dir(game_ra2));
	xcc_log::write_line("rg dir: " + xcc_dirs::get_dir(game_rg));
	xcc_log::write_line("gr dir: " + xcc_dirs::get_dir(game_gr));
	xcc_log::write_line("gr zh dir: " + xcc_dirs::get_dir(game_gr_zh));
	xcc_log::write_line("bfme dir: " + xcc_dirs::get_dir(game_bfme));
	xcc_log::write_line("cd dir: " + xcc_dirs::get_cd_dir());
	xcc_log::write_line("data dir: " + xcc_dirs::get_data_dir());
	find_mixs(xcc_dirs::get_dir(game_td), game_td, "*.mix");
	find_mixs(xcc_dirs::get_td_secondary_dir(), game_td, "*.mix");
	find_mixs(xcc_dirs::get_dir(game_ra), game_ra, "*.mix");
	find_mixs(xcc_dirs::get_dir(game_ts), game_ts, "*.mix");
	find_mixs(xcc_dirs::get_dir(game_dune2), game_dune2, "*.pak");
	find_mixs(xcc_dirs::get_dir(game_dune2000), game_dune2000, "*.mix");
	find_mixs(xcc_dirs::get_dir(game_ra2), game_ra2, "*.mix");
	find_mixs("", game_ra2_yr, "");
	find_mixs(xcc_dirs::get_dir(game_rg) + "data\\", game_rg, "*.dat");
	find_mixs(xcc_dirs::get_dir(game_rg) + "data\\", game_rg, "*.dbs");
	find_mixs(xcc_dirs::get_dir(game_rg) + "data\\", game_rg, "*.mix");
	find_mixs(xcc_dirs::get_dir(game_rg) + "data\\", game_rg, "*.pkg");
	find_mixs(xcc_dirs::get_dir(game_gr), game_gr, "*.big");
	find_mixs(xcc_dirs::get_dir(game_gr_zh), game_gr_zh, "*.big");
	find_mixs("", game_ebfd, ""); //i don't think mixer supports the files
	find_mixs(xcc_dirs::get_dir(game_nox), game_nox, "*.mix");
	find_mixs(xcc_dirs::get_dir(game_bfme), game_bfme, "*.big");
	find_mixs(xcc_dirs::get_dir(game_bfme2), game_bfme2, "*.big");
	find_mixs(xcc_dirs::get_dir(game_tw), game_tw, "*.big");
	find_mixs("", game_ts_fs, "");	//for some reason the order of these matter and i don't care enough to fix it properly, it's probably the wacky order of how the games are defined


	t_pal_list pal_list = m_pal_list;
	m_pal_list.clear();
	int j = 0;
	for (int i = 0; i < game_unknown; i++)
	{
		t_sort_list sort_list;
		for (; j < m_pal_i[i]; j++)
			sort_list[pal_list[j].name] = j;
		for (auto& l : sort_list)
			m_pal_list.push_back(pal_list[l.second]);
	}

	Cmix_file f1, f2;
	Cpal_file pal_f;
	if (!f1.open("temperat.mix") && !pal_f.open("temperat.pal", f1))
    memcpy(m_td_palette, pal_f.get_palette(), sizeof(t_palette));
  if (!f1.open("redalert.mix") && !f2.open("local.mix", f1) && !pal_f.open("temperat.pal", f2))
    memcpy(m_ra_palette, pal_f.get_palette(), sizeof(t_palette));
	if (!f1.open("tibsun.mix") && !f2.open("cache.mix", f1) && !pal_f.open("unittem.pal", f2))
    memcpy(m_ts_palette, pal_f.get_palette(), sizeof(t_palette));
	if (!f1.open("ra2.mix") && !f2.open("cache.mix", f1) && !pal_f.open("unittem.pal", f2))
		memcpy(m_ra2_palette, pal_f.get_palette(), sizeof(t_palette));
	if (m_palette_i >= m_pal_list.size())
		m_palette_i = -1;
	// User-configured PAL paths (folders + archives, registry-persisted).
	// Loaded after the per-game ranges so m_pal_i[] stays the boundary.
	reload_pal_paths();
	clean_pal_map_list();
	m_lists_initialized = true;
	xcc_log::write_line("initialize_lists ends");
}

string CMainFrame::get_mix_name(int i) const
{
	return m_mix_list[i];
}

const t_palette_entry* CMainFrame::get_game_palette(t_game game)
{
	initialize_lists();
	switch (game)
	{
	case game_td:
		return m_td_palette;
	case game_ra:
		return m_ra_palette;
	case game_ra2:
		return m_ra2_palette;
	default:
		return m_ts_palette;
	}
}

const t_palette_entry* CMainFrame::get_pal_data()
{
	initialize_lists();
	return m_palette_i == -1 ? NULL : m_pal_list[m_palette_i].palette;
}

int CMainFrame::get_vxl_mode() const
{
	return m_vxl_mode;
}

void CMainFrame::OnViewPaletteAuto() 
{
	set_palette(-1);
}

void CMainFrame::OnUpdateViewPaletteAuto(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_palette_i == -1);
}

void CMainFrame::OnViewPalette(UINT ID) 
{
	set_palette(ID - ID_VIEW_PALETTE_PAL000);
}

void CMainFrame::OnUpdateViewPalette(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_palette_i == pCmdUI->m_nID - ID_VIEW_PALETTE_PAL000);
}

void CMainFrame::OnViewPalettePrev() 
{
	if (!m_pal_i[game_ra2])
		return;
	if (m_palette_i > -1)
		m_palette_i--;
	m_file_info_pane->Invalidate();
	set_msg((m_palette_i == -1 ? "default" : m_pal_list[m_palette_i].name) + " selected");
}

void CMainFrame::OnViewPaletteNext() 
{
	if (!m_pal_i[game_ra2])
		return;
	m_palette_i++;
	if (m_palette_i == m_pal_i[game_ra2])
		m_palette_i = 0;
	m_file_info_pane->Invalidate();
	set_msg(m_pal_list[m_palette_i].name + " selected");
}

bool CMainFrame::auto_select(t_game game, string palette)
{
	const int user_start = m_pal_i[game_unknown - 1];
	const int per_game_lo = game < 1 ? 0 : m_pal_i[game - 1];
	const int per_game_hi = m_pal_i[game];
	const int user_hi = static_cast<int>(m_pal_list.size());
	const bool override_on = CPalPathsDlg::override_per_game();

	auto try_range = [&](int lo, int hi) -> bool
	{
		for (int i = lo; i < hi; i++)
		{
			if (m_pal_list[i].name.find(palette) == string::npos)
				continue;
			set_palette(i);
			set_msg(m_pal_list[m_palette_i].name + " selected");
			return true;
		}
		return false;
	};

	if (override_on)
	{
		// User-loaded PAL Paths win: try the user slice first, fall back to
		// the per-game range. A matching temperat.pal in PalPaths shadows the
		// stock RA2/TS/etc. one.
		if (try_range(user_start, user_hi)) return true;
		if (try_range(per_game_lo, per_game_hi)) return true;
	}
	else
	{
		// Default: per-game first, PalPaths only as a fallback.
		if (try_range(per_game_lo, per_game_hi)) return true;
		if (try_range(user_start, user_hi)) return true;
	}
	return false;
}

void CMainFrame::reload_pal_paths()
{
	// Build the set of map-tree nodes to drop: user roots from a previous
	// reload (registry-backed PalPaths) and all their descendants. Skip
	// game roots (parent == -1, name == game_name[g]) and session-only
	// roots created by the Select Palette dialog's Load Pal / Load Mix
	// buttons (preserved across reloads so an ad-hoc archive isn't wiped
	// just because the user opened the PAL Paths editor).
	std::set<int> drop_roots;
	for (auto& kv : m_pal_map_list)
	{
		if (kv.second.parent != -1)
			continue;
		bool is_game = false;
		for (int g = 0; g < game_unknown; g++)
		{
			if (kv.second.name == game_name[g])
			{
				is_game = true;
				break;
			}
		}
		if (is_game || kv.second.session_only)
			continue;
		drop_roots.insert(kv.first);
	}
	std::set<int> drop_all = drop_roots;
	for (bool added = true; added; )
	{
		added = false;
		for (auto& kv : m_pal_map_list)
		{
			if (drop_all.count(kv.first) || !drop_all.count(kv.second.parent))
				continue;
			drop_all.insert(kv.first);
			added = true;
		}
	}
	// Erase m_pal_list entries whose parent chain ends in a dropped root.
	// A list entry survives when its parent (or any ancestor) is NOT in
	// drop_all — i.e. it's under a game root or a session-only root.
	// While compacting, also remap m_palette_i so an existing selection
	// follows its underlying entry to its new index.
	{
		auto write = m_pal_list.begin();
		const size_t per_game_end = m_pal_i[game_unknown - 1];
		int new_palette_i = -1;
		for (size_t r = 0; r < m_pal_list.size(); r++)
		{
			bool dropped = false;
			if (r >= per_game_end)  // never drop entries inside per-game ranges
			{
				int p = m_pal_list[r].parent;
				while (p != -1)
				{
					auto it = m_pal_map_list.find(p);
					if (it == m_pal_map_list.end()) break;
					if (drop_all.count(p)) { dropped = true; break; }
					p = it->second.parent;
				}
			}
			if (!dropped)
			{
				if (static_cast<int>(r) == m_palette_i)
					new_palette_i = static_cast<int>(write - m_pal_list.begin());
				if (write != m_pal_list.begin() + r)
					*write = std::move(m_pal_list[r]);
				++write;
			}
		}
		m_pal_list.erase(write, m_pal_list.end());
		m_palette_i = new_palette_i;
	}
	for (int id : drop_all)
		m_pal_map_list.erase(id);

	// Load each PalPaths registry entry in order. No leaf-name dedupe:
	// stripping later-loaded duplicates would orphan the tree node created
	// inside load_pal_folder/load_pal_mix and leave the dialog showing an
	// empty branch. "Higher priority wins" applies to Auto Select instead —
	// auto_select() walks the user slice top-to-bottom and accepts the first
	// match, so the registry order naturally encodes priority without us
	// having to throw away later entries here.
	auto entries = CPalPathsDlg::load_from_registry();
	for (size_t idx = 0; idx < entries.size(); idx++)
	{
		const auto& e = entries[idx];
		if (e.path.empty())
			continue;
		int root = e.is_folder ? load_pal_folder(e.path) : load_pal_mix(e.path);
		if (root >= 0)
			m_pal_map_list[root].order = static_cast<int>(idx);
	}
}

void CMainFrame::OnViewPaletteUseForConversion() 
{
	m_use_palette_for_conversion = !m_use_palette_for_conversion;
}

void CMainFrame::OnUpdateViewPaletteUseForConversion(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_use_palette_for_conversion);
}

void CMainFrame::OnViewPaletteConvertFromTD() 
{
	m_convert_from_td = !m_convert_from_td;	
	m_convert_from_ra = false;
}

void CMainFrame::OnUpdateViewPaletteConvertFromTD(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_convert_from_td);
}

void CMainFrame::OnViewPaletteConvertFromRA() 
{
	m_convert_from_td = false;
	m_convert_from_ra = !m_convert_from_ra;	
}

void CMainFrame::OnUpdateViewPaletteConvertFromRA(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_convert_from_ra);
}

void CMainFrame::OnViewVoxelNormal() 
{
	m_vxl_mode = 0;
	m_file_info_pane->Invalidate();
}

void CMainFrame::OnUpdateViewVoxelNormal(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_vxl_mode == 0);
}

void CMainFrame::OnViewVoxelSurfaceNormals() 
{
	m_vxl_mode = 1;
	m_file_info_pane->Invalidate();
}

void CMainFrame::OnUpdateViewVoxelSurfaceNormals(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_vxl_mode == 1);
}

void CMainFrame::OnViewVoxelDepthInformation() 
{
	m_vxl_mode = 2;
	m_file_info_pane->Invalidate();
}

void CMainFrame::OnUpdateViewVoxelDepthInformation(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_vxl_mode == 2);
}

void CMainFrame::OnConversionCombineShadows() 
{
	m_combine_shadows = !m_combine_shadows;	
}

void CMainFrame::OnUpdateConversionCombineShadows(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_combine_shadows);
}

void CMainFrame::OnConversionFixShadows() 
{
	m_fix_shadows = !m_fix_shadows;	
}

void CMainFrame::OnUpdateConversionFixShadows(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_fix_shadows);
}

void CMainFrame::OnConversionSplitShadows() 
{
	m_split_shadows = !m_split_shadows;	
}

void CMainFrame::OnUpdateConversionSplitShadows(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_split_shadows);
}

void CMainFrame::OnConversionEnableCompression() 
{
	m_enable_compression = !m_enable_compression;	
}

void CMainFrame::OnUpdateConversionEnableCompression(CCmdUI* pCmdUI) 
{
	pCmdUI->SetCheck(m_enable_compression);
}

void CMainFrame::OnConversionRemapTeamColors() 
{
	m_remap_team_colors = !m_remap_team_colors;	
}

void CMainFrame::OnUpdateConversionRemapTeamColors(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(false);
	pCmdUI->SetCheck(m_remap_team_colors);
}

LPDIRECTSOUND CMainFrame::get_ds()
{
	if (!m_ds)
		open_ds();
	return m_ds;
}

void CMainFrame::open_ds()
{
	HRESULT dsr;
	assert(!m_ds);
    dsr = DirectSoundCreate(NULL, &m_ds, NULL);
	xcc_log::write_line("DirectSoundCreate returned " + nh(8, dsr));
	if (m_ds)
	{
		dsr = m_ds->SetCooperativeLevel(m_hWnd, DSSCL_NORMAL);
		xcc_log::write_line("SetCooperativeLevel returned " + nh(8, dsr));
	}
}

void CMainFrame::close_ds()
{
	if (!m_ds)
		return;
	m_ds->Release();
	m_ds = NULL;
}

static CXCCMixerApp* GetApp()
{
	return static_cast<CXCCMixerApp*>(AfxGetApp());
}

void CMainFrame::OnViewDirectories() 
{
	CDirectoriesDlg dlg;
	dlg.DoModal();
}

void CMainFrame::OnFileSearch()
{
	// Detect which pane is focused so predefined-game results route into
	// the user's active pane instead of always landing in the right pane.
	bool prefer_right = false;
	if (CWnd* focus = GetFocus())
	{
		HWND h = focus->GetSafeHwnd();
		if (m_right_mix_pane && (h == m_right_mix_pane->GetSafeHwnd() || ::IsChild(m_right_mix_pane->GetSafeHwnd(), h)))
			prefer_right = true;
	}
	CSearchFileDlg dlg;
	dlg.set(this, prefer_right);
	dlg.DoModal();
}

void CMainFrame::OnFileSearchInMix()
{
	CXCCMixerView* pane = nullptr;
	CWnd* focus = GetFocus();
	if (focus)
	{
		HWND h = focus->GetSafeHwnd();
		if (m_left_mix_pane && (h == m_left_mix_pane->GetSafeHwnd() || ::IsChild(m_left_mix_pane->GetSafeHwnd(), h)))
			pane = m_left_mix_pane;
		else if (m_right_mix_pane && (h == m_right_mix_pane->GetSafeHwnd() || ::IsChild(m_right_mix_pane->GetSafeHwnd(), h)))
			pane = m_right_mix_pane;
	}
	if (!pane)
		pane = m_left_mix_pane;
	if (!pane)
		return;
	CSearchInPaneDlg dlg;
	dlg.set(pane);
	dlg.DoModal();
}

void CMainFrame::OnLaunchXSTE_RA2() 
{
	CXSTE_dlg dlg(game_ra2);
	dlg.DoModal();
}

void CMainFrame::OnUpdateLaunchXSTE_RA2(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_language_mix(game_ra2)).exists());
}

void CMainFrame::OnLaunchXSTE_RA2_YR() 
{
	CXSTE_dlg dlg(game_ra2_yr);
	dlg.DoModal();
}

void CMainFrame::OnUpdateLaunchXSTE_RA2_YR(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_language_mix(game_ra2_yr)).exists());
}

void CMainFrame::OnLaunchXSTE_GR() 
{
	CXSTE_dlg dlg(game_gr);
	dlg.DoModal();
}

void CMainFrame::OnLaunchXSTE_GR_ZH() 
{
	CXSTE_dlg dlg(game_gr_zh);
	dlg.DoModal();
}

void CMainFrame::OnUpdateLaunchXSTE_GR(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_dir(game_gr) + xcc_dirs::get_csf_fname(game_gr)).exists()
		|| Cfname(xcc_dirs::get_language_mix(game_gr)).exists());
}

void CMainFrame::OnUpdateLaunchXSTE_GR_ZH(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_dir(game_gr_zh) + xcc_dirs::get_csf_fname(game_gr_zh)).exists()
		|| Cfname(xcc_dirs::get_language_mix(game_gr_zh)).exists());
}

void CMainFrame::OnLaunchXSTE_Open() 
{
	CFileDialog dlg(true, "csf", NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, "CSF files (*.csf)|*.csf|", this);
	if (IDOK != dlg.DoModal())
		return;
	CXSTE_dlg dlg2(game_unknown);
	dlg2.open(static_cast<string>(dlg.GetPathName()));
	dlg2.DoModal();
}

void CMainFrame::OnLaunchXSE_RA2() 
{
	CXSE_dlg dlg(game_ra2);
	dlg.DoModal();
}

void CMainFrame::OnUpdateLaunchXSE_RA2(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_language_mix(game_ra2)).exists());
}

void CMainFrame::OnLaunchXSE_RA2_YR() 
{
	CXSE_dlg dlg(game_ra2_yr);
	dlg.DoModal();
}

void CMainFrame::OnUpdateLaunchXSE_RA2_YR(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_language_mix(game_ra2_yr)).exists());	
}

void CMainFrame::OnLaunchXSE_Open() 
{
	CFileDialog dlg0(true, "bag", NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, "BAG files (*.bag)|*.bag|", this);
	if (IDOK != dlg0.DoModal())
		return;
	Cfname bag_path(string(dlg0.GetPathName()));
	Cfname idx_path(bag_path.get_path() + bag_path.get_ftitle() + ".idx");
	if (!idx_path.exists())
	{
		CFileDialog dlg1(true, "idx", NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, "IDX files (*.idx)|*.idx|", this);
		if (IDOK != dlg1.DoModal())
			return;
		idx_path = string(dlg1.GetPathName());
	}
	CXSE_dlg dlg2(game_ra2_yr);
	dlg2.bag_file(bag_path.get_all());
	dlg2.idx_file(idx_path.get_all());
	dlg2.DoModal();
}

void CMainFrame::OnDestroy()
{
	AfxGetApp()->WriteProfileInt(m_reg_key, "combine_shadows", m_combine_shadows);
	AfxGetApp()->WriteProfileInt(m_reg_key, "enable_compression", m_enable_compression);
	AfxGetApp()->WriteProfileInt(m_reg_key, "palette_i", m_palette_i);	//i don't care about keeping them wrong for older compatibility
	AfxGetApp()->WriteProfileInt(m_reg_key, "split_shadows", m_split_shadows);
	AfxGetApp()->WriteProfileInt(m_reg_key, "use_palette_for_conversion", m_use_palette_for_conversion);

	// Persist window placement so the next launch restores the user's last
	// size/position instead of always starting maximized. rcNormalPosition is
	// the restored-state rect (valid even when the window is currently
	// maximized); showCmd records whether the window was maximized at exit.
	WINDOWPLACEMENT wp = {};
	wp.length = sizeof(wp);
	if (GetWindowPlacement(&wp))
	{
		AfxGetApp()->WriteProfileInt(m_reg_key, "win_left",   wp.rcNormalPosition.left);
		AfxGetApp()->WriteProfileInt(m_reg_key, "win_top",    wp.rcNormalPosition.top);
		AfxGetApp()->WriteProfileInt(m_reg_key, "win_right",  wp.rcNormalPosition.right);
		AfxGetApp()->WriteProfileInt(m_reg_key, "win_bottom", wp.rcNormalPosition.bottom);
		AfxGetApp()->WriteProfileInt(m_reg_key, "win_maximized",
			wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
	}

	CFrameWnd::OnDestroy();
}

using t_theme_list = Ctheme_ts_ini_reader::t_theme_list;

void CMainFrame::OnLaunchXTW_TS() 
{
	Cmix_file tibsun;
	Cmix_file local;
	Ccc_file theme(true);
	Ctheme_ts_ini_reader ir;
	if (tibsun.open("tibsun.mix")
		|| local.open("local.mix", tibsun)
		|| theme.open("theme.ini", local)
		|| ir.process(theme.vdata()))
		return;
	t_theme_list theme_list = ir.get_theme_list();
	string dir = xcc_dirs::get_dir(game_ts);
	WIN32_FIND_DATA fd;
	HANDLE findhandle = FindFirstFile((dir + "*.aud").c_str(), &fd);
	if (findhandle != INVALID_HANDLE_VALUE)
	{
		do
		{
			const string fname = dir + fd.cFileName;
			Caud_file f;
			if (f.open(fname))
				continue;
			char b[MAX_PATH];
			int error = GetShortPathName(fname.c_str(), b, MAX_PATH);
			if (error > 0 && error < MAX_PATH)
			{
				Ctheme_data e;
				e.name(Cfname(fd.cFileName).get_ftitle());
				e.length(static_cast<float>(f.get_c_samples()) / f.get_samplerate() / 60);
				theme_list[to_upper(Cfname(b).get_ftitle())] = e;
			}
		}
		while (FindNextFile(findhandle, &fd));
		FindClose(findhandle);
	}
	ofstream g((dir + "theme.ini").c_str());
	g << "[Themes]" << endl;
	// "1=INTRO" << endl;
	int j = 51;
	for (auto& i : theme_list)
		g << n(j++) << '=' << to_upper(i.first) << endl;
	g << endl;
	for (auto& i : theme_list)
	{
		const Ctheme_data& e = i.second;
		g << '[' << to_upper(i.first) << ']' << endl
			<< "Name=" << e.name() << endl;
		if (e.normal())
			g << "Length=" << e.length() << endl;
		if (!e.normal())
			g << "Normal=no" << endl;
		if (e.scenario())
			g << "Scenario=" <<  n(e.scenario()) << endl;
		if (!e.side().empty())
			g << "Side=" <<  e.side() << endl;
		if (e.repeat())
			g << "Repeat=yes" << endl;
		g << endl;
	}
	if (g.fail())
		MessageBox("Error writing theme.ini.", NULL, MB_ICONERROR);
	else
		MessageBox((n(theme_list.size()) + " themes have been written to theme.ini.").c_str());
}

void CMainFrame::OnUpdateLaunchXTW_TS(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(!xcc_dirs::get_dir(game_ts).empty());	
}

void CMainFrame::launch_xtw(t_game game)
{
	Cmix_file ra2;
	Cmix_file local;
	Ccc_file theme(true);
	string theme_ini_fname = game == game_ra2 ? "theme.ini" : "thememd.ini";
	Ctheme_ts_ini_reader ir;
	if (ra2.open(xcc_dirs::get_main_mix(game))
		|| local.open(xcc_dirs::get_local_mix(game), ra2)
		|| theme.open(theme_ini_fname, local)
		|| ir.process(theme.vdata()))
		return;
	t_theme_list theme_list = ir.get_theme_list();
	string dir = xcc_dirs::get_dir(game_ra2);
	WIN32_FIND_DATA fd;
	HANDLE findhandle = FindFirstFile((dir + "*.wav").c_str(), &fd);
	if (findhandle != INVALID_HANDLE_VALUE)
	{
		CXSTE xste;
		bool xste_open = !xste.open(game);
		do
		{
			const string fname = dir + fd.cFileName;
			Cwav_file f;
			if (f.open(fname) || f.process())
				continue;
			char b[MAX_PATH];
			int error = GetShortPathName(fname.c_str(), b, MAX_PATH);
			if (error > 0 && error < MAX_PATH)
			{
				Ctheme_data e;
				e.name("THEME:" + Cfname(b).get_ftitle());
				e.sound(Cfname(b).get_ftitle());
				theme_list[to_upper(Cfname(b).get_ftitle())] = e;
				if (xste_open)
					xste.csf_f().set_value(e.name(), Ccsf_file::convert2wstring(Cfname(fname).get_ftitle()), "");

			}
		}
		while (FindNextFile(findhandle, &fd));
		if (xste_open)
			xste.write();
		FindClose(findhandle);
	}
	ofstream g((dir + theme_ini_fname).c_str());
	g << "[Themes]" << endl;
	int j = 51;
	for (auto& i : theme_list)
		g << n(j++) << '=' << to_upper(i.first) << endl;
	g << endl;
	for (auto& i : theme_list)
	{
		const Ctheme_data& e = i.second;
		g << '[' << to_upper(i.first) << ']' << endl;
		if (!e.name().empty())
			g << "Name=" << e.name() << endl;
		if (!e.normal())
			g << "Normal=no" << endl;
		if (e.repeat())
			g << "Repeat=yes" << endl;
		if (!e.sound().empty())
			g << "Sound=" << e.sound() << endl;
		g << endl;
	}
	if (g.fail())
		MessageBox(("Error writing " + theme_ini_fname + ".").c_str(), NULL, MB_ICONERROR);
	else
		MessageBox((n(theme_list.size()) + " themes have been written to " + theme_ini_fname + ".").c_str());
}

void CMainFrame::OnLaunchXTW_RA2() 
{
	launch_xtw(game_ra2);
}

void CMainFrame::OnUpdateLaunchXTW_RA2(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(!xcc_dirs::get_dir(game_ra2).empty());	
}

void CMainFrame::OnLaunchXTW_RA2_YR() 
{
	launch_xtw(game_ra2_yr);
}

void CMainFrame::OnUpdateLaunchXTW_RA2_YR(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(Cfname(xcc_dirs::get_language_mix(game_ra2_yr)).exists());
}

CXCCMixerView* CMainFrame::left_mix_pane()
{
	return m_left_mix_pane;
}

CXCCMixerView* CMainFrame::right_mix_pane()
{
	return m_right_mix_pane;
}

CXCCFileView* CMainFrame::file_info_pane()
{
	return m_file_info_pane;
}

BOOL CMainFrame::OnIdle(LONG lCount)
{
	initialize_lists();
	return m_left_mix_pane->OnIdle(lCount) 
		|| m_right_mix_pane->OnIdle(lCount);
}

void CMainFrame::OnViewReport() 
{
	string page;
	CString version;
	if (version.LoadString(IDR_MAINFRAME))
		page += "<tr><th colspan=2>" + static_cast<string>(version);
	page += "<tr><td>Left pane<td>" + m_left_mix_pane->get_dir()
		+ "<tr><td>Right pane<td>" + m_right_mix_pane->get_dir()
		+ "<tr><td>Combine shadows<td>" + btoa(m_combine_shadows)
		+ "<tr><td>Split shadows<td>" + btoa(m_split_shadows)
		+ "<tr><td>TD dir<td>" + xcc_dirs::get_dir(game_td)
		+ "<tr><td>RA dir<td>" + xcc_dirs::get_dir(game_ra)
		+ "<tr><td>TS dir<td>" + xcc_dirs::get_dir(game_ts)
		+ "<tr><td>RA2 dir<td>" + xcc_dirs::get_dir(game_ra2)
		+ "<tr><td>RG dir<td>" + xcc_dirs::get_dir(game_rg)
		+ "<tr><td>GR dir<td>" + xcc_dirs::get_dir(game_gr)
		+ "<tr><td>GR ZH dir<td>" + xcc_dirs::get_dir(game_gr_zh)
		+ "<tr><td>Data dir<td>" + xcc_dirs::get_data_dir()
		+ "<tr><td>EXE dir<td>" + GetModuleFileName().get_path();
		// + tr(td() + td())
	string fname = get_temp_path() + "XCC Mixer Report.html";
	ofstream(fname.c_str()) << "<link rel=stylesheet href=\"http://xhp.xwis.net/xcc.css\"><table border=0 width=100%><tr><td colspan=2><table border=1 width=100%>" + page + "</table><tr><td valign=top>" + m_left_mix_pane->report() + "<td valign=top>" + m_right_mix_pane->report() + "</table>";
	ShellExecute(m_hWnd, "open", fname.c_str(), NULL, NULL, SW_SHOW);
}

void CMainFrame::OnUpdateViewReport(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(!OnIdle(0));
}

void CMainFrame::OnViewPaletteSelect() 
{
	int old_palette = m_palette_i;
	CSelectPaletteDlg dlg;
	if (m_palette_i != -1)
		dlg.current_palette(m_palette_i);
	dlg.set(this, m_pal_map_list, m_pal_list);
	if (IDOK == dlg.DoModal())
		assert(m_palette_i == dlg.current_palette());
	else
		set_palette(old_palette);
}


void CMainFrame::set_palette(int id)
{
	if (m_palette_i == id)
		return;
	m_palette_i = id;
	m_file_info_pane->Invalidate();
}

// Walk a (possibly nested) Cmix_file and import every palette inside under
// pal_parent. mix_label is the human-readable prefix used for entry names
// ("foo.mix - palette.pal"). Returns count of palettes added at this level
// + below. Recursion creates a fresh pal-map sub-node per nested MIX.
static int import_pals_from_mix(CMainFrame& frame, Cmix_file& f, const std::string& mix_label, int pal_parent)
{
	int added = 0;
	for (int i = 0; i < f.get_c_files(); i++)
	{
		const int id = f.get_id(i);
		std::string name = f.get_name(id);
		if (name.empty())
			name = nh(8, id);
		switch (f.get_type(id))
		{
		case ft_mix:
		{
			Cmix_file g;
			if (!g.open(id, f))
			{
				int sub_parent = frame.pal_list_create_map(name, pal_parent);
				int sub_added = import_pals_from_mix(frame, g, mix_label + " - " + name, sub_parent);
				if (!sub_added)
				{
					// No palettes deeper in this branch — drop the empty
					// tree node so the dialog doesn't show dead leaves.
					auto& map = frame.pal_map_list_mut();
					map.erase(sub_parent);
				}
				added += sub_added;
			}
			break;
		}
		case ft_pal:
		{
			Cpal_file h;
			if (h.open(id, f))
				break;
			t_pal_list_entry e;
			e.name = mix_label + " - " + name;
			memcpy(e.palette, h.get_data(), sizeof(t_palette));
			e.parent = pal_parent;
			frame.pal_list_mut().push_back(e);
			added++;
			break;
		}
		default:
			break;
		}
	}
	return added;
}

int CMainFrame::load_pal_mix(const string& path)
{
	Cmix_file f;
	if (f.open(path))
		return -1;
	std::string mix_name = static_cast<Cfname>(path).get_fname();
	if (mix_name.empty())
		mix_name = path;
	int parent_id = pal_list_create_map(mix_name, -1);
	int added = import_pals_from_mix(*this, f, mix_name, parent_id);
	if (!added)
	{
		m_pal_map_list.erase(parent_id);
		return -1;
	}
	return parent_id;
}

// Walk a folder recursively, importing every .pal file under it. Each
// subdirectory becomes a child tree node so the dialog shows the on-disk
// hierarchy. label is the prefix used for entry names ("foo - palette.pal");
// parent_id is the pal_map_list parent under which this level's entries go.
// Returns the count of palettes added at this level + below.
static int import_pals_from_dir(CMainFrame& frame, const std::string& dir,
	const std::string& label, int parent_id)
{
	int added = 0;
	WIN32_FIND_DATA fd;
	HANDLE h = FindFirstFile((dir + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	do
	{
		const std::string name = fd.cFileName;
		if (name == "." || name == "..")
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// Lazy: create the sub-node only if the recursion finds palettes
			// inside it. Otherwise we'd litter the tree with empty folders.
			int sub_parent = frame.pal_list_create_map(name, parent_id);
			int sub_added = import_pals_from_dir(frame, dir + name + '\\',
				label + " - " + name, sub_parent);
			if (!sub_added)
				frame.pal_map_list_mut().erase(sub_parent);
			added += sub_added;
			continue;
		}
		// Match *.pal case-insensitively. FindFirstFile gives us everything
		// since we asked for "*"; do the extension test ourselves.
		if (name.size() < 4)
			continue;
		std::string ext = name.substr(name.size() - 4);
		for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
		if (ext != ".pal")
			continue;
		Cpal_file pf;
		if (pf.open(dir + name))
			continue;
		t_pal_list_entry e;
		e.name = label + " - " + name;
		memcpy(e.palette, pf.get_data(), sizeof(t_palette));
		e.parent = parent_id;
		frame.pal_list_mut().push_back(e);
		added++;
	}
	while (FindNextFile(h, &fd));
	FindClose(h);
	return added;
}

int CMainFrame::load_pal_folder(const string& folder)
{
	string dir = folder;
	if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
		dir += '\\';
	string folder_name = Cfname(dir.substr(0, dir.size() - 1)).get_fname();
	if (folder_name.empty())
		folder_name = dir;
	int parent_id = pal_list_create_map(folder_name, -1);
	int added = import_pals_from_dir(*this, dir, folder_name, parent_id);
	if (!added)
	{
		m_pal_map_list.erase(parent_id);
		return -1;
	}
	return parent_id;
}

void CMainFrame::OnViewPalettePrevSibling()
{
	if (m_palette_i < 0 || m_pal_list.empty())
		return;
	int parent = m_pal_list[m_palette_i].parent;
	int n = static_cast<int>(m_pal_list.size());
	for (int i = 1; i <= n; i++)
	{
		int j = (m_palette_i - i + n) % n;
		if (m_pal_list[j].parent == parent)
		{
			set_palette(j);
			set_msg(m_pal_list[j].name + " selected");
			return;
		}
	}
}

void CMainFrame::OnViewPaletteNextSibling()
{
	if (m_palette_i < 0 || m_pal_list.empty())
		return;
	int parent = m_pal_list[m_palette_i].parent;
	int n = static_cast<int>(m_pal_list.size());
	for (int i = 1; i <= n; i++)
	{
		int j = (m_palette_i + i) % n;
		if (m_pal_list[j].parent == parent)
		{
			set_palette(j);
			set_msg(m_pal_list[j].name + " selected");
			return;
		}
	}
}

void CMainFrame::OnViewPaletteAutoSelect() 
{
	m_file_info_pane->auto_select();
}

void CMainFrame::OnUpdateViewPaletteAutoSelect(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_file_info_pane->can_auto_select());
}

// ---------- Theme menu ----------

void CMainFrame::OnThemeLight()
{
	theme::set(theme::mode_light);
	theme::apply_titlebar(GetSafeHwnd());
	// Defer the menu rebuild: when this command is invoked from a click on the
	// Theme popup, the popup is still mid-dismiss and SetMenuItemInfo against
	// its items doesn't reliably take effect — leaving them stuck as
	// MFT_OWNERDRAW so the next time the popup opens it paints blank (the
	// owner-draw handler returns early in light mode). Posting WM_USER+0x101
	// runs rebuild_menu_owner_draw after the click's menu loop has fully
	// unwound. The Ctrl+1/Ctrl+2 accelerator path doesn't have this race, but
	// posting is harmless there.
	PostMessage(WM_USER + 0x101);
	apply_theme_to_children();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}

void CMainFrame::OnThemeDark()
{
	theme::set(theme::mode_dark);
	theme::apply_titlebar(GetSafeHwnd());
	PostMessage(WM_USER + 0x101);
	apply_theme_to_children();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}

void CMainFrame::OnThemeSystem()
{
	theme::set(theme::mode_system);
	theme::apply_titlebar(GetSafeHwnd());
	PostMessage(WM_USER + 0x101);
	apply_theme_to_children();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}

// WM_SETTINGCHANGE with lParam == "ImmersiveColorSet" fires when the user
// flips Windows' Apps Light/Dark setting. Only meaningful in mode_system —
// refresh_system_mode() returns false in any other mode.
void CMainFrame::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CFrameWnd::OnSettingChange(uFlags, lpszSection);
	// Section is "ImmersiveColorSet" for the Apps Light/Dark flip on Win10/11,
	// but some shells/builds broadcast with NULL or other strings — and the
	// query itself is one cheap registry read, so just always re-check when
	// the user is in system mode. refresh_system_mode() is a no-op in
	// light/dark modes.
	if (theme::refresh_system_mode())
	{
		theme::apply_titlebar(GetSafeHwnd());
		PostMessage(WM_USER + 0x101);
		apply_theme_to_children();
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
}

LRESULT CMainFrame::OnDeferredAccelRebuild(WPARAM, LPARAM)
{
	// Posted from OnCreate. Runs after MFC's LoadFrame has installed the
	// stock IDR_MAINFRAME accel via LoadAccelTable. rebuild_accel destroys
	// that and swaps in our keybinds-driven table.
	rebuild_accel();
	return 0;
}

LRESULT CMainFrame::OnThemeRebuildMenu(WPARAM, LPARAM)
{
	rebuild_menu_owner_draw();
	return 0;
}

// Posted from CXCCMixerView::OnPopupOpenWith after SHOpenWithDialog returns.
// Inline reapply doesn't work — the picker keeps draining its own dismiss
// paints from the queue *after* control returns to us, so an immediate
// RedrawWindow gets stomped by stragglers from the picker's WM_DESTROY chain.
// Posting defers the reapply past those, into the next idle of our message
// loop, where the listview is finally settled and our paint sticks.
LRESULT CMainFrame::OnPostPickerRetheme(WPARAM, LPARAM)
{
	apply_theme_to_children();
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	return 0;
}

void CMainFrame::OnUpdateThemeLight(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(theme::get() == theme::mode_light);
}

void CMainFrame::OnUpdateThemeDark(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(theme::get() == theme::mode_dark);
}

void CMainFrame::OnUpdateThemeSystem(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(theme::get() == theme::mode_system);
}

// ---------- Settings Directory menu ----------
//
// Flips the HKCU pointer key that InitInstance reads on next launch. The
// running session keeps using the file it loaded from. A destination file
// that already exists is left alone - users who've configured the other
// location previously get their old settings back instead of fresh defaults.

namespace
{
	bool file_exists(const std::string& path)
	{
		DWORD a = ::GetFileAttributesA(path.c_str());
		return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	}

	void switch_settings_dir(CWnd* parent, e_settings_dir v)
	{
		if (settings_dir_get() == v)
			return;
		settings_dir_set(v);
		std::string dest = settings_dir_path(v);
		const char* loc = (v == settings_exe) ? "next to the executable" : "in %APPDATA%\\XCC\\Mixer";
		std::string msg = "Settings location will be ";
		msg += loc;
		msg += " on next launch.\n\nFile:\n";
		msg += dest;
		if (file_exists(dest))
			msg += "\n\nThis file already exists. It will be loaded as-is on next launch.";
		else
			msg += "\n\nNo file exists yet at this location. Defaults will apply on next launch.";
		parent->MessageBox(msg.c_str(), "Settings Directory", MB_OK | MB_ICONINFORMATION);
	}
}

void CMainFrame::OnSettingsDirAppData()
{
	switch_settings_dir(this, settings_appdata);
}

void CMainFrame::OnSettingsDirExe()
{
	switch_settings_dir(this, settings_exe);
}

void CMainFrame::OnUpdateSettingsDirAppData(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(settings_dir_get() == settings_appdata);
}

void CMainFrame::OnUpdateSettingsDirExe(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(settings_dir_get() == settings_exe);
}

void CMainFrame::OnThemeShowGrid()
{
	theme::set_show_grid(!theme::show_grid());
	if (m_left_mix_pane && m_left_mix_pane->GetSafeHwnd())
		theme::apply_grid(m_left_mix_pane->GetSafeHwnd());
	if (m_right_mix_pane && m_right_mix_pane->GetSafeHwnd())
		theme::apply_grid(m_right_mix_pane->GetSafeHwnd());
}

void CMainFrame::OnUpdateThemeShowGrid(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(theme::show_grid() ? 1 : 0);
}

void CMainFrame::OnThemeAlphaColor()
{
	CColorDialog dlg(theme::alpha_color(), CC_FULLOPEN | CC_RGBINIT, this);
	if (dlg.DoModal() != IDOK)
		return;
	theme::set_alpha_color(dlg.GetColor());
	// Repaint the file-info pane so any currently-shown image with alpha
	// re-composites against the new checker color.
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
	{
		m_file_info_pane->invalidate_player_bgra_cache();
		m_file_info_pane->Invalidate();
	}
}

void CMainFrame::OnThemeShpTransparency()
{
	theme::set_shp_transparency(!theme::shp_transparency());
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
	{
		m_file_info_pane->invalidate_player_bgra_cache();
		m_file_info_pane->Invalidate();
	}
}

void CMainFrame::OnUpdateThemeShpTransparency(CCmdUI* p)
{
	p->SetCheck(theme::shp_transparency() ? 1 : 0);
}

void CMainFrame::OnThemeUseCheckerboard()
{
	theme::set_use_checkerboard(!theme::use_checkerboard());
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
	{
		m_file_info_pane->invalidate_player_bgra_cache();
		m_file_info_pane->Invalidate();
	}
}

void CMainFrame::OnUpdateThemeUseCheckerboard(CCmdUI* p)
{
	p->SetCheck(theme::use_checkerboard() ? 1 : 0);
}

void CMainFrame::OnThemeUseExternalPrograms()
{
	theme::set_use_external_programs(!theme::use_external_programs());
}

void CMainFrame::OnUpdateThemeUseExternalPrograms(CCmdUI* p)
{
	p->SetCheck(theme::use_external_programs() ? 1 : 0);
}

void CMainFrame::set_interp(theme::interpolation v)
{
	theme::set_interp(v);
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->Invalidate();
}

void CMainFrame::OnThemeInterpNearest()  { set_interp(theme::interp_nearest); }
void CMainFrame::OnThemeInterpBilinear() { set_interp(theme::interp_bilinear); }
void CMainFrame::OnThemeInterpBicubic()  { set_interp(theme::interp_bicubic); }
void CMainFrame::OnThemeInterpLanczos()  { set_interp(theme::interp_lanczos); }

void CMainFrame::OnUpdateThemeInterpNearest(CCmdUI* p)  { p->SetCheck(theme::interp() == theme::interp_nearest); }
void CMainFrame::OnUpdateThemeInterpBilinear(CCmdUI* p) { p->SetCheck(theme::interp() == theme::interp_bilinear); }
void CMainFrame::OnUpdateThemeInterpBicubic(CCmdUI* p)  { p->SetCheck(theme::interp() == theme::interp_bicubic); }
void CMainFrame::OnUpdateThemeInterpLanczos(CCmdUI* p)  { p->SetCheck(theme::interp() == theme::interp_lanczos); }

void CMainFrame::set_sharpen(int v)
{
	theme::set_sharpen_amount(v);
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->Invalidate();
}

void CMainFrame::OnThemeSharpen0()   { set_sharpen(0); }
void CMainFrame::OnThemeSharpen25()  { set_sharpen(25); }
void CMainFrame::OnThemeSharpen50()  { set_sharpen(50); }
void CMainFrame::OnThemeSharpen75()  { set_sharpen(75); }
void CMainFrame::OnThemeSharpen100() { set_sharpen(100); }

void CMainFrame::OnUpdateThemeSharpen0(CCmdUI* p)   { p->SetCheck(theme::sharpen_amount() == 0); }
void CMainFrame::OnUpdateThemeSharpen25(CCmdUI* p)  { p->SetCheck(theme::sharpen_amount() == 25); }
void CMainFrame::OnUpdateThemeSharpen50(CCmdUI* p)  { p->SetCheck(theme::sharpen_amount() == 50); }
void CMainFrame::OnUpdateThemeSharpen75(CCmdUI* p)  { p->SetCheck(theme::sharpen_amount() == 75); }
void CMainFrame::OnUpdateThemeSharpen100(CCmdUI* p) { p->SetCheck(theme::sharpen_amount() == 100); }

void CMainFrame::set_fps_cap(int v)
{
	theme::set_frame_rate_cap(v);
	// No invalidate needed — the cap only affects future paints' pacing.
}

void CMainFrame::OnThemeFps30()        { set_fps_cap(30); }
void CMainFrame::OnThemeFps60()        { set_fps_cap(60); }
void CMainFrame::OnThemeFps120()       { set_fps_cap(120); }
void CMainFrame::OnThemeFpsUnlimited() { set_fps_cap(theme::fps_unlimited_value); }

void CMainFrame::OnThemeFpsCustom()
{
	// Tiny modal dialog with a single edit field, defined in the .rc as
	// IDD_FPS_CUSTOM. Returns the user's chosen fps; -1 on cancel.
	const int current = theme::frame_rate_cap();
	int v = prompt_fps_value(this, current);
	if (v > 0) set_fps_cap(v);
}

void CMainFrame::OnUpdateThemeFps30(CCmdUI* p)        { p->SetCheck(theme::frame_rate_cap() == 30); }
void CMainFrame::OnUpdateThemeFps60(CCmdUI* p)        { p->SetCheck(theme::frame_rate_cap() == 60); }
void CMainFrame::OnUpdateThemeFps120(CCmdUI* p)       { p->SetCheck(theme::frame_rate_cap() == 120); }
void CMainFrame::OnUpdateThemeFpsUnlimited(CCmdUI* p) { p->SetCheck(theme::frame_rate_cap() == theme::fps_unlimited_value); }
void CMainFrame::OnUpdateThemeFpsCustom(CCmdUI* p)
{
	// Show a check next to "Custom..." whenever the active value isn't one
	// of the four presets, so the user can tell at a glance which mode they're
	// in.
	const int v = theme::frame_rate_cap();
	const bool is_preset = (v == 30 || v == 60 || v == 120 || v == theme::fps_unlimited_value);
	p->SetCheck(is_preset ? FALSE : TRUE);
}

void CMainFrame::set_pane_layout(bool two)
{
	if (!m_wndSplitter.GetSafeHwnd())
	{
		m_two_panes = two;
		return;
	}
	int cur_w = 0, cur_min = 0;
	m_wndSplitter.GetColumnInfo(1, cur_w, cur_min);
	if (!two)
	{
		// Stash the current width so a later switch back to two panes
		// restores something reasonable instead of a tiny default.
		if (cur_w > 0)
			m_saved_middle_pane_w = cur_w;
		// SetColumnInfo(col, ideal=0, min=0) collapses the column. The pane's
		// HWND is still alive; the splitter just gives it zero space and the
		// file-info column to the right takes the slack on RecalcLayout.
		m_wndSplitter.SetColumnInfo(1, 0, 0);
		// Lock columns so the user can't accidentally grab the now-invisible
		// splitter handles (bars 201 and 202 sit at the same X when col 1 is
		// 0px wide). Without this, a misclick when trying to expand the left
		// listview would re-expand the middle pane.
		m_wndSplitter.set_columns_locked(true);
	}
	else
	{
		int restore = m_saved_middle_pane_w > 0 ? m_saved_middle_pane_w : 400;
		m_wndSplitter.SetColumnInfo(1, restore, 50);
		m_wndSplitter.set_columns_locked(false);
	}
	m_wndSplitter.RecalcLayout();
	// RecalcLayout resizes children but doesn't force them to repaint, so the
	// remaining panes keep their old pixels (clipped/stretched) until the next
	// real WM_PAINT. Invalidate the whole splitter subtree so the listviews
	// and the file-info pane redraw at their new sizes immediately.
	m_wndSplitter.RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	m_two_panes = two;
	AfxGetApp()->WriteProfileInt(m_reg_key, "two_panes", two ? 1 : 0);
}

void CMainFrame::OnThemePanesOne() { set_pane_layout(false); }
void CMainFrame::OnThemePanesTwo() { set_pane_layout(true); }
void CMainFrame::OnUpdateThemePanesOne(CCmdUI* p) { p->SetCheck(!m_two_panes); }
void CMainFrame::OnUpdateThemePanesTwo(CCmdUI* p) { p->SetCheck(m_two_panes); }

void CMainFrame::apply_size_format(theme::size_format v)
{
	theme::set_size_fmt(v);
	// Pane listviews read e.size_bytes through theme::format_size in OnGetdispinfo,
	// so a redraw refreshes the Size column. The file-info pane bakes the formatted
	// bytes into its draw lines, so it needs Invalidate too.
	if (m_left_mix_pane && m_left_mix_pane->GetSafeHwnd())
		m_left_mix_pane->Invalidate(FALSE);
	if (m_right_mix_pane && m_right_mix_pane->GetSafeHwnd())
		m_right_mix_pane->Invalidate(FALSE);
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->Invalidate(FALSE);
}

void CMainFrame::OnThemeSizeFormatAuto()  { apply_size_format(theme::size_auto); }
void CMainFrame::OnThemeSizeFormatBytes() { apply_size_format(theme::size_bytes); }

void CMainFrame::OnUpdateThemeSizeFormatAuto(CCmdUI* p)  { p->SetCheck(theme::size_fmt() == theme::size_auto); }
void CMainFrame::OnUpdateThemeSizeFormatBytes(CCmdUI* p) { p->SetCheck(theme::size_fmt() == theme::size_bytes); }

void CMainFrame::apply_vxl_ss(theme::vxl_ss v)
{
	theme::set_vxl_supersample(v);
	// Force the file pane to repaint so a viewed VXL re-rasterizes at the new
	// supersample factor.
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->Invalidate(FALSE);
}

void CMainFrame::OnThemeVxlSsOff() { apply_vxl_ss(theme::vxl_ss_off); }
void CMainFrame::OnThemeVxlSs2()   { apply_vxl_ss(theme::vxl_ss_2); }
void CMainFrame::OnThemeVxlSs4()   { apply_vxl_ss(theme::vxl_ss_4); }
void CMainFrame::OnThemeVxlSs8()   { apply_vxl_ss(theme::vxl_ss_8); }
void CMainFrame::OnThemeVxlSs16()  { apply_vxl_ss(theme::vxl_ss_16); }

void CMainFrame::OnUpdateThemeVxlSsOff(CCmdUI* p) { p->SetCheck(theme::vxl_supersample() == theme::vxl_ss_off); }
void CMainFrame::OnUpdateThemeVxlSs2(CCmdUI* p)   { p->SetCheck(theme::vxl_supersample() == theme::vxl_ss_2); }
void CMainFrame::OnUpdateThemeVxlSs4(CCmdUI* p)   { p->SetCheck(theme::vxl_supersample() == theme::vxl_ss_4); }
void CMainFrame::OnUpdateThemeVxlSs8(CCmdUI* p)   { p->SetCheck(theme::vxl_supersample() == theme::vxl_ss_8); }
void CMainFrame::OnUpdateThemeVxlSs16(CCmdUI* p)  { p->SetCheck(theme::vxl_supersample() == theme::vxl_ss_16); }

void CMainFrame::OnThemeVxlShading()
{
	theme::set_vxl_shading(!theme::vxl_shading());
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->Invalidate(FALSE);
}

void CMainFrame::OnUpdateThemeVxlShading(CCmdUI* p) { p->SetCheck(theme::vxl_shading()); }

void CMainFrame::OnThemeVxlLighting()
{
	if (!m_vxl_lighting_dlg)
	{
		m_vxl_lighting_dlg = std::make_unique<CVxlLightingDlg>(this);
		if (!m_vxl_lighting_dlg->Create(IDD_VXL_LIGHTING, this))
		{
			m_vxl_lighting_dlg.reset();
			return;
		}
	}
	m_vxl_lighting_dlg->ShowWindow(SW_SHOW);
	m_vxl_lighting_dlg->SetForegroundWindow();
}

void CMainFrame::invalidate_file_info_pane()
{
	// Route through request_repaint so high-rate callers (slider drag) are
	// rate-limited at the invalidate source rather than producing a flood
	// of WM_PAINT dispatches that the OnPaint cap then has to swallow.
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->request_repaint(nullptr);
}

bool CMainFrame::throttle_input_tick()
{
	// Forward to the file view's throttle so the lighting dialog (and any
	// other high-rate input source) can use the same gate as orbit drag.
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		return m_file_info_pane->throttle_input_tick();
	return true;
}

void CMainFrame::set_file_view_interactive_low_ss(bool on)
{
	// Toggle the file view's interactive low-SS flag. Called by the VXL
	// Lighting dialog so slider drag renders at SS=1 (cheap), and the
	// final release paints at the user's chosen SS.
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->set_interactive_low_ss(on);
}


void CMainFrame::invalidate_vxl_cloud_in_file_view()
{
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->invalidate_vxl_cloud();
}

void CMainFrame::OnThemeParallelExtract()
{
	theme::set_parallel_extract(!theme::parallel_extract());
}

void CMainFrame::OnUpdateThemeParallelExtract(CCmdUI* p) { p->SetCheck(theme::parallel_extract()); }

void CMainFrame::OnThemeLimitVxlCpu()
{
	theme::set_limit_vxl_cpu(!theme::limit_vxl_cpu());
}

void CMainFrame::OnUpdateThemeLimitVxlCpu(CCmdUI* p) { p->SetCheck(theme::limit_vxl_cpu()); p->Enable(FALSE); }

void CMainFrame::OnThemeVxlFullHier()
{
	theme::set_vxl_full_hierarchy(!theme::vxl_full_hierarchy());
	// Reload any currently-displayed VXL so post_open's part-discovery runs (or
	// doesn't) under the new setting. m_vxl_parts is populated only in
	// post_open, so without a reload the user would have to manually re-open
	// to see the change.
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
		m_file_info_pane->reload_current();
}

void CMainFrame::OnUpdateThemeVxlFullHier(CCmdUI* p) { p->SetCheck(theme::vxl_full_hierarchy() ? 1 : 0); }

// Owner-draw menu data: text + a flag for whether the item lives directly on
// the menu bar (top-level) vs inside a popup. The bar-vs-popup distinction
// matters for measurement: bar items shouldn't reserve the popup checkmark
// gutter, otherwise menu-bar entries (File, View, ...) get extra horizontal
// padding that's invisible in light mode but very visible in the dark
// owner-draw painter.
//
// Stored layout: [theme_menu_data header][NUL-terminated label]. theme.cpp
// reads via theme_menu_unpack() which returns the label pointer + bar flag.
struct theme_menu_data
{
	uint32_t magic;          // identifies that this dwItemData belongs to us
	uint8_t is_bar;
	uint8_t reserved[3];
	// label bytes follow immediately after this struct
};
static const uint32_t kThemeMenuMagic = 0x544D4458u; // 'XDMT' in little-endian view

static void set_menu_owner_draw(HMENU hm, bool owner_draw, bool is_bar = true)
{
	if (!hm)
		return;
	int n = ::GetMenuItemCount(hm);
	for (int i = 0; i < n; i++)
	{
		MENUITEMINFOA mii = {};
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_DATA | MIIM_SUBMENU;
		char buf[256] = {};
		mii.dwTypeData = buf;
		mii.cch = sizeof(buf) - 1;
		if (!::GetMenuItemInfoA(hm, i, TRUE, &mii))
			continue;

		if (owner_draw)
		{
			byte* stored = reinterpret_cast<byte*>(mii.dwItemData);
			theme_menu_data* hdr = reinterpret_cast<theme_menu_data*>(stored);
			bool ours = stored && hdr->magic == kThemeMenuMagic;
			if (!ours)
			{
				size_t len = strlen(buf) + 1;
				stored = new byte[sizeof(theme_menu_data) + len];
				hdr = reinterpret_cast<theme_menu_data*>(stored);
				hdr->magic = kThemeMenuMagic;
				memcpy(stored + sizeof(theme_menu_data), buf, len);
			}
			hdr->is_bar = is_bar ? 1 : 0;
			MENUITEMINFOA upd = {};
			upd.cbSize = sizeof(upd);
			upd.fMask = MIIM_FTYPE | MIIM_DATA;
			upd.fType = (mii.fType & ~MFT_STRING) | MFT_OWNERDRAW;
			upd.dwItemData = reinterpret_cast<ULONG_PTR>(stored);
			::SetMenuItemInfoA(hm, i, TRUE, &upd);
		}
		else
		{
			byte* stored = reinterpret_cast<byte*>(mii.dwItemData);
			theme_menu_data* hdr = reinterpret_cast<theme_menu_data*>(stored);
			bool ours = stored && hdr->magic == kThemeMenuMagic;
			const char* label = ours
				? reinterpret_cast<const char*>(stored + sizeof(theme_menu_data))
				: buf;
			MENUITEMINFOA upd = {};
			upd.cbSize = sizeof(upd);
			upd.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_DATA;
			upd.fType = (mii.fType & ~MFT_OWNERDRAW) | MFT_STRING;
			upd.dwTypeData = const_cast<char*>(label);
			upd.cch = static_cast<UINT>(strlen(label));
			upd.dwItemData = 0;
			::SetMenuItemInfoA(hm, i, TRUE, &upd);
			if (ours)
				delete[] stored;
		}

		// Recurse into submenus. is_bar = false from now on; only the very
		// first level (the menu bar itself) has bar items.
		if (mii.hSubMenu)
			set_menu_owner_draw(mii.hSubMenu, owner_draw, false);
	}
}

namespace theme {
	// Helpers exposed to theme.cpp's measure/draw hooks so they can read
	// the bar/popup flag and the label out of dwItemData. Defined out-of-
	// namespace below so theme.cpp doesn't need to know the struct layout.
	const char* menu_item_label(ULONG_PTR data, bool* out_is_bar);
}

const char* theme::menu_item_label(ULONG_PTR data, bool* out_is_bar)
{
	const byte* p = reinterpret_cast<const byte*>(data);
	if (!p)
	{
		if (out_is_bar) *out_is_bar = false;
		return nullptr;
	}
	const theme_menu_data* hdr = reinterpret_cast<const theme_menu_data*>(p);
	if (hdr->magic != kThemeMenuMagic)
	{
		// Backwards-compat: legacy data was just a raw NUL-terminated string.
		if (out_is_bar) *out_is_bar = false;
		return reinterpret_cast<const char*>(p);
	}
	if (out_is_bar) *out_is_bar = hdr->is_bar != 0;
	return reinterpret_cast<const char*>(p + sizeof(theme_menu_data));
}

void CMainFrame::rebuild_menu_owner_draw()
{
	HWND h = GetSafeHwnd();
	HMENU hm = ::GetMenu(h);
	if (!hm)
		return;
	// Notepad++-style menu painting:
	//   * Bar strip is painted by our UAH window subclass (theme.cpp). The
	//     subclass is window-lifetime, idempotent, and a no-op when light.
	//   * Popups are drawn natively by Windows in whichever palette
	//     SetPreferredAppMode + FlushMenuThemes have configured.
	// We do NOT flip items to MFT_OWNERDRAW anymore. But existing users may
	// have stale MFT_OWNERDRAW items from previous Mixer builds, so always
	// run the "unflip" pass to scrub them. set_menu_owner_draw(false)
	// rewrites every owner-draw item back to MFT_STRING and frees its
	// dwItemData blob — see definition above. Safe on already-string items.
	theme::install_uah_menu_subclass(h);
	set_menu_owner_draw(hm, false);
	// Clear any stale MENUINFO::hbrBack on the bar itself (previous builds
	// set this to the dark brush, which Windows would draw under the items
	// where UAH doesn't reach). Popups need no brush — native dark theme
	// paints them.
	{
		MENUINFO mi = {};
		mi.cbSize = sizeof(mi);
		mi.fMask = MIM_BACKGROUND;
		mi.hbrBack = NULL;
		::SetMenuInfo(hm, &mi);
	}
	// Force uxtheme to drop any cached popup theme data so Windows
	// re-queries SetPreferredAppMode on the next popup open. Without this,
	// dark→light leaves popups stuck on the previous palette until next
	// activation.
	HMODULE uxtheme = ::GetModuleHandleW(L"uxtheme.dll");
	if (uxtheme)
	{
		typedef void (WINAPI* PFN_FlushMenuThemes)();
		PFN_FlushMenuThemes p = reinterpret_cast<PFN_FlushMenuThemes>(
			::GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
		if (p) p();
	}
	// Detach/reattach so Windows discards any cached bar widths from the
	// previous owner-draw run. Without this, the first dark→light toggle
	// after upgrading from an owner-draw build can leave the bar at the
	// wider measurements until the user toggles again.
	::SetMenu(h, NULL);
	::SetMenu(h, hm);
	::DrawMenuBar(h);
}

void CMainFrame::apply_theme_to_children()
{
	const UINT inv_flags = RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN;

	// Subclass list-view headers once so NM_CUSTOMDRAW reflects to our themed class.
	if (!m_headers_subclassed)
	{
		if (m_left_mix_pane && m_left_mix_pane->GetSafeHwnd())
		{
			HWND hh = reinterpret_cast<HWND>(::SendMessage(m_left_mix_pane->GetSafeHwnd(), LVM_GETHEADER, 0, 0));
			if (hh) m_left_header.SubclassWindow(hh);
		}
		if (m_right_mix_pane && m_right_mix_pane->GetSafeHwnd())
		{
			HWND hh = reinterpret_cast<HWND>(::SendMessage(m_right_mix_pane->GetSafeHwnd(), LVM_GETHEADER, 0, 0));
			if (hh) m_right_header.SubclassWindow(hh);
		}
		m_headers_subclassed = true;
	}

	if (m_left_mix_pane && m_left_mix_pane->GetSafeHwnd())
	{
		theme::apply_listview(m_left_mix_pane->GetSafeHwnd());
		// apply_grid strips/adds LVS_EX_GRIDLINES per the new mode (light
		// keeps the system gridlines; dark relies on OnCustomDraw painting
		// theme::border() lines). Without this call the system gridlines
		// stayed in whatever state the previous mode set them to.
		theme::apply_grid(m_left_mix_pane->GetSafeHwnd());
		::RedrawWindow(m_left_mix_pane->GetSafeHwnd(), NULL, NULL, inv_flags);
	}
	if (m_right_mix_pane && m_right_mix_pane->GetSafeHwnd())
	{
		theme::apply_listview(m_right_mix_pane->GetSafeHwnd());
		theme::apply_grid(m_right_mix_pane->GetSafeHwnd());
		::RedrawWindow(m_right_mix_pane->GetSafeHwnd(), NULL, NULL, inv_flags);
	}
	if (m_file_info_pane && m_file_info_pane->GetSafeHwnd())
	{
		theme::apply_window(m_file_info_pane->GetSafeHwnd());
		// Re-theme the player band's child controls (Play/Pause buttons, FPS
		// edit/spin, side-color swatches, Game Grid combobox + its dropdown
		// listbox). Without this they keep whatever DarkMode_Explorer state
		// they were created with and don't follow a Light <-> Dark toggle.
		m_file_info_pane->reapply_player_theme();
		::RedrawWindow(m_file_info_pane->GetSafeHwnd(), NULL, NULL, inv_flags);
	}
	if (m_wndStatusBar.GetSafeHwnd())
	{
		theme::apply_window(m_wndStatusBar.GetSafeHwnd());
		::RedrawWindow(m_wndStatusBar.GetSafeHwnd(), NULL, NULL, inv_flags);
	}
	if (m_wndSplitter.GetSafeHwnd())
	{
		theme::apply_window(m_wndSplitter.GetSafeHwnd());
		::RedrawWindow(m_wndSplitter.GetSafeHwnd(), NULL, NULL, inv_flags);
	}
	// Re-theme any modeless dialogs that outlive the theme flip. apply_dialog
	// walks every descendant + InvalidateRect's the dialog so slider tracks,
	// edits, group boxes, and combo dropdowns all repaint in the new palette.
	// Without this, opening VXL Lighting in dark then switching to light (or
	// vice versa) left the dialog half-flipped: body honored the new mode
	// via WM_CTLCOLOR but children kept their original SetWindowTheme state.
	if (m_vxl_lighting_dlg && m_vxl_lighting_dlg->GetSafeHwnd()
		&& ::IsWindow(m_vxl_lighting_dlg->GetSafeHwnd()))
	{
		theme::apply_dialog(m_vxl_lighting_dlg->GetSafeHwnd());
		::RedrawWindow(m_vxl_lighting_dlg->GetSafeHwnd(), NULL, NULL, inv_flags);
	}
}

void CMainFrame::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMIS)
{
	if (theme::on_measure_menu_item(lpMIS))
		return;
	CFrameWnd::OnMeasureItem(nIDCtl, lpMIS);
}

void CMainFrame::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS)
{
	if (theme::on_draw_menu_item(lpDIS))
		return;
	CFrameWnd::OnDrawItem(nIDCtl, lpDIS);
}

HBRUSH CMainFrame::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (theme::is_dark())
	{
		pDC->SetTextColor(theme::text());
		pDC->SetBkColor(theme::bg());
		pDC->SetBkMode(TRANSPARENT);
		return theme::bg_brush();
	}
	return CFrameWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CMainFrame::OnEraseBkgnd(CDC* pDC)
{
	if (theme::is_dark())
	{
		CRect r;
		GetClientRect(&r);
		pDC->FillSolidRect(&r, theme::bg());
		return TRUE;
	}
	return CFrameWnd::OnEraseBkgnd(pDC);
}

void CMainFrame::rebuild_accel()
{
	HACCEL h_new = keybinds::build_accel_table();
	HACCEL h_old = m_hAccelTable;
	m_hAccelTable = h_new;
	if (h_old)
		DestroyAcceleratorTable(h_old);
}

// Recurse through every menu, rewriting each item's trailing "\t<shortcut>"
// suffix to match the current Menu-scope binding for that command id.
// Handles both plain MFT_STRING items and our owner-draw items (which keep
// the label inside the theme_menu_data blob).
static void refresh_menu_shortcuts_recursive(HMENU hm)
{
	if (!hm) return;
	int n = ::GetMenuItemCount(hm);
	for (int i = 0; i < n; i++)
	{
		MENUITEMINFOA mii = {};
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_DATA | MIIM_SUBMENU | MIIM_ID;
		char buf[256] = {};
		mii.dwTypeData = buf;
		mii.cch = sizeof(buf) - 1;
		if (!::GetMenuItemInfoA(hm, i, TRUE, &mii))
			continue;

		if (mii.hSubMenu)
			refresh_menu_shortcuts_recursive(mii.hSubMenu);

		// Skip separators and items without a real command id (popups, etc.).
		if ((mii.fType & MFT_SEPARATOR) || mii.wID == 0 || mii.hSubMenu)
			continue;

		// Resolve the current label, regardless of owner-draw state.
		bool is_owner_draw = (mii.fType & MFT_OWNERDRAW) != 0;
		std::string label;
		byte* stored = reinterpret_cast<byte*>(mii.dwItemData);
		theme_menu_data* hdr = reinterpret_cast<theme_menu_data*>(stored);
		bool ours = stored && hdr && hdr->magic == kThemeMenuMagic;
		if (is_owner_draw && ours)
			label = reinterpret_cast<const char*>(stored + sizeof(theme_menu_data));
		else
			label = buf;

		// Strip any existing "\t..." suffix; we're about to rewrite it.
		size_t tab = label.find('\t');
		if (tab != std::string::npos)
			label.erase(tab);

		std::string sc = keybinds::shortcut_for_command(mii.wID);
		if (!sc.empty())
		{
			label += '\t';
			label += sc;
		}

		// Write the new label back. For owner-draw items we replace the blob
		// (different label length needs a new allocation). For string items
		// we just push the new dwTypeData.
		if (is_owner_draw)
		{
			size_t len = label.size() + 1;
			byte* fresh = new byte[sizeof(theme_menu_data) + len];
			theme_menu_data* h2 = reinterpret_cast<theme_menu_data*>(fresh);
			h2->magic = kThemeMenuMagic;
			h2->is_bar = ours ? hdr->is_bar : 0;
			memcpy(fresh + sizeof(theme_menu_data), label.c_str(), len);
			MENUITEMINFOA upd = {};
			upd.cbSize = sizeof(upd);
			upd.fMask = MIIM_DATA;
			upd.dwItemData = reinterpret_cast<ULONG_PTR>(fresh);
			::SetMenuItemInfoA(hm, i, TRUE, &upd);
			if (ours)
				delete[] stored;
		}
		else
		{
			MENUITEMINFOA upd = {};
			upd.cbSize = sizeof(upd);
			upd.fMask = MIIM_STRING;
			upd.dwTypeData = const_cast<char*>(label.c_str());
			upd.cch = static_cast<UINT>(label.size());
			::SetMenuItemInfoA(hm, i, TRUE, &upd);
		}
	}
}

void CMainFrame::refresh_menu_shortcuts()
{
	HMENU hm = ::GetMenu(GetSafeHwnd());
	if (!hm) return;
	refresh_menu_shortcuts_recursive(hm);
	// Owner-draw widths cache stale strings; force the bar to re-measure.
	::DrawMenuBar(GetSafeHwnd());
}

void CMainFrame::OnKeybindsConfigure()
{
	CKeybindsDlg dlg(this);
	if (dlg.DoModal() == IDOK)
	{
		rebuild_accel();
		refresh_menu_shortcuts();
	}
}
