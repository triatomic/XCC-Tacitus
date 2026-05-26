#pragma once

#include "XCCFileView.h"
#include "XCC MixerView.h"

#include <ddraw.h>
#include <memory>
#include <mmsystem.h>
#include <dsound.h>
#include "cc_structures.h"
#include "mix_file.h"
#include "pal_file.h"
#include "theme.h"
#include "xm_types.h"

class CVxlLightingDlg;

// Prompt for a custom fps value (1..9999). Returns the entered value on OK,
// -1 on cancel. Defined in MainFrm.cpp.
int prompt_fps_value(CWnd* parent, int initial);

struct t_pal_map_list_entry
{
	string name;
	int parent;
	// User-PAL-path priority: 0..N-1 for the N entries in PalPaths registry,
	// in dialog order. -1 for game roots and nested children (those sort
	// alphabetically as before). Lets the Select Palette tree show user
	// folders/mixes in the same order Ctrl+Q walks them.
	int order = -1;
	// True for ad-hoc roots created by the Select Palette dialog's Load Pal /
	// Load Mix buttons. reload_pal_paths() preserves these so reopening the
	// PAL Paths editor doesn't wipe session-only loads. Also the tree sorts
	// them with the registry-backed PalPaths entries, but they don't get
	// persisted to registry.
	bool session_only = false;
};

struct t_pal_list_entry
{
	string name;
	t_palette palette;
	int parent;
};

using t_mix_list = vector<string>;
using t_pal_map_list = map<int, t_pal_map_list_entry>;
using t_pal_list = vector<t_pal_list_entry>;

class CMainFrame : public CFrameWnd
{
protected: // create from serialization only
	CMainFrame();
	DECLARE_DYNCREATE(CMainFrame)

// Attributes
public:
	CXCCMixerView* left_mix_pane();
	CXCCMixerView* right_mix_pane();
	CXCCFileView* file_info_pane();

// Operations
public:
	void do_mix(Cmix_file& f, const string& mix_name, int mix_parent, int pal_parent);
	void find_mixs(const string& dir, t_game game, string filter);
	void initialize_lists();
	void launch_xtw(t_game game);

	// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMainFrame)
	public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	protected:
	virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext);
	//}}AFX_VIRTUAL
	virtual BOOL PreTranslateMessage(MSG* pMsg);

// Implementation
public:
	bool auto_select(t_game game, string palette);
	void set_palette(int id);
	void clean_pal_map_list();
	int mix_list_create_map(string name, string fname, int id, int parent);
	int pal_list_create_map(string name, int parent);
	int load_pal_folder(const string& folder);
	// Load a single archive (MIX/DAT/PAK/BIG/...) from disk and import every
	// palette entry found inside, recursing into nested archives. Returns the
	// new pal-map parent id (root tree node for the archive), or -1 if the
	// file couldn't be opened or contained no palettes.
	int load_pal_mix(const string& path);
	// Walk the user's PalPaths registry list and import every entry into
	// m_pal_list. Idempotent: clears any previously-loaded PalPath slice (the
	// portion of m_pal_list past m_pal_i[game_unknown - 1]) before reloading.
	void reload_pal_paths();
	t_pal_map_list& pal_map_list_mut() { return m_pal_map_list; }
	t_pal_list& pal_list_mut() { return m_pal_list; }
	// Current global palette index into m_pal_list; -1 means default (no
	// override). Used by the Load PAL dialog to snapshot/revert state.
	int get_palette() const { return m_palette_i; }
	BOOL OnIdle(LONG lCount);
	void close_ds();
	void open_ds();
	LPDIRECTSOUND get_ds();
	t_game get_game();
	string get_mix_name(int i) const;
	const t_palette_entry* get_game_palette(t_game game);
	const t_palette_entry* get_pal_data();
	int get_vxl_mode() const;
	void set_msg(const string& s);
	virtual ~CMainFrame();

	bool combine_shadows() const
	{
		return m_combine_shadows;
	}

	bool convert_from_td() const
	{
		return m_convert_from_td;
	}

	bool convert_from_ra() const
	{
		return m_convert_from_ra;
	}

	bool enable_compression() const
	{
		return m_enable_compression;
	}

	bool fix_shadows() const
	{
		return m_fix_shadows;
	}

	const t_mix_map_list& mix_map_list() const
	{
		return m_mix_map_list;
	}

	bool remap_team_colors() const
	{
		return m_remap_team_colors;
	}

	bool split_shadows() const
	{
		return m_split_shadows;
	}

	// Style for combined-shadow output. 0 = darken background, 1 = transparent.
	// Stored as plain int to avoid pulling shp_ts_file.h into MainFrm.h;
	// the two callers in XCC MixerView.cpp cast to Cshp_ts_file::shadow_style.
	int shadow_style() const
	{
		return m_shadow_style;
	}

	void set_shadow_style(int v)
	{
		m_shadow_style = v;
	}

	bool use_palette_for_conversion() const
	{
		return m_use_palette_for_conversion;
	}
