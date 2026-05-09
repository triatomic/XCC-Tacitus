#pragma once

#include <afxwin.h>
#include <afxext.h>

// Splitter that paints its gutters with theme colors when dark mode is on.
// Drop-in replacement for CSplitterWnd.
class CThemedSplitterWnd : public CSplitterWnd
{
protected:
	void OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rect) override;
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()
};

// Header subclass that paints text with the theme color in dark mode.
// Subclass an existing list-view header by calling SubclassWindow on its HWND.
class CThemedHeaderCtrl : public CHeaderCtrl
{
protected:
	afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()
};

// Status bar that paints dark when dark mode is on.
// Drop-in replacement for CStatusBar.
class CThemedStatusBar : public CStatusBar
{
protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	DECLARE_MESSAGE_MAP()
};

namespace theme
{
	enum mode
	{
		mode_light = 0,
		mode_dark = 1,
	};

	void load();
	void save();
	mode get();
	void set(mode m);
	bool is_dark();

	bool show_grid();
	void set_show_grid(bool v);

	// When on, paletted Westwood images (SHP/PCX/CPS/WSA) treat palette index 0
	// as transparent — the engine convention. Off = paint index 0 with whatever
	// color the palette says, like older XCC builds.
	bool shp_transparency();
	void set_shp_transparency(bool v);
	// Apply the grid setting to a listview's extended style.
	void apply_grid(HWND h_listview);

	// Checkerboard for transparent-image previews. One square is the user-chosen
	// "alpha color" (default green), the other is black. 8x8 squares.
	COLORREF alpha_color();
	void set_alpha_color(COLORREF c);
	// When off, checker_b() returns alpha_color too, collapsing the
	// checkerboard to a flat single-color background. Default on.
	bool use_checkerboard();
	void set_use_checkerboard(bool v);

	// When on, double-clicking a leaf file (anything that isn't a MIX/dir
	// container) extracts it to a temp folder and shell-opens it with the
	// associated Windows program. Default off (Mixer's built-in viewers).
	bool use_external_programs();
	void set_use_external_programs(bool v);

	// Image interpolation: how StretchBlt-style scaling is performed for
	// previewed images (single-image zoom + SHP/WSA/VXL player auto-fit).
	enum interpolation
	{
		interp_nearest = 0,   // GDI COLORONCOLOR — sharp, pixel-art appropriate
		interp_bilinear = 1,  // GDI HALFTONE
		interp_bicubic = 2,   // GDI+ HighQualityBicubic
		interp_lanczos = 3,   // hand-rolled Lanczos-3 (separable)
	};
	interpolation interp();
	void set_interp(interpolation v);
	// Stretch-blit src onto dst using the configured interpolation. Source must
	// be a 32bpp top-down DIB; src_bits is the linear pixel array (only used
	// for the Lanczos path — others read from src_dc).
	void stretch_image(CDC* dst, int dx, int dy, int dw, int dh,
		CDC* src_dc, HBITMAP src_dib, const DWORD* src_bits, int sw, int sh);
	COLORREF checker_a();   // alpha_color
	COLORREF checker_b();   // black

	// Palette (only meaningful when is_dark()).
	COLORREF bg();          // window/client background
	COLORREF bg_alt();      // list row alternate / panes
	COLORREF text();        // primary text
	COLORREF text_dim();    // secondary text
	COLORREF accent();      // selection
	COLORREF accent_text(); // text on selection
	COLORREF border();      // grid/borders
	COLORREF menu_bg();     // menu bar + popup
	COLORREF menu_hot();    // hovered/selected menu item

	HBRUSH bg_brush();
	HBRUSH bg_alt_brush();
	HBRUSH menu_bg_brush();
	HBRUSH menu_hot_brush();

	// Apply per-window dark-mode treatment. Safe to call repeatedly.
	void apply_window(HWND h);                  // generic: title bar (top-level) + dark scrollbars/headers
	void apply_listview(HWND h_listview);       // dark explorer-style header + scrollbars
	void apply_titlebar(HWND h_top_level);      // DWM immersive dark title bar
	void apply_app_mode();                      // global SetPreferredAppMode

	// Apply dark-mode treatment to a whole dialog: immersive title bar,
	// dark scrollbars/headers on the dialog itself and every child window,
	// listview/header subtreatments where applicable. Safe to call
	// repeatedly; in light mode it restores defaults.
	void apply_dialog(HWND h_dialog);

	// One-stop CtlColor handler for dialogs. Call from CDialog::OnCtlColor
	// (or wherever the dialog handles WM_CTLCOLOR* / WM_CTLCOLORDLG /
	// WM_CTLCOLORSTATIC / WM_CTLCOLORBTN / WM_CTLCOLOREDIT /
	// WM_CTLCOLORLISTBOX / WM_CTLCOLORSCROLLBAR). Sets dark text/background
	// colors on the DC and returns a brush to paint the control background.
	// Returns NULL when not in dark mode — caller should fall through to the
	// default handler in that case.
	HBRUSH on_ctl_color(HDC dc, HWND hwnd_ctl, UINT msg);

	// Owner-draw helpers for the menu bar.
	// Returns true if it handled the message; caller should pass result back.
	bool on_measure_menu_item(MEASUREITEMSTRUCT* mis);
	bool on_draw_menu_item(DRAWITEMSTRUCT* dis);
	// Unwraps a stored owner-draw menu item: returns the label and writes
	// the menu-bar flag (true = top-level menu bar entry, false = popup
	// item). Implemented in MainFrm.cpp where the layout is owned.
	const char* menu_item_label(ULONG_PTR data, bool* out_is_bar);
	// Paint over the gray strip between the menu bar and the client area.
	void paint_menu_bar_background(HWND h_frame);
}
