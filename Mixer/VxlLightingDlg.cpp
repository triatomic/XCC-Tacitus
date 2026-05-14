#include "stdafx.h"
#include "VxlLightingDlg.h"

#include "MainFrm.h"
#include "XCC Mixer.h"
#include "theme.h"

#include <cstdio>

static CMainFrame* GetMainFrame()
{
	return static_cast<CMainFrame*>(AfxGetMainWnd());
}

namespace
{
	// Slider integer ranges. We use a 1000-step range for smooth dragging on
	// each parameter; conversions happen in scaled_*() / from_scaled_*().
	const int k_steps = 1000;

	int  scale_az(float v)         { return static_cast<int>((v / 360.0f) * k_steps); }
	float unscale_az(int v)        { return (static_cast<float>(v) / k_steps) * 360.0f; }
	int  scale_el(float v)         { return static_cast<int>(((v + 90.0f) / 180.0f) * k_steps); }
	float unscale_el(int v)        { return (static_cast<float>(v) / k_steps) * 180.0f - 90.0f; }
	int  scale_unit(float v)       { return static_cast<int>(v * k_steps); }
	float unscale_unit(int v)      { return static_cast<float>(v) / k_steps; }
	// Specular spans 0..5 (matches vxl-renderer's slider range).
	const float k_specular_max     = 5.0f;
	int  scale_spec(float v)       { return static_cast<int>((v / k_specular_max) * k_steps); }
	float unscale_spec(int v)      { return (static_cast<float>(v) / k_steps) * k_specular_max; }
}

CVxlLightingDlg::CVxlLightingDlg(CWnd* pParent)
	: CDialog(CVxlLightingDlg::IDD, pParent)
{
}

void CVxlLightingDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_VXL_LIGHT_AZ_SLIDER, m_az);
	DDX_Control(pDX, IDC_VXL_LIGHT_EL_SLIDER, m_el);
	DDX_Control(pDX, IDC_VXL_LIGHT_AMBIENT_SLIDER, m_ambient);
	DDX_Control(pDX, IDC_VXL_LIGHT_DIFFUSE_SLIDER, m_diffuse);
	DDX_Control(pDX, IDC_VXL_LIGHT_SPECULAR_SLIDER, m_specular);
	DDX_Control(pDX, IDC_VXL_LIGHT_AZ_VALUE, m_az_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_EL_VALUE, m_el_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_AMBIENT_VALUE, m_ambient_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_DIFFUSE_VALUE, m_diffuse_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_SPECULAR_VALUE, m_specular_value);
	DDX_Control(pDX, IDC_VXL_NORMAL_METHOD, m_method);
	DDX_Control(pDX, IDC_VXL_NORMAL_KERNEL, m_kernel);
}