protected:
	t_game m_game;
	t_palette m_td_palette;
	t_palette m_ra_palette;
	t_palette m_ts_palette;
	t_palette m_ra2_palette;
	int m_palette_i = -1;
	int m_vxl_mode = 0;
	bool m_lists_initialized;
	int m_mix_i[game_unknown] = { 0 };
	int m_pal_i[game_unknown] = { 0 };
	t_mix_list m_mix_list;
	t_mix_map_list m_mix_map_list;
	t_pal_map_list m_pal_map_list;
	t_pal_list m_pal_list;
	bool m_combine_shadows = false;
	bool m_convert_from_td = false;
	bool m_convert_from_ra = false;
	bool m_enable_compression = true;
	bool m_fix_shadows = false;
	bool m_remap_team_colors = false;
	bool m_split_shadows = false;
	int m_shadow_style = 0;
	bool m_use_palette_for_conversion = false;
	CXCCMixerView* m_left_mix_pane;
	CXCCMixerView* m_right_mix_pane;
	CXCCFileView* m_file_info_pane;
	// Sticky "active pane": the last mix list to hold focus. Drives the
	// accent-border indicator and which pane the shared filter box acts on.
	// Set via set_active_pane() from CXCCMixerView::OnSetFocus; NOT changed
	// when focus moves to the filter box, so the border persists while typing.
	CXCCMixerView* m_active_pane = nullptr;
	CThemedSplitterWnd m_wndSplitter;
	// Single filter bar spanning the top of the client area, between the menu
	// bar and the panes. Child of the frame; filters whichever mix pane is
	// active (focused, else left). The frame owns layout (OnSize reserves a
	// top strip for it and shrinks the splitter below).
	CEdit m_filter_edit;
	CThemedStatusBar m_wndStatusBar;
	CThemedHeaderCtrl m_left_header;
	CThemedHeaderCtrl m_right_header;
	bool m_headers_subclassed = false;
	LPDIRECTDRAW m_dd = NULL;
	LPDIRECTSOUND m_ds = NULL;
	CString m_reg_key = "MainFrame";
	bool m_two_panes = true;
	int m_saved_middle_pane_w = 0; // restored when going one→two
	std::unique_ptr<CVxlLightingDlg> m_vxl_lighting_dlg;
public:
	void set_pane_layout(bool two_panes);
	// Walk every themed child window and re-apply dark-mode state. Public
	// because the listview's "Open With..." path needs to call this after
	// SHOpenWithDialog returns — the picker invalidates our process-wide
	// theme cache and we need to put dark mode back together.
	void apply_theme_to_children();
	// Refresh the shared filter edit from the active pane's filter text (e.g.
	// after a pane navigates and clears its filter). Public: the mix view calls
	// it from update_list. Safe before the edit exists.
	void sync_filter_ui();
	// Mark a mix pane as the active one (called when a list gains focus). Repaints
	// both panes' borders so the indicator moves. Ignores null / no-op repeats.
	void set_active_pane(CXCCMixerView* pane);
	// True if the given pane is the sticky active pane. Used by the pane's
	// non-client border paint. Defaults to the left pane until one gains focus.
	bool is_active_pane(const CXCCMixerView* pane) const;
	// Trigger a repaint of the right-hand file-info pane. Used by the VXL
	// Lighting dialog to refresh the splat after sliders change.
	void invalidate_file_info_pane();
	bool throttle_input_tick();
	void set_file_view_interactive_low_ss(bool on);
	// Drop+rebuild the VXL point cloud in the file-info pane. Used by the
	// VXL Lighting dialog when normal source flips between Computed/File.
	void invalidate_vxl_cloud_in_file_view();
	// VPL master on/off routed to the file-info pane. Used by the VXL Lighting
	// dialog's "Use VPL engine formula" checkbox: unchecking clears the VPL
	// (synthetic shading), re-checking re-runs auto-detection. clear_vpl_in_file_view
	// returns whether anything was using a VPL; reload returns whether one was found.
	void clear_vpl_in_file_view();
	bool reload_vpl_in_file_view();

