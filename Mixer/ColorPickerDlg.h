#pragma once

#include "resource.h"

// Minimal color picker for Mixer. Replaces Win32 CColorDialog for the three
// Mixer call-sites (Theme → Alpha Color, SHP custom side color, VXL custom
// side color) so users get a fully dark-themed picker instead of the Win32
// dialog (which has hardcoded light swatches and ignores theming).
//
// Layout: three R/G/B sliders + edits (0..255), three H/S/L sliders + edits
// (H 0..359°, S/L 0..100%), one hex edit (RRGGBB, no leading #), and an
// owner-draw preview swatch. All inputs are two-way bound through m_color
// (the single source of truth): any change recomputes the other groups and
// the swatch. m_updating guards against EN_CHANGE re-entry during the
// programmatic SetWindowText cascade.
class CColorPickerDlg : public CDialog
{
public:
	CColorPickerDlg(COLORREF initial, CWnd* pParent = NULL);

	enum { IDD = IDD_COLOR_PICKER };

	COLORREF color() const { return m_color; }

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();

	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnRgbEditChange(UINT id);
	afx_msg void OnRChange();
	afx_msg void OnGChange();
	afx_msg void OnBChange();
	afx_msg void OnHChange();
	afx_msg void OnSChange();
	afx_msg void OnLChange();
	afx_msg void OnHexChange();
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	// Refresh every control from m_color. skip_id stays untouched (used to
	// avoid clobbering the edit the user is mid-type in — typically the one
	// that fired EN_CHANGE).
	void refresh_all(UINT skip_id = 0);
	void redraw_preview();

	// HSL conversion. H in degrees [0,360), S/L in [0,1]. Reference:
	// CSS Color Module Level 3 (the standard "HSL" definition — same as
	// most picker UIs). Round-trips RGB↔HSL with one-int precision in
	// each channel, which is enough for slider-driven UI.
	static void rgb_to_hsl(int r, int g, int b, double& h, double& s, double& l);
	static void hsl_to_rgb(double h, double s, double l, int& r, int& g, int& b);

	COLORREF m_color = RGB(0, 0, 0);
	bool m_updating = false;
};