BEGIN_MESSAGE_MAP(CVxlLightingDlg, CDialog)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_VXL_LIGHT_RESET, OnReset)
	ON_BN_CLICKED(IDC_VXL_NORMAL_SRC_COMPUTED, OnNormalSrcComputed)
	ON_BN_CLICKED(IDC_VXL_NORMAL_SRC_FILE, OnNormalSrcFile)
	// Clicks on the label statics route into the same radio handlers, after
	// flipping the matching radio's check state (statics don't auto-track).
	ON_STN_CLICKED(IDC_VXL_NORMAL_SRC_COMPUTED_LABEL, OnNormalSrcComputed)
	ON_STN_CLICKED(IDC_VXL_NORMAL_SRC_FILE_LABEL, OnNormalSrcFile)
	ON_CBN_SELCHANGE(IDC_VXL_NORMAL_METHOD, OnNormalMethodChanged)
	ON_CBN_SELCHANGE(IDC_VXL_NORMAL_KERNEL, OnNormalKernelChanged)
	ON_BN_CLICKED(IDC_VXL_LIGHT_VPL_FAITHFUL, OnVplFaithfulToggle)
	ON_BN_CLICKED(IDC_VXL_LIGHT_INDICATOR_OVERLAY, OnIndicatorOverlay)
	ON_BN_CLICKED(IDC_VXL_LIGHT_INDICATOR_CORNER, OnIndicatorCorner)
	ON_WM_SHOWWINDOW()
	ON_EN_KILLFOCUS(IDC_VXL_LIGHT_AZ_VALUE,       OnAzEditKillFocus)
	ON_EN_KILLFOCUS(IDC_VXL_LIGHT_EL_VALUE,       OnElEditKillFocus)
	ON_EN_KILLFOCUS(IDC_VXL_LIGHT_AMBIENT_VALUE,  OnAmbientEditKillFocus)
	ON_EN_KILLFOCUS(IDC_VXL_LIGHT_DIFFUSE_VALUE,  OnDiffuseEditKillFocus)
	ON_EN_KILLFOCUS(IDC_VXL_LIGHT_SPECULAR_VALUE, OnSpecularEditKillFocus)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CVxlLightingDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_az.SetRange(0, k_steps);
	m_el.SetRange(0, k_steps);
	m_ambient.SetRange(0, k_steps);
	m_diffuse.SetRange(0, k_steps);
	m_specular.SetRange(0, k_steps);
	// Populate the Method + Kernel comboboxes once. Order matches the enum
	// values in theme.h so the index can be passed straight through.
	m_method.AddString("Basic (6 faces)");
	m_method.AddString("Faces + edges + corners");
	m_method.AddString("Smooth gradient");
	m_kernel.AddString("3\xB3 (radius 1)");
	m_kernel.AddString("5\xB3 (radius 2)");
	load_from_theme();

	// Tooltips. Multi-line wraps via TTM_SETMAXTIPWIDTH; embedded \r\n inside
	// the text yields hard breaks. Activated last so the wrap setting applies.
	m_tooltips.Create(this, TTS_ALWAYSTIP);
	m_tooltips.SetMaxTipWidth(320);
	m_tooltips.SetDelayTime(TTDT_INITIAL, 400);
	m_tooltips.SetDelayTime(TTDT_AUTOPOP, 15000);
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_AZ_SLIDER),
		"Direction the light comes from, around the screen. "
		"0\xB0 = right, 90\xB0 = down, 180\xB0 = left, 270\xB0 = up. "
		"Default: 225\xB0 (upper-left).");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_EL_SLIDER),
		"How high the light sits above the screen plane. "
		"-90\xB0 = behind the model, 0\xB0 = level with the camera, "
		"+90\xB0 = directly in front. Default: 54.7\xB0.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_AMBIENT_SLIDER),
		"Floor brightness \x97 how lit a surface is when fully turned away "
		"from the light. 0 = pitch black on the dark side, 1 = no shading at "
		"all. Default: 0.55.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_DIFFUSE_SLIDER),
		"Shading range above ambient. Higher values = brighter highlights on "
		"faces pointing at the light, more contrast. Final brightness peaks "
		"at ambient + diffuse. Default: 0.85.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_SPECULAR_SLIDER),
		"Specular peak above ambient + diffuse on fully-lit faces. Ported "
		"from vxl-renderer's per-colorset VPL curve. 0 = strict Lambertian "
		"(no specular bump), 1.2 = vxl-renderer default. Range 0..5.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_RESET),
		"Restore all four sliders to their default values.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_NORMAL_SRC_COMPUTED),
		"Compute normals from voxel neighborhood (6-neighbor empty sides). "
		"Smooth, view-independent of the file's stored normals.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_NORMAL_SRC_FILE),
		"Use the on-disk Westwood normal index per voxel (TS uses 36 normals, "
		"RA2/YR uses 244). The same normals the original engine used to shade "
		"these units. Will rebuild the voxel cloud when switched.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_NORMAL_METHOD),
		"Algorithm used to compute per-voxel normals.\r\n"
		"  Basic (6 faces) \x97 legacy face-neighbor sum (~7 directions, faceted).\r\n"
		"  Faces + edges + corners \x97 26-neighbor weighted sum (~26 directions).\r\n"
		"  Smooth gradient \x97 Gaussian-blurred density field gradient. Continuous "
		"directions, smooth curved hulls. Recommended.");
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_NORMAL_KERNEL),
		"Gaussian kernel size for Smooth gradient. 3\xB3 preserves thin features "
		"like antennas and barrels. 5\xB3 smooths blocky hulls more aggressively "
		"but can over-soften small features. Only used when Method = Smooth "
		"gradient.");
	m_tooltips.Activate(TRUE);

	theme::apply_dialog(GetSafeHwnd());
	return TRUE;
}

