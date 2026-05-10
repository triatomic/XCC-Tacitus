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
	DDX_Control(pDX, IDC_VXL_LIGHT_AZ_VALUE, m_az_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_EL_VALUE, m_el_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_AMBIENT_VALUE, m_ambient_value);
	DDX_Control(pDX, IDC_VXL_LIGHT_DIFFUSE_VALUE, m_diffuse_value);
}

BEGIN_MESSAGE_MAP(CVxlLightingDlg, CDialog)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_VXL_LIGHT_RESET, OnReset)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CVxlLightingDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_az.SetRange(0, k_steps);
	m_el.SetRange(0, k_steps);
	m_ambient.SetRange(0, k_steps);
	m_diffuse.SetRange(0, k_steps);
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
	m_tooltips.AddTool(GetDlgItem(IDC_VXL_LIGHT_RESET),
		"Restore all four sliders to their default values.");
	m_tooltips.Activate(TRUE);

	theme::apply_dialog(GetSafeHwnd());
	return TRUE;
}

BOOL CVxlLightingDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltips.GetSafeHwnd())
		m_tooltips.RelayEvent(pMsg);
	return CDialog::PreTranslateMessage(pMsg);
}

void CVxlLightingDlg::load_from_theme()
{
	m_az.SetPos(scale_az(theme::vxl_light_azimuth()));
	m_el.SetPos(scale_el(theme::vxl_light_elevation()));
	m_ambient.SetPos(scale_unit(theme::vxl_light_ambient()));
	m_diffuse.SetPos(scale_unit(theme::vxl_light_diffuse()));
	update_value_labels();
}

void CVxlLightingDlg::update_value_labels()
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.0f\xB0", theme::vxl_light_azimuth());
	m_az_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.0f\xB0", theme::vxl_light_elevation());
	m_el_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.2f", theme::vxl_light_ambient());
	m_ambient_value.SetWindowText(buf);
	std::snprintf(buf, sizeof(buf), "%.2f", theme::vxl_light_diffuse());
	m_diffuse_value.SetWindowText(buf);
}

void CVxlLightingDlg::invalidate_vxl_view()
{
	// Tell the main frame's file-info pane to repaint. The splat cache will
	// miss because theme::vxl_lighting_version() bumped, so the next paint
	// rebuilds with the new lighting.
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
	int id = pScrollBar->GetDlgCtrlID();
	switch (id)
	{
	case IDC_VXL_LIGHT_AZ_SLIDER:
		theme::set_vxl_light_azimuth(unscale_az(m_az.GetPos()));
		break;
	case IDC_VXL_LIGHT_EL_SLIDER:
		theme::set_vxl_light_elevation(unscale_el(m_el.GetPos()));
		break;
	case IDC_VXL_LIGHT_AMBIENT_SLIDER:
		theme::set_vxl_light_ambient(unscale_unit(m_ambient.GetPos()));
		break;
	case IDC_VXL_LIGHT_DIFFUSE_SLIDER:
		theme::set_vxl_light_diffuse(unscale_unit(m_diffuse.GetPos()));
		break;
	default:
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	update_value_labels();
	invalidate_vxl_view();
}

void CVxlLightingDlg::OnReset()
{
	theme::reset_vxl_lighting();
	load_from_theme();
	invalidate_vxl_view();
}

void CVxlLightingDlg::OnOK()
{
	ShowWindow(SW_HIDE);
}

void CVxlLightingDlg::OnCancel()
{
	ShowWindow(SW_HIDE);
}

HBRUSH CVxlLightingDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}
