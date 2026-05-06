#include "stdafx.h"
#include "theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace theme
{
	namespace
	{
		mode g_mode = mode_light;

		// Cached brushes — recreated when the mode flips.
		HBRUSH g_brush_bg = NULL;
		HBRUSH g_brush_bg_alt = NULL;
		HBRUSH g_brush_menu_bg = NULL;
		HBRUSH g_brush_menu_hot = NULL;

		void destroy_brushes()
		{
			if (g_brush_bg) { ::DeleteObject(g_brush_bg); g_brush_bg = NULL; }
			if (g_brush_bg_alt) { ::DeleteObject(g_brush_bg_alt); g_brush_bg_alt = NULL; }
			if (g_brush_menu_bg) { ::DeleteObject(g_brush_menu_bg); g_brush_menu_bg = NULL; }
			if (g_brush_menu_hot) { ::DeleteObject(g_brush_menu_hot); g_brush_menu_hot = NULL; }
		}

		void create_brushes()
		{
			destroy_brushes();
			g_brush_bg = ::CreateSolidBrush(bg());
			g_brush_bg_alt = ::CreateSolidBrush(bg_alt());
			g_brush_menu_bg = ::CreateSolidBrush(menu_bg());
			g_brush_menu_hot = ::CreateSolidBrush(menu_hot());
		}

		// uxtheme ordinals: undocumented but stable since Win10 1809+.
		// Used by File Explorer to opt windows into dark scrollbars/headers.
		using fnSetPreferredAppMode = int(WINAPI*)(int);          // ord 135
		using fnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL); // ord 133
		using fnFlushMenuThemes = void(WINAPI*)();                // ord 136

		HMODULE g_uxtheme = NULL;
		fnSetPreferredAppMode p_SetPreferredAppMode = NULL;
		fnAllowDarkModeForWindow p_AllowDarkModeForWindow = NULL;
		fnFlushMenuThemes p_FlushMenuThemes = NULL;
		bool g_uxtheme_loaded = false;

		void load_uxtheme()
		{
			if (g_uxtheme_loaded)
				return;
			g_uxtheme_loaded = true;
			g_uxtheme = ::LoadLibraryEx("uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!g_uxtheme)
				return;
			p_SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(::GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(135)));
			p_AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(::GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(133)));
			p_FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(::GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(136)));
		}
	}

	void load()
	{
		g_mode = static_cast<mode>(AfxGetApp()->GetProfileInt("Theme", "mode", mode_light));
		create_brushes();
	}

	void save()
	{
		AfxGetApp()->WriteProfileInt("Theme", "mode", g_mode);
	}

	mode get() { return g_mode; }
	bool is_dark() { return g_mode == mode_dark; }

	void set(mode m)
	{
		if (g_mode == m)
			return;
		g_mode = m;
		create_brushes();
		save();
		apply_app_mode();
	}

	COLORREF bg()          { return RGB(0x20, 0x20, 0x20); }
	COLORREF bg_alt()      { return RGB(0x2B, 0x2B, 0x2B); }
	COLORREF text()        { return RGB(0xFF, 0xFF, 0xFF); }
	COLORREF text_dim()    { return RGB(0xC8, 0xC8, 0xC8); }
	COLORREF accent()      { return RGB(0x00, 0x78, 0xD4); }
	COLORREF accent_text() { return RGB(0xFF, 0xFF, 0xFF); }
	COLORREF border()      { return RGB(0x3A, 0x3A, 0x3A); }
	COLORREF menu_bg()     { return RGB(0x2B, 0x2B, 0x2B); }
	COLORREF menu_hot()    { return RGB(0x3D, 0x3D, 0x3D); }

	HBRUSH bg_brush()       { return g_brush_bg; }
	HBRUSH bg_alt_brush()   { return g_brush_bg_alt; }
	HBRUSH menu_bg_brush()  { return g_brush_menu_bg; }
	HBRUSH menu_hot_brush() { return g_brush_menu_hot; }

	void apply_app_mode()
	{
		load_uxtheme();
		if (p_SetPreferredAppMode)
			p_SetPreferredAppMode(is_dark() ? 2 /*ForceDark*/ : 0 /*Default*/);
		if (p_FlushMenuThemes)
			p_FlushMenuThemes();
	}

	void apply_titlebar(HWND h_top_level)
	{
		if (!h_top_level)
			return;
		BOOL dark = is_dark() ? TRUE : FALSE;
		// 20 is DWMWA_USE_IMMERSIVE_DARK_MODE on Win10 20H1+/Win11.
		// 19 is the older pre-20H1 ordinal — try both to cover older Win10 builds.
		HRESULT hr = ::DwmSetWindowAttribute(h_top_level, 20, &dark, sizeof(dark));
		if (FAILED(hr))
			::DwmSetWindowAttribute(h_top_level, 19, &dark, sizeof(dark));
	}

	void apply_window(HWND h)
	{
		if (!h)
			return;
		load_uxtheme();
		if (p_AllowDarkModeForWindow)
			p_AllowDarkModeForWindow(h, is_dark() ? TRUE : FALSE);
		// "DarkMode_Explorer" gives dark scrollbars/headers when the app mode is ForceDark.
		// Passing NULL theme in light mode restores defaults.
		if (is_dark())
			::SetWindowTheme(h, L"DarkMode_Explorer", NULL);
		else
			::SetWindowTheme(h, NULL, NULL);
	}

	void apply_listview(HWND h_listview)
	{
		if (!h_listview)
			return;
		apply_window(h_listview);
		HWND h_header = reinterpret_cast<HWND>(::SendMessage(h_listview, LVM_GETHEADER, 0, 0));
		if (h_header)
		{
			if (is_dark())
				::SetWindowTheme(h_header, L"ItemsView", NULL); // dark header look
			else
				::SetWindowTheme(h_header, NULL, NULL);
		}
		if (is_dark())
		{
			ListView_SetBkColor(h_listview, bg());
			ListView_SetTextBkColor(h_listview, bg());
			ListView_SetTextColor(h_listview, text());
		}
		else
		{
			ListView_SetBkColor(h_listview, ::GetSysColor(COLOR_WINDOW));
			ListView_SetTextBkColor(h_listview, ::GetSysColor(COLOR_WINDOW));
			ListView_SetTextColor(h_listview, ::GetSysColor(COLOR_WINDOWTEXT));
		}
		::InvalidateRect(h_listview, NULL, TRUE);
	}

	// ---------- Owner-draw menu bar ----------

	bool on_measure_menu_item(MEASUREITEMSTRUCT* mis)
	{
		if (!mis || mis->CtlType != ODT_MENU)
			return false;
		if (!is_dark())
			return false;

		HDC hdc = ::GetDC(NULL);
		HFONT hf = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
		NONCLIENTMETRICS ncm = {};
		ncm.cbSize = sizeof(ncm);
		if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
		{
			hf = ::CreateFontIndirect(&ncm.lfMenuFont);
		}
		HGDIOBJ old = ::SelectObject(hdc, hf);

		const char* text_a = reinterpret_cast<const char*>(mis->itemData);
		SIZE sz = { 0, 0 };
		if (text_a)
			::GetTextExtentPoint32(hdc, text_a, static_cast<int>(strlen(text_a)), &sz);

		mis->itemWidth = sz.cx + 16;
		mis->itemHeight = sz.cy + 6;

		::SelectObject(hdc, old);
		if (hf != ::GetStockObject(DEFAULT_GUI_FONT))
			::DeleteObject(hf);
		::ReleaseDC(NULL, hdc);
		return true;
	}

	bool on_draw_menu_item(DRAWITEMSTRUCT* dis)
	{
		if (!dis || dis->CtlType != ODT_MENU)
			return false;
		if (!is_dark())
			return false;

		HDC hdc = dis->hDC;
		RECT r = dis->rcItem;
		bool selected = (dis->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
		bool disabled = (dis->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;

		HBRUSH bgb = selected ? menu_hot_brush() : menu_bg_brush();
		::FillRect(hdc, &r, bgb);

		const char* text_a = reinterpret_cast<const char*>(dis->itemData);
		if (text_a)
		{
			::SetBkMode(hdc, TRANSPARENT);
			::SetTextColor(hdc, disabled ? text_dim() : text());

			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			HFONT hf = NULL;
			HGDIOBJ old = NULL;
			if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
			{
				hf = ::CreateFontIndirect(&ncm.lfMenuFont);
				old = ::SelectObject(hdc, hf);
			}

			RECT tr = r;
			tr.left += 8;
			::DrawTextA(hdc, text_a, -1, &tr,
				DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_HIDEPREFIX);

			if (hf)
			{
				::SelectObject(hdc, old);
				::DeleteObject(hf);
			}
		}
		return true;
	}

	void paint_menu_bar_background(HWND h_frame)
	{
		if (!is_dark() || !h_frame)
			return;
		// Repaint the 1px-ish gap below the menu strip that Windows draws in the
		// non-client area. We force a non-client redraw; the WM_NCPAINT handler
		// in the frame fills it.
		::SetWindowPos(h_frame, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}