BOOL CVxlLightingDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltips.GetSafeHwnd())
		m_tooltips.RelayEvent(pMsg);
	// Enter key in any of the four lighting edit boxes commits the typed
	// value (parse + clamp + write to theme + snap the slider). Without
	// this, Enter would route to the default button (Close) and dismiss
	// the dialog before the typed value is read. We swallow the message
	// so MFC doesn't continue to IDOK dispatch.
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
	{
		const HWND focus = ::GetFocus();
		if      (focus == m_az_value.GetSafeHwnd())       { commit_az_edit();       return TRUE; }
		else if (focus == m_el_value.GetSafeHwnd())       { commit_el_edit();       return TRUE; }
		else if (focus == m_ambient_value.GetSafeHwnd())  { commit_ambient_edit();  return TRUE; }
		else if (focus == m_diffuse_value.GetSafeHwnd())  { commit_diffuse_edit();  return TRUE; }
		else if (focus == m_specular_value.GetSafeHwnd()) { commit_specular_edit(); return TRUE; }
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CVxlLightingDlg::load_from_theme()
{
	m_az.SetPos(scale_az(theme::vxl_light_azimuth()));
	m_el.SetPos(scale_el(theme::vxl_light_elevation()));
	m_ambient.SetPos(scale_unit(theme::vxl_light_ambient()));
	m_diffuse.SetPos(scale_unit(theme::vxl_light_diffuse()));
	m_specular.SetPos(scale_spec(theme::vxl_light_specular()));
	// Sync the normal-source radios with the persisted value.
	const bool is_file = theme::vxl_normal_src() == theme::vxl_normals_file;
	CheckDlgButton(IDC_VXL_NORMAL_SRC_COMPUTED, is_file ? BST_UNCHECKED : BST_CHECKED);
	CheckDlgButton(IDC_VXL_NORMAL_SRC_FILE,     is_file ? BST_CHECKED   : BST_UNCHECKED);
	m_method.SetCurSel(static_cast<int>(theme::vxl_normals_method()));
	m_kernel.SetCurSel(static_cast<int>(theme::vxl_normals_kernel()));
	const bool overlay_mode = theme::vxl_light_indicator_mode() == theme::vxl_light_indicator_overlay;
	CheckDlgButton(IDC_VXL_LIGHT_INDICATOR_OVERLAY, overlay_mode ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_VXL_LIGHT_INDICATOR_CORNER, overlay_mode ? BST_UNCHECKED : BST_CHECKED);
	CheckDlgButton(IDC_VXL_LIGHT_VPL_FAITHFUL, theme::vxl_vpl_engine_faithful() ? BST_CHECKED : BST_UNCHECKED);
	update_ambient_diffuse_enable();
	update_computed_combos_enable();
	update_value_labels();
}

void CVxlLightingDlg::update_ambient_diffuse_enable()
{
	// Engine-faithful VPL ignores both sliders. Gray them + their edit boxes
	// so it's visually obvious the values aren't doing anything.
	const bool ena = !theme::vxl_vpl_engine_faithful();
	if (HWND h = m_ambient.GetSafeHwnd())          ::EnableWindow(h, ena ? TRUE : FALSE);
	if (HWND h = m_diffuse.GetSafeHwnd())          ::EnableWindow(h, ena ? TRUE : FALSE);
	if (HWND h = m_ambient_value.GetSafeHwnd())    ::EnableWindow(h, ena ? TRUE : FALSE);
	if (HWND h = m_diffuse_value.GetSafeHwnd())    ::EnableWindow(h, ena ? TRUE : FALSE);
	if (HWND h = GetDlgItem(IDC_VXL_LIGHT_AMBIENT_LABEL)->GetSafeHwnd())
		::EnableWindow(h, ena ? TRUE : FALSE);
	if (HWND h = GetDlgItem(IDC_VXL_LIGHT_DIFFUSE_LABEL)->GetSafeHwnd())
		::EnableWindow(h, ena ? TRUE : FALSE);
}

void CVxlLightingDlg::update_computed_combos_enable()
{
	const bool computed = theme::vxl_normal_src() == theme::vxl_normals_computed;
	const bool gradient = theme::vxl_normals_method() == theme::vxl_method_gradient;
	if (HWND h = m_method.GetSafeHwnd())
		::EnableWindow(h, computed ? TRUE : FALSE);
	if (HWND h = m_kernel.GetSafeHwnd())
		::EnableWindow(h, (computed && gradient) ? TRUE : FALSE);
	if (HWND h = GetDlgItem(IDC_VXL_NORMAL_METHOD_LABEL)->GetSafeHwnd())
		::EnableWindow(h, computed ? TRUE : FALSE);
	if (HWND h = GetDlgItem(IDC_VXL_NORMAL_KERNEL_LABEL)->GetSafeHwnd())
		::EnableWindow(h, (computed && gradient) ? TRUE : FALSE);
}

void CVxlLightingDlg::update_value_labels()
{
	// Push the current theme:: values into the four edit boxes. The
	// m_updating_ui guard suppresses our own EN_KILLFOCUS handlers from
	// trying to parse-then-re-write the same value we just wrote (would
	// produce duplicate set_vxl_light_* calls but no actual change; mostly
	// harmless, but it's cleaner to short-circuit).
	m_updating_ui = true;
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.0f", theme::vxl_light_azimuth());
	m_az_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.0f", theme::vxl_light_elevation());
	m_el_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.2f", theme::vxl_light_ambient());
	m_ambient_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.2f", theme::vxl_light_diffuse());
	m_diffuse_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.2f", theme::vxl_light_specular());
	m_specular_value.SetWindowText(buf);
	m_updating_ui = false;
}

void CVxlLightingDlg::invalidate_vxl_view()
{
	// Repaint the file view at the user's chosen supersample factor.
	// (We previously routed through an SS=1 preview + idle upgrade, but
	// commits are now infrequent enough — mid-drag is preview-only —
	// that a single full-quality paint per commit is fine.)
	if (CMainFrame* mf = GetMainFrame())
		mf->invalidate_file_info_pane();
}

void CVxlLightingDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (!pScrollBar)
	{
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	// Continuous-commit: every slider event (including mid-drag TB_THUMBTRACK)
	// writes through to theme and triggers a repaint, so the model updates
	// live as the user drags. The splat cache invalidates per lighting_version
	// bump and the VPL section selection re-runs; mouse-input throttling via
	// CMainFrame::throttle_input_tick() (see CLAUDE.md "Frame-rate cap + paint/
	// input throttle") already drops high-poll-rate drag events to keep this
	// affordable. TB_ENDTRACK still arrives at release; no special handling
	// needed — it just commits the final value once more.
	int id = pScrollBar->GetDlgCtrlID();
	// Compute the would-be value from the slider position, regardless of
	// phase. Used either to update the edit preview (mid-drag) or to
	// commit (release).
	float preview_value = 0.0f;
	switch (id)
	{
	case IDC_VXL_LIGHT_AZ_SLIDER:       preview_value = unscale_az(m_az.GetPos()); break;
	case IDC_VXL_LIGHT_EL_SLIDER:       preview_value = unscale_el(m_el.GetPos()); break;
	case IDC_VXL_LIGHT_AMBIENT_SLIDER:  preview_value = unscale_unit(m_ambient.GetPos()); break;
	case IDC_VXL_LIGHT_DIFFUSE_SLIDER:  preview_value = unscale_unit(m_diffuse.GetPos()); break;
	case IDC_VXL_LIGHT_SPECULAR_SLIDER: preview_value = unscale_spec(m_specular.GetPos()); break;
	default:
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	// Commit: write through to theme:: and trigger a repaint. Runs on every
	// slider event so the model reflects the live slider position.
	switch (id)
	{
	case IDC_VXL_LIGHT_AZ_SLIDER:       theme::set_vxl_light_azimuth(preview_value); break;
	case IDC_VXL_LIGHT_EL_SLIDER:       theme::set_vxl_light_elevation(preview_value); break;
	case IDC_VXL_LIGHT_AMBIENT_SLIDER:  theme::set_vxl_light_ambient(preview_value); break;
	case IDC_VXL_LIGHT_DIFFUSE_SLIDER:  theme::set_vxl_light_diffuse(preview_value); break;
	case IDC_VXL_LIGHT_SPECULAR_SLIDER: theme::set_vxl_light_specular(preview_value); break;
	}
	update_value_labels();
	invalidate_vxl_view();
	// Registry save is deferred until the dialog is closed (OnOK / OnCancel),
	// not on slider release. The lighting setters keep the value in memory +
	// bump the dirty flag; flush_lighting_save() runs once at close.
}

void CVxlLightingDlg::OnReset()
{
	theme::reset_vxl_lighting();
	load_from_theme();
	invalidate_vxl_view();
}

// Edit-box commit implementations. Parse + clamp + write to theme +
// snap slider position + repaint. The m_updating_ui guard skips parsing
// when the edit was just programmatically updated by update_value_labels
// (slider drag). Each commit also re-emits the canonical formatted
// value back into the edit so "180.7" snaps to "181", "1.5" to "1.00",
// etc., giving immediate feedback on clamping.
void CVxlLightingDlg::commit_az_edit()
{
	if (m_updating_ui) return;
	CString s; m_az_value.GetWindowText(s);
	float v = static_cast<float>(_tstof(s));
	if (v < 0.0f) v = 0.0f; else if (v > 360.0f) v = 360.0f;
	theme::set_vxl_light_azimuth(v);
	m_az.SetPos(static_cast<int>((v / 360.0f) * 1000));
	update_value_labels();
	invalidate_vxl_view();
	theme::flush_lighting_save();
}

void CVxlLightingDlg::commit_el_edit()
{
	if (m_updating_ui) return;
	CString s; m_el_value.GetWindowText(s);
	float v = static_cast<float>(_tstof(s));
	if (v < -90.0f) v = -90.0f; else if (v > 90.0f) v = 90.0f;
	theme::set_vxl_light_elevation(v);
	m_el.SetPos(static_cast<int>(((v + 90.0f) / 180.0f) * 1000));
	update_value_labels();
	invalidate_vxl_view();
	theme::flush_lighting_save();
}

void CVxlLightingDlg::commit_ambient_edit()
{
	if (m_updating_ui) return;
	CString s; m_ambient_value.GetWindowText(s);
	float v = static_cast<float>(_tstof(s));
	if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
	theme::set_vxl_light_ambient(v);
	m_ambient.SetPos(static_cast<int>(v * 1000));
	update_value_labels();
	invalidate_vxl_view();
	theme::flush_lighting_save();
}

void CVxlLightingDlg::commit_diffuse_edit()
{
	if (m_updating_ui) return;
	CString s; m_diffuse_value.GetWindowText(s);
	float v = static_cast<float>(_tstof(s));
	if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
	theme::set_vxl_light_diffuse(v);
	m_diffuse.SetPos(static_cast<int>(v * 1000));
	update_value_labels();
	invalidate_vxl_view();
	theme::flush_lighting_save();
}

void CVxlLightingDlg::commit_specular_edit()
{
	if (m_updating_ui) return;
	CString s; m_specular_value.GetWindowText(s);
	float v = static_cast<float>(_tstof(s));
	if (v < 0.0f) v = 0.0f; else if (v > k_specular_max) v = k_specular_max;
	theme::set_vxl_light_specular(v);
	m_specular.SetPos(scale_spec(v));
	update_value_labels();
	invalidate_vxl_view();
	theme::flush_lighting_save();
}

void CVxlLightingDlg::OnAzEditKillFocus()       { commit_az_edit(); }
void CVxlLightingDlg::OnElEditKillFocus()       { commit_el_edit(); }
void CVxlLightingDlg::OnAmbientEditKillFocus()  { commit_ambient_edit(); }
void CVxlLightingDlg::OnDiffuseEditKillFocus()  { commit_diffuse_edit(); }
void CVxlLightingDlg::OnSpecularEditKillFocus() { commit_specular_edit(); }

void CVxlLightingDlg::OnNormalSrcComputed()
{
	// Sync radio visual state (no-op when called from the radio itself;
	// needed when the label static fires STN_CLICKED, since statics don't
	// auto-track sibling radios).
	CheckDlgButton(IDC_VXL_NORMAL_SRC_COMPUTED, BST_CHECKED);
	CheckDlgButton(IDC_VXL_NORMAL_SRC_FILE,     BST_UNCHECKED);
	theme::set_vxl_normal_src(theme::vxl_normals_computed);
	update_computed_combos_enable();
	// Normal source affects the voxel cloud itself (not just the splat
	// cache), so we have to drop+rebuild the cloud rather than just
	// invalidate the splat.
	if (CMainFrame* mf = GetMainFrame())
		mf->invalidate_vxl_cloud_in_file_view();
}

void CVxlLightingDlg::OnNormalSrcFile()
{
	CheckDlgButton(IDC_VXL_NORMAL_SRC_COMPUTED, BST_UNCHECKED);
	CheckDlgButton(IDC_VXL_NORMAL_SRC_FILE,     BST_CHECKED);
	theme::set_vxl_normal_src(theme::vxl_normals_file);
	update_computed_combos_enable();
	if (CMainFrame* mf = GetMainFrame())
		mf->invalidate_vxl_cloud_in_file_view();
}

void CVxlLightingDlg::OnNormalMethodChanged()
{
	const int sel = m_method.GetCurSel();
	if (sel < 0) return;
	theme::set_vxl_normals_method(static_cast<theme::vxl_normal_method>(sel));
	update_computed_combos_enable();
	// Method also bakes into the cloud — rebuild rather than splat-invalidate.
	if (CMainFrame* mf = GetMainFrame())
		mf->invalidate_vxl_cloud_in_file_view();
}

void CVxlLightingDlg::OnNormalKernelChanged()
{
	const int sel = m_kernel.GetCurSel();
	if (sel < 0) return;
	theme::set_vxl_normals_kernel(static_cast<theme::vxl_normal_kernel>(sel));
	// Kernel only matters when method == gradient, but cheaper to always
	// rebuild than to inspect — the dialog enable state already gates it.
	if (CMainFrame* mf = GetMainFrame())
		mf->invalidate_vxl_cloud_in_file_view();
}

void CVxlLightingDlg::OnVplFaithfulToggle()
{
	const bool checked = IsDlgButtonChecked(IDC_VXL_LIGHT_VPL_FAITHFUL) == BST_CHECKED;
	theme::set_vxl_vpl_engine_faithful(checked);
	update_ambient_diffuse_enable();
	invalidate_vxl_view();
	theme::flush_lighting_save();
}

void CVxlLightingDlg::OnIndicatorOverlay()
{
	theme::set_vxl_light_indicator_mode(theme::vxl_light_indicator_overlay);
	CheckDlgButton(IDC_VXL_LIGHT_INDICATOR_OVERLAY, BST_CHECKED);
	CheckDlgButton(IDC_VXL_LIGHT_INDICATOR_CORNER, BST_UNCHECKED);
	invalidate_vxl_view();
}

void CVxlLightingDlg::OnIndicatorCorner()
{
	theme::set_vxl_light_indicator_mode(theme::vxl_light_indicator_corner);
	CheckDlgButton(IDC_VXL_LIGHT_INDICATOR_OVERLAY, BST_UNCHECKED);
	CheckDlgButton(IDC_VXL_LIGHT_INDICATOR_CORNER, BST_CHECKED);
	invalidate_vxl_view();
}

void CVxlLightingDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CDialog::OnShowWindow(bShow, nStatus);
	// Mirror the dialog's visibility into the runtime flag so the VXL view
	// only draws the light indicator while the dialog is on screen.
	theme::set_vxl_light_indicator_visible(bShow ? true : false);
	if (CMainFrame* mf = GetMainFrame())
		mf->invalidate_file_info_pane();
}

void CVxlLightingDlg::OnOK()
{
	theme::flush_lighting_save();
	ShowWindow(SW_HIDE);
}

void CVxlLightingDlg::OnCancel()
{
	theme::flush_lighting_save();
	ShowWindow(SW_HIDE);
}

HBRUSH CVxlLightingDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}
