#include "stdafx.h"
#include "ColorPickerDlg.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

CColorPickerDlg::CColorPickerDlg(COLORREF initial, CWnd* pParent)
	: CDialog(CColorPickerDlg::IDD, pParent), m_color(initial)
{
	// Seed cached H/S from the initial color so degenerate starting points
	// (gray, white, black) still let the user pull the picker into a sensible
	// hue/saturation. For a chromatic starting color rgb_to_hsl gives a real
	// value; for a degenerate one it returns 0 and the user gets the default
	// (red, S=1.0) when they first move the H slider.
	double h, s, l;
	rgb_to_hsl(GetRValue(initial), GetGValue(initial), GetBValue(initial), h, s, l);
	m_hue_cached = h;
	m_sat_cached = (s > 0.0) ? s : 1.0;
}

void CColorPickerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CColorPickerDlg, CDialog)
	ON_WM_HSCROLL()
	ON_EN_CHANGE(IDC_CP_R_EDIT, OnRChange)
	ON_EN_CHANGE(IDC_CP_G_EDIT, OnGChange)
	ON_EN_CHANGE(IDC_CP_B_EDIT, OnBChange)
	ON_EN_CHANGE(IDC_CP_H_EDIT, OnHChange)
	ON_EN_CHANGE(IDC_CP_S_EDIT, OnSChange)
	ON_EN_CHANGE(IDC_CP_L_EDIT, OnLChange)
	ON_EN_CHANGE(IDC_CP_HEX_EDIT, OnHexChange)
	ON_WM_DRAWITEM()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

// CSS HSL. h returned in degrees [0,360); s,l in [0,1].
void CColorPickerDlg::rgb_to_hsl(int r, int g, int b, double& h, double& s, double& l)
{
	const double rn = r / 255.0;
	const double gn = g / 255.0;
	const double bn = b / 255.0;
	const double mx = (std::max)({ rn, gn, bn });
	const double mn = (std::min)({ rn, gn, bn });
	l = (mx + mn) * 0.5;
	const double d = mx - mn;
	if (d < 1e-9)
	{
		h = 0.0;
		s = 0.0;
		return;
	}
	s = (l > 0.5) ? d / (2.0 - mx - mn) : d / (mx + mn);
	double hh;
	if (mx == rn)      hh = (gn - bn) / d + (gn < bn ? 6.0 : 0.0);
	else if (mx == gn) hh = (bn - rn) / d + 2.0;
	else               hh = (rn - gn) / d + 4.0;
	h = hh * 60.0;
	if (h < 0) h += 360.0;
	if (h >= 360.0) h -= 360.0;
}

void CColorPickerDlg::hsl_to_rgb(double h, double s, double l, int& r, int& g, int& b)
{
	auto hue2rgb = [](double p, double q, double t) {
		if (t < 0) t += 1.0;
		if (t > 1) t -= 1.0;
		if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
		if (t < 1.0 / 2.0) return q;
		if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
		return p;
	};
	if (s < 1e-9)
	{
		const int v = static_cast<int>(std::lround(l * 255.0));
		r = g = b = (std::max)(0, (std::min)(255, v));
		return;
	}
	const double hn = h / 360.0;
	const double q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
	const double p = 2.0 * l - q;
	const double rn = hue2rgb(p, q, hn + 1.0 / 3.0);
	const double gn = hue2rgb(p, q, hn);
	const double bn = hue2rgb(p, q, hn - 1.0 / 3.0);
	r = (std::max)(0, (std::min)(255, static_cast<int>(std::lround(rn * 255.0))));
	g = (std::max)(0, (std::min)(255, static_cast<int>(std::lround(gn * 255.0))));
	b = (std::max)(0, (std::min)(255, static_cast<int>(std::lround(bn * 255.0))));
}

