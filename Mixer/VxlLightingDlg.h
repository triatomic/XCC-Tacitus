#pragma once

#include "resource.h"

// Modeless dialog that exposes the VXL viewer's directional-light parameters
// (azimuth, elevation, ambient, diffuse) as live-updating sliders. Each slider
// movement writes through to theme:: and bumps theme::vxl_lighting_version(),
// which is part of the splat cache key — the next paint rebuilds with the new
// lighting. Reset returns all four to their defaults.
//
// Lifetime: created on demand from CMainFrame::OnThemeVxlLighting (Theme menu);
// stays alive across hide/show. Closing via OK/Esc/system close hides; the
// dialog is destroyed when the main frame is destroyed.
class CVxlLightingDlg : public CDialog
{
public:
	CVxlLightingDlg(CWnd* pParent = NULL);

	enum { IDD = IDD_VXL_LIGHTING };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnReset();
	afx_msg void OnNormalSrcComputed();
	afx_msg void OnNormalSrcFile();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()

private:
	void load_from_theme();
	void update_value_labels();
	void invalidate_vxl_view();

	CSliderCtrl m_az;
	CSliderCtrl m_el;
	CSliderCtrl m_ambient;
	CSliderCtrl m_diffuse;
	CStatic m_az_value;
	CStatic m_el_value;
	CStatic m_ambient_value;
	CStatic m_diffuse_value;
	CToolTipCtrl m_tooltips;
};
