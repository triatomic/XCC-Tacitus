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
	afx_msg void OnNormalMethodChanged();
	afx_msg void OnNormalKernelChanged();
	afx_msg void OnVplFaithfulToggle();
	afx_msg void OnIndicatorOverlay();
	afx_msg void OnIndicatorCorner();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	// Edit-box commit handlers. EN_KILLFOCUS fires when the user tabs away
	// or clicks elsewhere; we also intercept Enter in PreTranslateMessage
	// and route through commit_<field>() so committed-on-Enter feels
	// natural. EN_CHANGE is intentionally NOT used — that would re-parse
	// after every keystroke and snap the slider mid-typing, which feels
	// wrong (typing "180" would jump to 1, 18, 180 in quick succession).
	afx_msg void OnAzEditKillFocus();
	afx_msg void OnElEditKillFocus();
	afx_msg void OnAmbientEditKillFocus();
	afx_msg void OnDiffuseEditKillFocus();
	afx_msg void OnSpecularEditKillFocus();
	afx_msg void OnAoEnabledToggle();
	afx_msg void OnAoStrengthEditKillFocus();
	afx_msg void OnAoQualityChanged();
	afx_msg void OnAoMethodChanged();
	afx_msg void OnAoFalloffChanged();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	virtual BOOL PreTranslateMessage(MSG* pMsg);

	// Light-indicator visibility is transient feedback: shown only while the
	// user is actively changing one of the 5 light knobs (Azimuth / Elevation
	// / Diffuse / Ambient / Specular). On each knob change we call
	// show_indicator_briefly(), which flips the runtime visibility flag on
	// and (re)arms a one-shot timer; when the timer fires the indicator hides
	// again. Indicator-mode radios (overlay/corner) and normal-source / VPL
	// controls do NOT count as "knob changes" and don't re-arm the timer.
	static const UINT_PTR TIMER_INDICATOR_HIDE = 1;
	static const UINT     INDICATOR_HIDE_DELAY_MS = 1500;
	void show_indicator_briefly();

	DECLARE_MESSAGE_MAP()

private:
	void load_from_theme();
	void update_value_labels();
	void invalidate_vxl_view();

	CSliderCtrl m_az;
	CSliderCtrl m_el;
	CSliderCtrl m_ambient;
	CSliderCtrl m_diffuse;
	CSliderCtrl m_specular;
	CSliderCtrl m_ao_strength;
	CButton     m_ao_enabled;
	CEdit       m_ao_strength_value;
	CComboBox   m_ao_quality;
	CComboBox   m_ao_method;
	CComboBox   m_ao_falloff;
	CEdit m_az_value;
	CEdit m_el_value;
	CEdit m_ambient_value;
	CEdit m_diffuse_value;
	CEdit m_specular_value;
	CComboBox m_method;
	CComboBox m_kernel;
	CToolTipCtrl m_tooltips;
	// Reentrancy guard: set true while we're programmatically updating the
	// edit-box text in response to a slider movement, so the EN_KILLFOCUS /
	// commit handlers don't re-parse our own freshly-written text.
	bool m_updating_ui = false;
	// Enable/disable Method + Kernel combos based on current radio + method
	// selection. Method combo is disabled when File is selected; Kernel combo
	// is disabled when File is selected OR when Method != Smooth gradient.
	void update_computed_combos_enable();
	// Gray out Ambient + Diffuse slider/edit when "Use VPL engine formula"
	// is checked — those values are ignored by the engine-faithful path.
	void update_ambient_diffuse_enable();
	// Commit helpers: parse an edit's text, clamp to range, write to theme,
	// snap the corresponding slider position, repaint the VXL view.
	void commit_az_edit();
	void commit_el_edit();
	void commit_ambient_edit();
	void commit_diffuse_edit();
	void commit_specular_edit();
	void commit_ao_strength_edit();
	// Gray out the AO strength slider/edit when "Enable AO" is unchecked.
	void update_ao_strength_enable();
	// Quality combo is only meaningful for hemisphere; Falloff combo is
	// only meaningful for contact. Greys out the dormant one based on the
	// current method.
	void update_ao_method_controls();
};