BOOL CColorPickerDlg::OnInitDialog()
{
	theme::apply_titlebar(GetSafeHwnd());
	SetRedraw(FALSE);

	CDialog::OnInitDialog();

	// Slider ranges: R/G/B 0..255, H 0..359, S/L 0..100.
	struct slider_def { UINT id; int lo; int hi; };
	const slider_def sliders[] = {
		{ IDC_CP_R_SLIDER, 0, 255 },
		{ IDC_CP_G_SLIDER, 0, 255 },
		{ IDC_CP_B_SLIDER, 0, 255 },
		{ IDC_CP_H_SLIDER, 0, 359 },
		{ IDC_CP_S_SLIDER, 0, 100 },
		{ IDC_CP_L_SLIDER, 0, 100 },
	};
	for (const auto& sd : sliders)
	{
		CSliderCtrl* s = static_cast<CSliderCtrl*>(GetDlgItem(sd.id));
		if (s) s->SetRange(sd.lo, sd.hi);
	}
	// Edit limits: 3 chars covers 0..255, 0..359, 0..100. Hex gets 6.
	for (UINT id : { IDC_CP_R_EDIT, IDC_CP_G_EDIT, IDC_CP_B_EDIT,
		IDC_CP_H_EDIT, IDC_CP_S_EDIT, IDC_CP_L_EDIT })
	{
		CEdit* e = static_cast<CEdit*>(GetDlgItem(id));
		if (e) e->SetLimitText(3);
	}
	if (CEdit* h = static_cast<CEdit*>(GetDlgItem(IDC_CP_HEX_EDIT)))
		h->SetLimitText(6);

	refresh_all();

	theme::apply_dialog(GetSafeHwnd());

	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	return TRUE;
}

void CColorPickerDlg::refresh_all(UINT skip_id /*=0*/)
{
	m_updating = true;
	const int r = GetRValue(m_color);
	const int g = GetGValue(m_color);
	const int b = GetBValue(m_color);
	double h, s, l;
	rgb_to_hsl(r, g, b, h, s, l);
	// Preserve cached H through grayscale (r==g==b) and cached S through
	// pure white/black (l==0 or l==1) — rgb_to_hsl returns 0 in those cases
	// (mathematically undefined), which would snap the H/S sliders back to 0
	// every time the user typed a value while on those axes. The cache is
	// updated by R/G/B changes and by H/S/L changes that move OUT of the
	// degenerate state.
	const bool gray = (r == g && g == b);
	const bool pure = (l <= 0.0 || l >= 1.0);
	if (gray) h = m_hue_cached; else m_hue_cached = h;
	if (pure || s == 0.0) s = m_sat_cached; else m_sat_cached = s;
	const int h_i = static_cast<int>(std::lround(h));
	const int s_i = static_cast<int>(std::lround(s * 100.0));
	const int l_i = static_cast<int>(std::lround(l * 100.0));

	struct sync { UINT slider_id; UINT edit_id; int value; };
	const sync items[] = {
		{ IDC_CP_R_SLIDER, IDC_CP_R_EDIT, r },
		{ IDC_CP_G_SLIDER, IDC_CP_G_EDIT, g },
		{ IDC_CP_B_SLIDER, IDC_CP_B_EDIT, b },
		{ IDC_CP_H_SLIDER, IDC_CP_H_EDIT, h_i },
		{ IDC_CP_S_SLIDER, IDC_CP_S_EDIT, s_i },
		{ IDC_CP_L_SLIDER, IDC_CP_L_EDIT, l_i },
	};
	for (const sync& it : items)
	{
		// Sliders are cheap to write — always update so the thumb tracks
		// even when the user is typing in the matching edit.
		if (CSliderCtrl* sld = static_cast<CSliderCtrl*>(GetDlgItem(it.slider_id)))
			sld->SetPos(it.value);
		// Skip the edit the user is actively typing in (passed via skip_id)
		// to avoid jumping the caret to the end on every keystroke.
		if (it.edit_id == skip_id) continue;
		if (CEdit* e = static_cast<CEdit*>(GetDlgItem(it.edit_id)))
		{
			char buf[8];
			_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", it.value);
			e->SetWindowTextA(buf);
		}
	}
	if (skip_id != IDC_CP_HEX_EDIT)
	{
		if (CEdit* hx = static_cast<CEdit*>(GetDlgItem(IDC_CP_HEX_EDIT)))
		{
			char buf[8];
			_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02X%02X%02X", r, g, b);
			hx->SetWindowTextA(buf);
		}
	}
	m_updating = false;
	redraw_preview();
	// Fire the optional live-preview callback. Set by callers that want the
	// main view to track the picker's current color in real time (e.g. the
	// VXL / SHP custom side-color swatches). Cancel restores the original
	// value via the caller's snapshot — picker doesn't manage that.
	if (m_on_change)
		m_on_change(m_color);
}