protected:
	// Filter-bar plumbing. layout_filter_bar() positions the edit + shrinks the
	// splitter; active_mix_pane() picks the focused pane (left fallback).
	// sync_filter_ui() is declared public above (the mix view calls it).
	void layout_filter_bar();
	CXCCMixerView* active_mix_pane() const;
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnFilterChange();
	afx_msg void OnViewGameTD();
	afx_msg void OnViewGameRA();
	afx_msg void OnViewGameTS();
	afx_msg void OnViewGameRA2();
	afx_msg void OnUpdateViewGameTD(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewGameRA(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewGameTS(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewGameRA2(CCmdUI* pCmdUI);
	afx_msg void OnUpdateFileFoundUpdate(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewPaletteUpdate(CCmdUI* pCmdUI);
	afx_msg void OnViewGameAuto();
	afx_msg void OnUpdateViewGameAuto(CCmdUI* pCmdUI);
	afx_msg void OnViewPaletteAuto();
	afx_msg void OnUpdateViewPaletteAuto(CCmdUI* pCmdUI);
	afx_msg void OnViewPalettePrev();
	afx_msg void OnViewPaletteNext();
	afx_msg void OnViewPalettePrevSibling();
	afx_msg void OnViewPaletteNextSibling();
	afx_msg void OnViewPaletteUseForConversion();
	afx_msg void OnUpdateViewPaletteUseForConversion(CCmdUI* pCmdUI);
	afx_msg void OnViewPaletteConvertFromTD();
	afx_msg void OnUpdateViewPaletteConvertFromTD(CCmdUI* pCmdUI);
	afx_msg void OnViewPaletteConvertFromRA();
	afx_msg void OnUpdateViewPaletteConvertFromRA(CCmdUI* pCmdUI);
	afx_msg void OnViewVoxelNormal();
	afx_msg void OnUpdateViewVoxelNormal(CCmdUI* pCmdUI);
	afx_msg void OnViewVoxelSurfaceNormals();
	afx_msg void OnUpdateViewVoxelSurfaceNormals(CCmdUI* pCmdUI);
	afx_msg void OnViewVoxelDepthInformation();
	afx_msg void OnUpdateViewVoxelDepthInformation(CCmdUI* pCmdUI);
	afx_msg void OnViewVoxelTest();
	afx_msg void OnUpdateViewVoxelTest(CCmdUI* pCmdUI);
	afx_msg void OnConversionSplitShadows();
	afx_msg void OnUpdateConversionSplitShadows(CCmdUI* pCmdUI);
	afx_msg void OnUtilitiesXccAvPlayer();
	afx_msg void OnUpdateUtilitiesXccAvPlayer(CCmdUI* pCmdUI);
	afx_msg void OnUtilitiesXccEditor();
	afx_msg void OnUpdateUtilitiesXccEditor(CCmdUI* pCmdUI);
	afx_msg void OnUtilitiesXccMixEditor();
	afx_msg void OnUpdateUtilitiesXccMixEditor(CCmdUI* pCmdUI);
	afx_msg void OnViewDirectories();
	afx_msg void OnLaunchXccThemeWriter();
	afx_msg void OnUpdateLaunchXccThemeWriter(CCmdUI* pCmdUI);
	afx_msg void OnFileSearch();
	afx_msg void OnFileSearchInMix();
	afx_msg void OnFileSearchOnDisk();
	afx_msg void OnFileReloadMixDb();
	afx_msg void OnFileScreenshot();
	// Override of MFC's title formatter so the [DB: ...] suffix is reapplied
	// on every title update (document open/close, runtime DB reload). Without
	// this MFC's CFrameWnd::OnUpdateFrameTitle rebuilds the caption from
	// scratch as "<base> - <doc>" and our suffix is lost.
	virtual void OnUpdateFrameTitle(BOOL bAddToTitle);
	afx_msg void OnConversionEnableCompression();
	afx_msg void OnUpdateConversionEnableCompression(CCmdUI* pCmdUI);
	afx_msg void OnDestroy();
	afx_msg void OnLaunchXTW_TS();
	afx_msg void OnUpdateLaunchXTW_TS(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXTW_RA2();
	afx_msg void OnUpdateLaunchXTW_RA2(CCmdUI* pCmdUI);
	afx_msg void OnConversionCombineShadows();
	afx_msg void OnUpdateConversionCombineShadows(CCmdUI* pCmdUI);
	afx_msg void OnShadowDarken();
	afx_msg void OnUpdateShadowDarken(CCmdUI* pCmdUI);
	afx_msg void OnShadowTransparent();
	afx_msg void OnUpdateShadowTransparent(CCmdUI* pCmdUI);
	afx_msg void OnShadowTransparent32();
	afx_msg void OnUpdateShadowTransparent32(CCmdUI* pCmdUI);
	afx_msg void OnShadowTransparentPng();
	afx_msg void OnUpdateShadowTransparentPng(CCmdUI* pCmdUI);
	afx_msg void OnViewReport();
	afx_msg void OnUpdateViewReport(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSTE_RA2();
	afx_msg void OnUpdateLaunchXSTE_RA2(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSTE_RA2_YR();
	afx_msg void OnUpdateLaunchXSTE_RA2_YR(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXTW_RA2_YR();
	afx_msg void OnUpdateLaunchXTW_RA2_YR(CCmdUI* pCmdUI);
	afx_msg void OnViewPaletteSelect();
	afx_msg void OnViewPaletteAutoSelect();
	afx_msg void OnUpdateViewPaletteAutoSelect(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSTE_GR();
	afx_msg void OnUpdateLaunchXSTE_GR(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSTE_GR_ZH();
	afx_msg void OnUpdateLaunchXSTE_GR_ZH(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSTE_Open();
	afx_msg void OnConversionFixShadows();
	afx_msg void OnUpdateConversionFixShadows(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSE_RA2();
	afx_msg void OnUpdateLaunchXSE_RA2(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSE_RA2_YR();
	afx_msg void OnUpdateLaunchXSE_RA2_YR(CCmdUI* pCmdUI);
	afx_msg void OnLaunchXSE_Open();
	afx_msg void OnViewPalette(UINT ID);
	afx_msg void OnUpdateViewPalette(CCmdUI* pCmdUI);
	afx_msg void OnConversionRemapTeamColors();
	afx_msg void OnUpdateConversionRemapTeamColors(CCmdUI* pCmdUI);
	afx_msg void OnThemeLight();
	afx_msg void OnThemeDark();
	afx_msg void OnThemeSystem();
	afx_msg void OnUpdateThemeSystem(CCmdUI* pCmdUI);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg void OnSettingsDirAppData();
	afx_msg void OnSettingsDirExe();
	afx_msg void OnUpdateSettingsDirAppData(CCmdUI* pCmdUI);
	afx_msg void OnUpdateSettingsDirExe(CCmdUI* pCmdUI);
	afx_msg void OnThemeShowGrid();
	afx_msg void OnThemeShowColumnHeaders();
	afx_msg void OnThemeHideEmptyResults();
	afx_msg void OnThemeActivePaneBorder();
	afx_msg void OnThemeShowFilterBox();
	afx_msg void OnThemeAlphaColor();
	afx_msg void OnThemeShpTransparency();
	afx_msg void OnUpdateThemeShpTransparency(CCmdUI* pCmdUI);
	afx_msg void OnThemeUseCheckerboard();
	afx_msg void OnUpdateThemeUseCheckerboard(CCmdUI* pCmdUI);
	afx_msg void OnThemeUseExternalPrograms();
	afx_msg void OnUpdateThemeUseExternalPrograms(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeLight(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeDark(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeShowGrid(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeShowColumnHeaders(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeHideEmptyResults(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeActivePaneBorder(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeShowFilterBox(CCmdUI* pCmdUI);
	afx_msg void OnThemeInterpNearest();
	afx_msg void OnThemeInterpBilinear();
	afx_msg void OnThemeInterpBicubic();
	afx_msg void OnThemeInterpLanczos();
	afx_msg void OnThemeInterpScale2x();
	afx_msg void OnThemeInterpScale3x();
	afx_msg void OnThemeInterpHq2x();
	afx_msg void OnThemeInterpHq4x();
	afx_msg void OnThemeInterpXbr2x();
	afx_msg void OnThemeInterpXbr4x();
	afx_msg void OnThemeInterpNnedi2x();
	afx_msg void OnThemeInterpNnedi4x();
	afx_msg void OnUpdateThemeInterpNearest(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpBilinear(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpBicubic(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpLanczos(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpScale2x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpScale3x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpHq2x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpHq4x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpXbr2x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpXbr4x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpNnedi2x(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeInterpNnedi4x(CCmdUI* pCmdUI);
	void set_interp(theme::interpolation v);
	afx_msg void OnThemeSharpen0();
	afx_msg void OnThemeSharpen25();
	afx_msg void OnThemeSharpen50();
	afx_msg void OnThemeSharpen75();
	afx_msg void OnThemeSharpen100();
	afx_msg void OnUpdateThemeSharpen0(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeSharpen25(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeSharpen50(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeSharpen75(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeSharpen100(CCmdUI* pCmdUI);
	void set_sharpen(int v);
	afx_msg void OnThemeFps30();
	afx_msg void OnThemeFps60();
	afx_msg void OnThemeFps120();
	afx_msg void OnThemeFpsUnlimited();
	afx_msg void OnThemeFpsCustom();
	afx_msg void OnUpdateThemeFps30(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeFps60(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeFps120(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeFpsUnlimited(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeFpsCustom(CCmdUI* pCmdUI);
	void set_fps_cap(int v);
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMIS);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	void rebuild_menu_owner_draw();
	afx_msg LRESULT OnThemeRebuildMenu(WPARAM wp, LPARAM lp);
	afx_msg LRESULT OnPostPickerRetheme(WPARAM wp, LPARAM lp);
	afx_msg LRESULT OnDeferredAccelRebuild(WPARAM wp, LPARAM lp);
	afx_msg void OnThemePanesOne();
	afx_msg void OnThemePanesTwo();
	afx_msg void OnUpdateThemePanesOne(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemePanesTwo(CCmdUI* pCmdUI);
	void apply_size_format(theme::size_format v);
	afx_msg void OnThemeSizeFormatAuto();
	afx_msg void OnThemeSizeFormatBytes();
	afx_msg void OnUpdateThemeSizeFormatAuto(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeSizeFormatBytes(CCmdUI* pCmdUI);
	afx_msg void OnThemeClipboardIndexed();
	afx_msg void OnThemeClipboardRgb();
	afx_msg void OnUpdateThemeClipboardIndexed(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeClipboardRgb(CCmdUI* pCmdUI);
	void apply_vxl_ss(theme::vxl_ss v);
	afx_msg void OnThemeVxlSsOff();
	afx_msg void OnThemeVxlSs2();
	afx_msg void OnThemeVxlSs4();
	afx_msg void OnThemeVxlSs8();
	afx_msg void OnThemeVxlSs16();
	afx_msg void OnUpdateThemeVxlSsOff(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeVxlSs2(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeVxlSs4(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeVxlSs8(CCmdUI* pCmdUI);
	afx_msg void OnUpdateThemeVxlSs16(CCmdUI* pCmdUI);
	afx_msg void OnThemeVxlShading();
	afx_msg void OnUpdateThemeVxlShading(CCmdUI* pCmdUI);
	afx_msg void OnThemeVxlLighting();
	afx_msg void OnThemeParallelExtract();
	afx_msg void OnUpdateThemeParallelExtract(CCmdUI* pCmdUI);
	afx_msg void OnThemeLimitVxlCpu();
	afx_msg void OnUpdateThemeLimitVxlCpu(CCmdUI* pCmdUI);
	afx_msg void OnThemeVxlFullHier();
	afx_msg void OnUpdateThemeVxlFullHier(CCmdUI* pCmdUI);
	afx_msg void OnKeybindsConfigure();

	// Replace the frame's accelerator table with one built from current
	// keybinds. Safe to call repeatedly; frees the old table.
	void rebuild_accel();

	// Walk every menu item and rewrite its trailing "\t<shortcut>" suffix
	// to match the current Menu-scope keybinds. Run after rebuild_accel so
	// the visible label always agrees with what TranslateAccelerator sees.
	void refresh_menu_shortcuts();

	DECLARE_MESSAGE_MAP()
};
