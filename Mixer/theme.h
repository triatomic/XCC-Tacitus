#pragma once

#include <afxwin.h>

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

	// Owner-draw helpers for the menu bar.
	// Returns true if it handled the message; caller should pass result back.
	bool on_measure_menu_item(MEASUREITEMSTRUCT* mis);
	bool on_draw_menu_item(DRAWITEMSTRUCT* dis);
	// Paint over the gray strip between the menu bar and the client area.
	void paint_menu_bar_background(HWND h_frame);
}