void CColorPickerDlg::redraw_preview()
{
	if (CWnd* p = GetDlgItem(IDC_CP_PREVIEW))
		p->Invalidate();
}

void CColorPickerDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (!pScrollBar)
	{
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	const UINT id = pScrollBar->GetDlgCtrlID();
	// CSliderCtrl sends WM_HSCROLL with pScrollBar pointing at itself, but
	// MFC types it as CScrollBar*. Re-resolve from HWND to get a real
	// CSliderCtrl interface.
	CSliderCtrl* s = static_cast<CSliderCtrl*>(GetDlgItem(id));
	if (!s)
	{
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	const int v = s->GetPos();
	int r = GetRValue(m_color);
	int g = GetGValue(m_color);
	int b = GetBValue(m_color);
	switch (id)
	{
	case IDC_CP_R_SLIDER: r = v; m_color = RGB(r, g, b); break;
	case IDC_CP_G_SLIDER: g = v; m_color = RGB(r, g, b); break;
	case IDC_CP_B_SLIDER: b = v; m_color = RGB(r, g, b); break;
	case IDC_CP_H_SLIDER:
	case IDC_CP_S_SLIDER:
	case IDC_CP_L_SLIDER:
	{
		// Use cached H/S as the basis, then apply the new slider value to its
		// channel. Same reason as the edit handlers: round-tripping through
		// rgb_to_hsl loses H/S on gray and pure white/black, leaving the H/S
		// sliders effectively dead at those colors. L is taken from RGB (always
		// well-defined).
		double h = m_hue_cached;
		double sat = m_sat_cached;
		double tmp_h, tmp_s, l;
		rgb_to_hsl(r, g, b, tmp_h, tmp_s, l);
		if (id == IDC_CP_H_SLIDER)      { h = v;          m_hue_cached = h; }
		else if (id == IDC_CP_S_SLIDER) { sat = v / 100.0; m_sat_cached = sat; }
		else                            { l = v / 100.0; }
		int nr, ng, nb;
		hsl_to_rgb(h, sat, l, nr, ng, nb);
		m_color = RGB(nr, ng, nb);
		break;
	}
	default:
		CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	refresh_all();
}

void CColorPickerDlg::OnRChange()
{
	if (m_updating) return;
	CString s;
	GetDlgItem(IDC_CP_R_EDIT)->GetWindowTextA(s);
	int v = atoi(s); if (v < 0) v = 0; if (v > 255) v = 255;
	m_color = RGB(v, GetGValue(m_color), GetBValue(m_color));
	refresh_all(IDC_CP_R_EDIT);
}

void CColorPickerDlg::OnGChange()
{
	if (m_updating) return;
	CString s;
	GetDlgItem(IDC_CP_G_EDIT)->GetWindowTextA(s);
	int v = atoi(s); if (v < 0) v = 0; if (v > 255) v = 255;
	m_color = RGB(GetRValue(m_color), v, GetBValue(m_color));
	refresh_all(IDC_CP_G_EDIT);
}

void CColorPickerDlg::OnBChange()
{
	if (m_updating) return;
	CString s;
	GetDlgItem(IDC_CP_B_EDIT)->GetWindowTextA(s);
	int v = atoi(s); if (v < 0) v = 0; if (v > 255) v = 255;
	m_color = RGB(GetRValue(m_color), GetGValue(m_color), v);
	refresh_all(IDC_CP_B_EDIT);
}

void CColorPickerDlg::OnHChange()
{
	if (m_updating) return;
	CString s;
	GetDlgItem(IDC_CP_H_EDIT)->GetWindowTextA(s);
	int v = atoi(s); if (v < 0) v = 0; if (v > 359) v = 359;
	// Use cached H/S so typing H on a gray color (where rgb_to_hsl gives S=0)
	// still produces a proper hue. L comes from RGB — it's always well-defined.
	double h, sat, l;
	rgb_to_hsl(GetRValue(m_color), GetGValue(m_color), GetBValue(m_color),
		h, sat, l);
	m_hue_cached = v;
	int nr, ng, nb;
	hsl_to_rgb(v, m_sat_cached, l, nr, ng, nb);
	m_color = RGB(nr, ng, nb);
	refresh_all(IDC_CP_H_EDIT);
}

void CColorPickerDlg::OnSChange()
{
	if (m_updating) return;
	CString s;
	GetDlgItem(IDC_CP_S_EDIT)->GetWindowTextA(s);
	int v = atoi(s); if (v < 0) v = 0; if (v > 100) v = 100;
	double h, sat, l;
	rgb_to_hsl(GetRValue(m_color), GetGValue(m_color), GetBValue(m_color),
		h, sat, l);
	m_sat_cached = v / 100.0;
	int nr, ng, nb;
	hsl_to_rgb(m_hue_cached, m_sat_cached, l, nr, ng, nb);
	m_color = RGB(nr, ng, nb);
	refresh_all(IDC_CP_S_EDIT);
}

void CColorPickerDlg::OnLChange()
{
	if (m_updating) return;
	CString s;
	GetDlgItem(IDC_CP_L_EDIT)->GetWindowTextA(s);
	int v = atoi(s); if (v < 0) v = 0; if (v > 100) v = 100;
	double h, sat, l;
	rgb_to_hsl(GetRValue(m_color), GetGValue(m_color), GetBValue(m_color),
		h, sat, l);
	int nr, ng, nb;
	hsl_to_rgb(m_hue_cached, m_sat_cached, v / 100.0, nr, ng, nb);
	m_color = RGB(nr, ng, nb);
	refresh_all(IDC_CP_L_EDIT);
}

void CColorPickerDlg::OnHexChange()
{
	if (m_updating) return;
	CEdit* h = static_cast<CEdit*>(GetDlgItem(IDC_CP_HEX_EDIT));
	if (!h) return;
	CString sval;
	h->GetWindowTextA(sval);
	const char* p = static_cast<const char*>(sval);
	while (*p == '#' || *p == ' ') p++;
	const size_t len = strlen(p);
	unsigned v = 0;
	if (len == 6 && sscanf_s(p, "%x", &v) == 1)
	{
		m_color = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
	}
	else if (len == 3 && sscanf_s(p, "%x", &v) == 1)
	{
		// Expand short form: F0A -> FF00AA.
		int r = ((v >> 8) & 0xF) * 17;
		int g = ((v >> 4) & 0xF) * 17;
		int b = (v & 0xF) * 17;
		m_color = RGB(r, g, b);
	}
	else
	{
		return; // partial input — leave other groups alone
	}
	refresh_all(IDC_CP_HEX_EDIT);
}

void CColorPickerDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS)
{
	if (nIDCtl != IDC_CP_PREVIEW || !lpDIS)
	{
		CDialog::OnDrawItem(nIDCtl, lpDIS);
		return;
	}
	HBRUSH br = ::CreateSolidBrush(m_color);
	::FillRect(lpDIS->hDC, &lpDIS->rcItem, br);
	::DeleteObject(br);
}

HBRUSH CColorPickerDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(),
		pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}
