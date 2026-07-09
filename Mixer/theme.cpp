#include "stdafx.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <vsstyle.h>

#include "UAHMenuBar.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

namespace theme
{
	namespace
	{
		mode g_mode = mode_light;
		// Resolved light/dark for the current g_mode. For mode_light/mode_dark
		// this mirrors g_mode; for mode_system it's whatever the last system
		// query returned. is_dark() reads this; set() and refresh_system_mode()
		// keep it up-to-date.
		bool g_resolved_dark = false;
		bool g_show_grid = true;
		bool g_show_column_headers = true;
		bool g_hide_empty_results = false;
		bool g_active_pane_border = true;
		bool g_show_filter_box = true;
		bool g_show_breadcrumb = true;
		int g_topbar_filter_w = 250;
		bool g_topbar_swapped = false;
		size_format g_size_fmt = size_auto;
		clipboard_format g_clipboard_fmt = clipboard_indexed;
		vxl_ss g_vxl_ss = vxl_ss_4;
		bool g_vxl_shading = false;
		vxl_normal_source g_vxl_normal_src = vxl_normals_computed;
		vxl_normal_method g_vxl_normal_method = vxl_method_basic;
		vxl_normal_kernel g_vxl_normal_kernel = vxl_kernel_3;
		// Ambient occlusion: view-independent, baked per-voxel at file load.
		// `g_vxl_ao_enabled` gates the multiply in the shade pass; the bake
		// runs unconditionally so toggling is instant.
		bool g_vxl_ao_enabled = true;
		int g_vxl_ao_strength = 60;	// 0..100
		vxl_ao_quality g_vxl_ao_quality = ao_q_high;
		// Default method is contact: free file open, immediate-neighbor AO.
		// Hemisphere is still available for users who want medium-range
		// cavity darkening at the cost of a slower bake.
		vxl_ao_method g_vxl_ao_method = ao_method_contact;
		// Only consulted in contact mode; ignored by the hemisphere path.
		vxl_ao_contact_falloff g_vxl_ao_contact_falloff = ao_contact_soft;
		// Defaults below are aligned to vxl-renderer's default light direction.
		// The renderer's D3D path uses light_direction = (0.2013022, -0.9101138,
		// -0.3621709) (d3d.h:139); its GDI software path uses the same vector with
		// +Y (gdi.h:78). Our splat dots the light against a Y-flipped stored normal
		// (cam_normal = (nrx, -nry_p, nrz_p)), so to reproduce the GDI reference's
		// dot product our light's Y must carry the opposite sign -> the D3D-signed
		// vector. Solving vxl_light_direction() (x=cos el cos az, y=cos el sin az,
		// z=-sin el) for that vector gives az=282.4721°, el=21.2336°.
		// (Previous hand-tuned default was az=225°, el=-54.7356° ->
		// (-0.40825,-0.40825,+0.81650).)
		const float k_default_az = 282.4721f;
		const float k_default_el = 21.2336f;
		// Engine-faithful light vectors (WORLD space), derived from the
		// TS/RA2 Set_Voxel_Light_Angle(45°) source: mtx.Rotate_Y(45°) * input.
		//   TS:  input (-1,0,0)            -> (-0.70711, 0,        +0.70711)
		//   RA2: input (-cos45,-cos45,0)   -> (-0.5,     -0.70711, +0.5)
		// (RA2's source literally passes DEG_TO_RAD(-40.51419) ~= -0.70711 as
		// the X/Y components — a radian-value-as-component quirk that equals
		// -cos45°.) az/el are the inverse of vxl_light_direction():
		//   az = atan2(y, x) (deg, wrapped 0..360);  el = -asin(z) (deg).
		const float k_ra2_az = 234.7356f;   // atan2(-0.70711, -0.5)
		const float k_ra2_el = -30.0f;       // -asin(0.5)
		const float k_ts_az  = 180.0f;       // atan2(0, -0.70711)
		const float k_ts_el  = -45.0f;       // -asin(0.70711)
		const float k_default_ambient = 0.55f;
		const float k_default_diffuse = 0.85f;
		// Specular default matches vxl-renderer's colorset_desc::specular
		// (mainwindow.cpp:36). Range 0..5 mirrors vxl-renderer's slider.
		const float k_default_specular = 1.2f;
		float g_vxl_light_az = k_default_az;
		float g_vxl_light_el = k_default_el;
		float g_vxl_light_ambient = k_default_ambient;
		float g_vxl_light_diffuse = k_default_diffuse;
		float g_vxl_light_specular = k_default_specular;
		int g_vxl_lighting_version = 0;
		// Set to true by any of the four lighting slider setters whenever they
		// would otherwise have called save(). flush_lighting_save() consumes
		// it. Decouples slider-drag tick rate from registry write rate.
		bool g_vxl_lighting_save_pending = false;
		// Light-direction indicator state. Visibility is a runtime flag (not
		// persisted) toggled by the lighting dialog's show/hide lifecycle.
		// Placement mode IS persisted so the user's choice sticks.
		bool g_vxl_vpl_engine_faithful = true;
		bool g_vxl_light_indicator_visible = false;
		vxl_light_indicator_placement g_vxl_light_indicator_mode = vxl_light_indicator_overlay;
		bool g_limit_vxl_cpu = false;
		bool g_vxl_full_hierarchy = true;
		bool g_vxl_zoom_aware_ss = true;
		int g_vxl_splat_pad_extra = 0;
		vxl_light_frame g_vxl_light_frame = vlf_camera_fixed;
		bool g_parallel_extract = true;
		bool g_silent_delete = false;
		bool g_auto_refresh = true;
		banner_mode g_banner_mode = banner_strip;
		bool g_shp_transparency = false;
		COLORREF g_alpha_color = RGB(0, 255, 0);
		bool g_use_checkerboard = true;
		bool g_use_external_programs = false;
		interpolation g_interp = interp_nearest;
		int g_sharpen_amount = 0;   // 0..100, post-pass strength on non-nearest stretch_image paths
		int g_fps_cap = 60;

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

		// The dark-mode uxtheme ordinals below only exist as these functions on
		// Win10 1809 (build 17763) and later. On Windows 7/8/early Win10, uxtheme
		// exports DIFFERENT internal functions at ordinals 133/135/136, so
		// GetProcAddress returns a valid-but-wrong pointer that passes the NULL
		// guards at every call site and gets called with a mismatched signature
		// -- corrupting the process (observed as an AV at CWinApp::Run on Win7).
		// Gate resolution on the real OS build via RtlGetVersion (GetVersionEx
		// lies on Win8.1+ without a manifest; RtlGetVersion reports the true build).
		bool os_supports_dark_ordinals()
		{
			using fnRtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
			HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
			if (!ntdll)
				return false;
			auto p = reinterpret_cast<fnRtlGetVersion>(
				::GetProcAddress(ntdll, "RtlGetVersion"));
			if (!p)
				return false;
			RTL_OSVERSIONINFOW vi = {};
			vi.dwOSVersionInfoSize = sizeof(vi);
			if (p(&vi) != 0)
				return false;
			// Win10 = major 10; 1809 = build 17763.
			return vi.dwMajorVersion > 10
				|| (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 17763);
		}

		void load_uxtheme()
		{
			if (g_uxtheme_loaded)
				return;
			g_uxtheme_loaded = true;
			// Only resolve the ordinals where they mean what we expect. On older
			// OSes leave the pointers NULL so every call site no-ops (no dark mode
			// there anyway) instead of calling the wrong function.
			if (!os_supports_dark_ordinals())
				return;
			g_uxtheme = ::LoadLibraryEx("uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!g_uxtheme)
				return;
			p_SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(::GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(135)));
			p_AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(::GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(133)));
			p_FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(::GetProcAddress(g_uxtheme, MAKEINTRESOURCEA(136)));
		}
	}

	bool system_prefers_dark()
	{
		// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
		// AppsUseLightTheme = 0 → dark, 1 → light. Absent on Win7/early Win10
		// builds; treat that as light.
		HKEY hk = NULL;
		if (::RegOpenKeyExA(HKEY_CURRENT_USER,
			"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
			return false;
		DWORD v = 1, cb = sizeof(v), type = 0;
		bool dark = false;
		if (::RegQueryValueExA(hk, "AppsUseLightTheme", NULL, &type,
				reinterpret_cast<BYTE*>(&v), &cb) == ERROR_SUCCESS
			&& type == REG_DWORD)
		{
			dark = (v == 0);
		}
		::RegCloseKey(hk);
		return dark;
	}

	namespace
	{
		// Computes the resolved dark/light from the user-pref g_mode.
		bool resolve_dark(mode m)
		{
			switch (m)
			{
			case mode_dark: return true;
			case mode_system: return system_prefers_dark();
			default: return false;
			}
		}
	}

	bool refresh_system_mode()
	{
		if (g_mode != mode_system)
			return false;
		bool new_dark = system_prefers_dark();
		if (new_dark == g_resolved_dark)
			return false;
		g_resolved_dark = new_dark;
		create_brushes();
		apply_app_mode();
		return true;
	}

	void load()
	{
		int mv = AfxGetApp()->GetProfileInt("Theme", "mode", mode_light);
		if (mv != mode_light && mv != mode_dark && mv != mode_system)
			mv = mode_light;
		g_mode = static_cast<mode>(mv);
		g_resolved_dark = resolve_dark(g_mode);
		g_show_grid = AfxGetApp()->GetProfileInt("Theme", "show_grid", 1) != 0;
		g_show_column_headers = AfxGetApp()->GetProfileInt("Theme", "show_column_headers", 1) != 0;
		g_hide_empty_results = AfxGetApp()->GetProfileInt("Theme", "hide_empty_results", 0) != 0;
		g_active_pane_border = AfxGetApp()->GetProfileInt("Theme", "active_pane_border", 1) != 0;
		g_show_filter_box = AfxGetApp()->GetProfileInt("Theme", "show_filter_box", 1) != 0;
		g_show_breadcrumb = AfxGetApp()->GetProfileInt("Theme", "show_breadcrumb", 1) != 0;
		g_topbar_filter_w = AfxGetApp()->GetProfileInt("Theme", "topbar_filter_w", 250);
		g_topbar_swapped = AfxGetApp()->GetProfileInt("Theme", "topbar_swapped", 0) != 0;
		g_shp_transparency = AfxGetApp()->GetProfileInt("Theme", "shp_transparency", 0) != 0;
		g_alpha_color = static_cast<COLORREF>(AfxGetApp()->GetProfileInt("Theme", "alpha_color", RGB(0, 255, 0)));
		g_use_checkerboard = AfxGetApp()->GetProfileInt("Theme", "use_checkerboard", 1) != 0;
		g_use_external_programs = AfxGetApp()->GetProfileInt("Theme", "use_external_programs", 0) != 0;
		int iv = AfxGetApp()->GetProfileInt("Theme", "interpolation", interp_nearest);
		// Out-of-range falls back to nearest. Range now includes the pixel-art
		// upscalers (Scale2x/3x, HQ2x/4x, xBR2x/4x — values 4..9) plus the
		// neural NNEDI3 variants (10..11).
		if (iv < interp_nearest || iv > interp_nnedi4x) iv = interp_nearest;
		g_interp = static_cast<interpolation>(iv);
		int sa = AfxGetApp()->GetProfileInt("Theme", "sharpen_amount", 0);
		if (sa < 0) sa = 0; else if (sa > 100) sa = 100;
		g_sharpen_amount = sa;
		int fc = AfxGetApp()->GetProfileInt("Theme", "fps_cap", 60);
		if (fc < 1) fc = 1; else if (fc > 9999) fc = 9999;
		g_fps_cap = fc;
		int sf = AfxGetApp()->GetProfileInt("Theme", "size_format", size_auto);
		if (sf != size_auto && sf != size_bytes) sf = size_auto;
		g_size_fmt = static_cast<size_format>(sf);
		int cf = AfxGetApp()->GetProfileInt("Theme", "clipboard_format", clipboard_indexed);
		if (cf != clipboard_indexed && cf != clipboard_rgb) cf = clipboard_indexed;
		g_clipboard_fmt = static_cast<clipboard_format>(cf);
		int ss = AfxGetApp()->GetProfileInt("Theme", "vxl_supersample", vxl_ss_4);
		if (ss != vxl_ss_off && ss != vxl_ss_2 && ss != vxl_ss_4 && ss != vxl_ss_8 && ss != vxl_ss_16)
			ss = vxl_ss_4;
		g_vxl_ss = static_cast<vxl_ss>(ss);
		g_vxl_shading = AfxGetApp()->GetProfileInt("Theme", "vxl_shading", 0) != 0;
		int ns = AfxGetApp()->GetProfileInt("Theme", "vxl_normal_src", vxl_normals_computed);
		if (ns != vxl_normals_computed && ns != vxl_normals_file) ns = vxl_normals_computed;
		g_vxl_normal_src = static_cast<vxl_normal_source>(ns);
		int nm = AfxGetApp()->GetProfileInt("Theme", "vxl_normal_method", vxl_method_basic);
		if (nm < vxl_method_basic || nm > vxl_method_gradient) nm = vxl_method_basic;
		g_vxl_normal_method = static_cast<vxl_normal_method>(nm);
		int nk = AfxGetApp()->GetProfileInt("Theme", "vxl_normal_kernel", vxl_kernel_3);
		if (nk != vxl_kernel_3 && nk != vxl_kernel_5) nk = vxl_kernel_3;
		g_vxl_normal_kernel = static_cast<vxl_normal_kernel>(nk);
		g_vxl_ao_enabled = AfxGetApp()->GetProfileInt("Theme", "vxl_ao_enabled", 1) != 0;
		{
			int aos = AfxGetApp()->GetProfileInt("Theme", "vxl_ao_strength", 60);
			if (aos < 0) aos = 0; else if (aos > 100) aos = 100;
			g_vxl_ao_strength = aos;
		}
		{
			int aoq = AfxGetApp()->GetProfileInt("Theme", "vxl_ao_quality", ao_q_high);
			if (aoq < ao_q_low || aoq > ao_q_ultra) aoq = ao_q_high;
			g_vxl_ao_quality = static_cast<vxl_ao_quality>(aoq);
		}
		{
			int aom = AfxGetApp()->GetProfileInt("Theme", "vxl_ao_method", ao_method_contact);
			if (aom != ao_method_hemisphere && aom != ao_method_contact) aom = ao_method_contact;
			g_vxl_ao_method = static_cast<vxl_ao_method>(aom);
		}
		{
			int aof = AfxGetApp()->GetProfileInt("Theme", "vxl_ao_contact_falloff", ao_contact_soft);
			if (aof != ao_contact_soft && aof != ao_contact_hard) aof = ao_contact_soft;
			g_vxl_ao_contact_falloff = static_cast<vxl_ao_contact_falloff>(aof);
		}
		// Lighting parameters: stored as int * 1000 to preserve precision via
		// the int-only WriteProfileInt API. Out-of-range values fall back to
		// defaults so corrupted/manually-edited registry entries don't render
		// black.
		auto load_f = [](const char* key, float def, float lo, float hi) -> float {
			int iv = AfxGetApp()->GetProfileInt("Theme", key, static_cast<int>(def * 1000.0f));
			float fv = iv / 1000.0f;
			if (fv < lo || fv > hi) fv = def;
			return fv;
		};
		g_vxl_light_az = load_f("vxl_light_az", k_default_az, 0.0f, 360.0f);
		g_vxl_light_el = load_f("vxl_light_el", k_default_el, -90.0f, 90.0f);
		g_vxl_light_ambient = load_f("vxl_light_ambient", k_default_ambient, 0.0f, 1.0f);
		g_vxl_light_diffuse = load_f("vxl_light_diffuse", k_default_diffuse, 0.0f, 1.0f);
		g_vxl_light_specular = load_f("vxl_light_specular", k_default_specular, 0.0f, 5.0f);
		g_vxl_vpl_engine_faithful = AfxGetApp()->GetProfileInt("Theme", "vxl_vpl_engine_faithful", 1) != 0;
		{
			int v = AfxGetApp()->GetProfileInt("Theme", "vxl_light_indicator_mode", static_cast<int>(vxl_light_indicator_overlay));
			if (v < 0 || v > 1) v = static_cast<int>(vxl_light_indicator_overlay);
			g_vxl_light_indicator_mode = static_cast<vxl_light_indicator_placement>(v);
		}
		g_limit_vxl_cpu = AfxGetApp()->GetProfileInt("Theme", "limit_vxl_cpu", 0) != 0;
		g_vxl_full_hierarchy = AfxGetApp()->GetProfileInt("Theme", "vxl_full_hierarchy", 1) != 0;
		g_vxl_zoom_aware_ss = AfxGetApp()->GetProfileInt("Theme", "vxl_zoom_aware_ss", 1) != 0;
		g_vxl_splat_pad_extra = std::clamp(static_cast<int>(AfxGetApp()->GetProfileInt("Theme", "vxl_splat_pad_extra", 0)), -64, 64);
		{
			int v = static_cast<int>(AfxGetApp()->GetProfileInt("Theme", "vxl_light_frame", vlf_camera_fixed));
			g_vxl_light_frame = (v == vlf_world_fixed) ? vlf_world_fixed : vlf_camera_fixed;
		}
		g_parallel_extract = AfxGetApp()->GetProfileInt("Theme", "parallel_extract", 1) != 0;
		g_silent_delete = AfxGetApp()->GetProfileInt("Theme", "silent_delete", 0) != 0;
		g_auto_refresh = AfxGetApp()->GetProfileInt("Theme", "auto_refresh", 1) != 0;
		{
			int bm = AfxGetApp()->GetProfileInt("Theme", "banner_mode", banner_strip);
			if (bm < banner_off || bm > banner_strip) bm = banner_strip;
			g_banner_mode = static_cast<banner_mode>(bm);
		}
		create_brushes();
	}

	void save()
	{
		AfxGetApp()->WriteProfileInt("Theme", "mode", g_mode);
		AfxGetApp()->WriteProfileInt("Theme", "show_grid", g_show_grid ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "show_column_headers", g_show_column_headers ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "hide_empty_results", g_hide_empty_results ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "active_pane_border", g_active_pane_border ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "show_filter_box", g_show_filter_box ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "show_breadcrumb", g_show_breadcrumb ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "topbar_filter_w", g_topbar_filter_w);
		AfxGetApp()->WriteProfileInt("Theme", "topbar_swapped", g_topbar_swapped ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "shp_transparency", g_shp_transparency ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "alpha_color", static_cast<int>(g_alpha_color));
		AfxGetApp()->WriteProfileInt("Theme", "use_checkerboard", g_use_checkerboard ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "use_external_programs", g_use_external_programs ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "interpolation", static_cast<int>(g_interp));
		AfxGetApp()->WriteProfileInt("Theme", "sharpen_amount", g_sharpen_amount);
		AfxGetApp()->WriteProfileInt("Theme", "fps_cap", g_fps_cap);
		AfxGetApp()->WriteProfileInt("Theme", "size_format", static_cast<int>(g_size_fmt));
		AfxGetApp()->WriteProfileInt("Theme", "clipboard_format", static_cast<int>(g_clipboard_fmt));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_supersample", static_cast<int>(g_vxl_ss));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_shading", g_vxl_shading ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_normal_src", static_cast<int>(g_vxl_normal_src));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_normal_method", static_cast<int>(g_vxl_normal_method));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_normal_kernel", static_cast<int>(g_vxl_normal_kernel));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_ao_enabled", g_vxl_ao_enabled ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_ao_strength", g_vxl_ao_strength);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_ao_quality", static_cast<int>(g_vxl_ao_quality));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_ao_method", static_cast<int>(g_vxl_ao_method));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_ao_contact_falloff", static_cast<int>(g_vxl_ao_contact_falloff));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_az", static_cast<int>(g_vxl_light_az * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_el", static_cast<int>(g_vxl_light_el * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_ambient", static_cast<int>(g_vxl_light_ambient * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_diffuse", static_cast<int>(g_vxl_light_diffuse * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_specular", static_cast<int>(g_vxl_light_specular * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_vpl_engine_faithful", g_vxl_vpl_engine_faithful ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_indicator_mode", static_cast<int>(g_vxl_light_indicator_mode));
		AfxGetApp()->WriteProfileInt("Theme", "limit_vxl_cpu", g_limit_vxl_cpu ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_full_hierarchy", g_vxl_full_hierarchy ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_zoom_aware_ss", g_vxl_zoom_aware_ss ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_splat_pad_extra", g_vxl_splat_pad_extra);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_frame", static_cast<int>(g_vxl_light_frame));
		AfxGetApp()->WriteProfileInt("Theme", "parallel_extract", g_parallel_extract ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "silent_delete", g_silent_delete ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "auto_refresh", g_auto_refresh ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "banner_mode", static_cast<int>(g_banner_mode));
	}

	mode get() { return g_mode; }
	bool is_dark() { return g_resolved_dark; }

	void set(mode m)
	{
		if (g_mode == m)
			return;
		g_mode = m;
		g_resolved_dark = resolve_dark(g_mode);
		create_brushes();
		save();
		apply_app_mode();
	}

	bool show_grid() { return g_show_grid; }

	void set_show_grid(bool v)
	{
		if (g_show_grid == v)
			return;
		g_show_grid = v;
		save();
	}

	bool show_column_headers() { return g_show_column_headers; }

	void set_show_column_headers(bool v)
	{
		if (g_show_column_headers == v)
			return;
		g_show_column_headers = v;
		save();
	}

	bool hide_empty_results() { return g_hide_empty_results; }

	void set_hide_empty_results(bool v)
	{
		if (g_hide_empty_results == v)
			return;
		g_hide_empty_results = v;
		save();
	}

	bool active_pane_border() { return g_active_pane_border; }

	void set_active_pane_border(bool v)
	{
		if (g_active_pane_border == v)
			return;
		g_active_pane_border = v;
		save();
	}

	bool show_filter_box() { return g_show_filter_box; }

	void set_show_filter_box(bool v)
	{
		if (g_show_filter_box == v)
			return;
		g_show_filter_box = v;
		save();
	}

	bool show_breadcrumb() { return g_show_breadcrumb; }

	void set_show_breadcrumb(bool v)
	{
		if (g_show_breadcrumb == v)
			return;
		g_show_breadcrumb = v;
		save();
	}

	int topbar_filter_w() { return g_topbar_filter_w; }

	void set_topbar_filter_w(int v)
	{
		if (g_topbar_filter_w == v)
			return;
		g_topbar_filter_w = v;
		save();
	}

	bool topbar_swapped() { return g_topbar_swapped; }

	void set_topbar_swapped(bool v)
	{
		if (g_topbar_swapped == v)
			return;
		g_topbar_swapped = v;
		save();
	}

	size_format size_fmt() { return g_size_fmt; }

	void set_size_fmt(size_format v)
	{
		if (g_size_fmt == v)
			return;
		g_size_fmt = v;
		save();
	}

	clipboard_format clipboard_fmt() { return g_clipboard_fmt; }

	void set_clipboard_fmt(clipboard_format v)
	{
		if (g_clipboard_fmt == v)
			return;
		g_clipboard_fmt = v;
		save();
	}

	vxl_ss vxl_supersample() { return g_vxl_ss; }

	void set_vxl_supersample(vxl_ss v)
	{
		if (g_vxl_ss == v)
			return;
		g_vxl_ss = v;
		save();
	}

	bool vxl_shading() { return g_vxl_shading; }

	void set_vxl_shading(bool v)
	{
		if (g_vxl_shading == v)
			return;
		g_vxl_shading = v;
		save();
	}

	vxl_normal_source vxl_normal_src() { return g_vxl_normal_src; }
	void set_vxl_normal_src(vxl_normal_source v)
	{
		if (g_vxl_normal_src == v)
			return;
		g_vxl_normal_src = v;
		// Bump lighting version so the splat cache invalidates. The cloud
		// itself also needs to rebuild — the dialog handler that calls this
		// is responsible for bumping the view's m_open_token.
		g_vxl_lighting_version++;
		save();
	}

	vxl_normal_method vxl_normals_method() { return g_vxl_normal_method; }
	void set_vxl_normals_method(vxl_normal_method v)
	{
		if (g_vxl_normal_method == v) return;
		g_vxl_normal_method = v;
		g_vxl_lighting_version++;
		save();
	}

	vxl_normal_kernel vxl_normals_kernel() { return g_vxl_normal_kernel; }
	void set_vxl_normals_kernel(vxl_normal_kernel v)
	{
		if (g_vxl_normal_kernel == v) return;
		g_vxl_normal_kernel = v;
		g_vxl_lighting_version++;
		save();
	}

	bool vxl_ao_enabled() { return g_vxl_ao_enabled; }
	void set_vxl_ao_enabled(bool v)
	{
		if (g_vxl_ao_enabled == v) return;
		g_vxl_ao_enabled = v;
		g_vxl_lighting_version++;
		save();
	}
	int vxl_ao_strength() { return g_vxl_ao_strength; }
	void set_vxl_ao_strength(int v)
	{
		if (v < 0) v = 0; else if (v > 100) v = 100;
		if (g_vxl_ao_strength == v) return;
		g_vxl_ao_strength = v;
		g_vxl_lighting_version++;
		// Defer save like the other lighting sliders. VxlLightingDlg
		// flushes on slider release via flush_lighting_save().
		g_vxl_lighting_save_pending = true;
	}

	vxl_ao_quality vxl_ao_quality_v() { return g_vxl_ao_quality; }
	void set_vxl_ao_quality(vxl_ao_quality v)
	{
		if (g_vxl_ao_quality == v) return;
		g_vxl_ao_quality = v;
		// Quality change requires the cloud to rebake — the caller (dialog)
		// triggers that via invalidate_vxl_cloud_in_file_view. We still bump
		// the lighting version so any post-rebake splat key check invalidates
		// cleanly. Quality is a discrete combobox change, not a drag, so
		// save() immediately (no flush deferral).
		g_vxl_lighting_version++;
		save();
	}

	vxl_ao_method vxl_ao_method_v() { return g_vxl_ao_method; }
	void set_vxl_ao_method(vxl_ao_method v)
	{
		if (g_vxl_ao_method == v) return;
		g_vxl_ao_method = v;
		// Both AO paths write into the same per-voxel `ao` byte, so a method
		// change is a cloud rebuild — caller routes through
		// invalidate_vxl_cloud_in_file_view. Bump version + save immediately.
		g_vxl_lighting_version++;
		save();
	}

	vxl_ao_contact_falloff vxl_ao_contact_falloff_v() { return g_vxl_ao_contact_falloff; }
	void set_vxl_ao_contact_falloff(vxl_ao_contact_falloff v)
	{
		if (g_vxl_ao_contact_falloff == v) return;
		g_vxl_ao_contact_falloff = v;
		// Falloff is consulted only by the contact bake; rebake the cloud
		// when in contact mode (caller decides). In hemisphere mode the
		// setting is dormant, so the rebake call is harmless.
		g_vxl_lighting_version++;
		save();
	}

	float vxl_light_azimuth()   { return g_vxl_light_az; }
	float vxl_light_elevation() { return g_vxl_light_el; }
	float vxl_light_ambient()   { return g_vxl_light_ambient; }
	float vxl_light_diffuse()   { return g_vxl_light_diffuse; }
	float vxl_light_specular()  { return g_vxl_light_specular; }
	int   vxl_lighting_version(){ return g_vxl_lighting_version; }

	// All four lighting slider setters defer the registry save() — they only
	// update memory + bump the version stamp + mark the save pending. The
	// dialog flushes on slider release via flush_lighting_save(). Without
	// this, a fast slider drag fires hundreds of save() calls per second,
	// each writing ~22 registry keys via WriteProfileInt, which is what was
	// pegging CPU/IO.
	void set_vxl_light_azimuth(float v)
	{
		if (v < 0.0f) v = 0.0f;
		if (v > 360.0f) v = 360.0f;
		if (g_vxl_light_az == v) return;
		g_vxl_light_az = v;
		g_vxl_lighting_version++;
		g_vxl_lighting_save_pending = true;
	}
	void set_vxl_light_elevation(float v)
	{
		if (v < -90.0f) v = -90.0f;
		if (v > 90.0f) v = 90.0f;
		if (g_vxl_light_el == v) return;
		g_vxl_light_el = v;
		g_vxl_lighting_version++;
		g_vxl_lighting_save_pending = true;
	}
	void set_vxl_light_ambient(float v)
	{
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		if (g_vxl_light_ambient == v) return;
		g_vxl_light_ambient = v;
		g_vxl_lighting_version++;
		g_vxl_lighting_save_pending = true;
	}
	void set_vxl_light_diffuse(float v)
	{
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		if (g_vxl_light_diffuse == v) return;
		g_vxl_light_diffuse = v;
		g_vxl_lighting_version++;
		g_vxl_lighting_save_pending = true;
	}
	void set_vxl_light_specular(float v)
	{
		if (v < 0.0f) v = 0.0f;
		if (v > 5.0f) v = 5.0f;
		if (g_vxl_light_specular == v) return;
		g_vxl_light_specular = v;
		g_vxl_lighting_version++;
		g_vxl_lighting_save_pending = true;
	}

	void flush_lighting_save()
	{
		if (!g_vxl_lighting_save_pending) return;
		g_vxl_lighting_save_pending = false;
		save();
	}
	void reset_vxl_lighting()
	{
		g_vxl_light_az = k_default_az;
		g_vxl_light_el = k_default_el;
		g_vxl_light_ambient = k_default_ambient;
		g_vxl_light_diffuse = k_default_diffuse;
		g_vxl_light_specular = k_default_specular;
		g_vxl_lighting_version++;
		save();
	}
	void set_vxl_light_preset(vxl_light_preset p)
	{
		switch (p)
		{
		case vlp_ra2: g_vxl_light_az = k_ra2_az; g_vxl_light_el = k_ra2_el; break;
		case vlp_ts:  g_vxl_light_az = k_ts_az;  g_vxl_light_el = k_ts_el;  break;
		default: return;	// vlp_custom: nothing to set
		}
		// Engine presets only make engine-faithful sense in world-fixed mode
		// (the engine's VoxelLightSource is fixed to the model, not the
		// camera). Force it on for the user; they can flip back to
		// camera-fixed via the dropdown after if they prefer.
		g_vxl_light_frame = vlf_world_fixed;
		g_vxl_lighting_version++;
		save();
	}

	vxl_light_frame vxl_light_frame_v() { return g_vxl_light_frame; }

	void set_vxl_light_frame(vxl_light_frame v)
	{
		if (g_vxl_light_frame == v) return;
		g_vxl_light_frame = v;
		// Frame change alters how the splat consumes the light vector and the
		// VPL bake's section indices, so it must invalidate the splat. The
		// version bump piggybacks on lighting_version (the splat key already
		// includes it for VPL mode and ignores it for synthetic — both want a
		// rebuild here).
		g_vxl_lighting_version++;
		save();
	}
	void vxl_light_direction(float& x, float& y, float& z)
	{
		const float pi = 3.14159265358979323846f;
		float az_rad = g_vxl_light_az * pi / 180.0f;
		float el_rad = g_vxl_light_el * pi / 180.0f;
		float ce = std::cos(el_rad);
		// Convention: az=0 → +X, az=90° → +Y; +elevation = light above the
		// horizon (RA2 [Lighting] convention). This returns a WORLD-space
		// (model-fixed) light vector — the splat rotates it into camera space
		// (by the same yaw+pitch+Y-flip the voxel normals get) before dotting,
		// so the lit side stays fixed to the model as the camera orbits, exactly
		// like the engine's world-fixed VoxelLightSource.
		// Default az=225°, el=-54.7356° → x=-0.40825, y=-0.40825, z=+0.81650
		// (matches the original hand-tuned constants).
		x = ce * std::cos(az_rad);
		y = ce * std::sin(az_rad);
		z = -std::sin(el_rad);
	}

	bool vxl_vpl_engine_faithful() { return g_vxl_vpl_engine_faithful; }
	void set_vxl_vpl_engine_faithful(bool v)
	{
		if (g_vxl_vpl_engine_faithful == v) return;
		g_vxl_vpl_engine_faithful = v;
		// Bumping the lighting version invalidates the splat cache, which
		// has the VPL section bytes baked in. The next paint re-derives
		// section indices under the new formula.
		g_vxl_lighting_version++;
		save();
	}

	bool vxl_light_indicator_visible() { return g_vxl_light_indicator_visible; }
	void set_vxl_light_indicator_visible(bool v) { g_vxl_light_indicator_visible = v; }
	vxl_light_indicator_placement vxl_light_indicator_mode() { return g_vxl_light_indicator_mode; }
	void set_vxl_light_indicator_mode(vxl_light_indicator_placement v)
	{
		if (g_vxl_light_indicator_mode == v) return;
		g_vxl_light_indicator_mode = v;
		save();
	}

	bool limit_vxl_cpu() { return g_limit_vxl_cpu; }

	void set_limit_vxl_cpu(bool v)
	{
		if (g_limit_vxl_cpu == v)
			return;
		g_limit_vxl_cpu = v;
		save();
	}

	bool parallel_extract() { return g_parallel_extract; }

	void set_parallel_extract(bool v)
	{
		if (g_parallel_extract == v)
			return;
		g_parallel_extract = v;
		save();
	}

	bool silent_delete() { return g_silent_delete; }

	void set_silent_delete(bool v)
	{
		if (g_silent_delete == v)
			return;
		g_silent_delete = v;
		save();
	}

	bool auto_refresh() { return g_auto_refresh; }

	void set_auto_refresh(bool v)
	{
		if (g_auto_refresh == v)
			return;
		g_auto_refresh = v;
		save();
	}

	banner_mode banner_mode_v() { return g_banner_mode; }

	void set_banner_mode(banner_mode v)
	{
		if (g_banner_mode == v)
			return;
		g_banner_mode = v;
		save();
	}

	bool vxl_full_hierarchy() { return g_vxl_full_hierarchy; }

	void set_vxl_full_hierarchy(bool v)
	{
		if (g_vxl_full_hierarchy == v)
			return;
		g_vxl_full_hierarchy = v;
		save();
	}

	bool vxl_zoom_aware_ss() { return g_vxl_zoom_aware_ss; }

	void set_vxl_zoom_aware_ss(bool v)
	{
		if (g_vxl_zoom_aware_ss == v)
			return;
		g_vxl_zoom_aware_ss = v;
		save();
	}

	int vxl_splat_pad_extra() { return g_vxl_splat_pad_extra; }

	void set_vxl_splat_pad_extra(int v)
	{
		v = std::clamp(v, -64, 64);
		if (g_vxl_splat_pad_extra == v)
			return;
		g_vxl_splat_pad_extra = v;
		save();
	}

	std::string format_size(long long bytes)
	{
		if (bytes < 0)
			return std::string();
		if (g_size_fmt == size_bytes)
			return std::to_string(bytes);
		// size_auto — Vodrix's totalSize: integer-truncated value with B/KB/MB/GB suffix.
		static const char* names[] = { " B", " KB", " MB", " GB" };
		size_t v = static_cast<size_t>(bytes);
		size_t div = 0;
		while (v >= 1024 && div < (sizeof names / sizeof *names) - 1)
		{
			div++;
			v /= 1024;
		}
		char buf[64];
		std::snprintf(buf, sizeof buf, "%zu%s", v, names[div]);
		return buf;
	}

	bool shp_transparency() { return g_shp_transparency; }
	void set_shp_transparency(bool v)
	{
		if (g_shp_transparency == v)
			return;
		g_shp_transparency = v;
		save();
	}

	void apply_grid(HWND h_listview)
	{
		if (!h_listview)
			return;
		DWORD ex = static_cast<DWORD>(::SendMessage(h_listview, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0));
		// In dark mode the system's LVS_EX_GRIDLINES paints with a hardcoded
		// light gray that clashes with the theme. CListCtrlEx now draws its
		// own grid in CDDS_ITEMPOSTPAINT using theme::border(), so we strip
		// the style here in dark mode regardless of the show_grid flag and
		// rely on CListCtrlEx to honor the flag itself via the theme hook.
		if (g_show_grid && !is_dark())
			ex |= LVS_EX_GRIDLINES;
		else
			ex &= ~LVS_EX_GRIDLINES;
		::SendMessage(h_listview, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, ex);
		::InvalidateRect(h_listview, NULL, TRUE);
	}

	// Forward declarations for file-scope helpers defined further down.
	// enable_column_visibility_menu + apply_listview_groups (just below)
	// need them.
	static void install_dark_header_subclass(HWND h_header);
	static std::string col_key(const char* lv_id, int col, const char* suffix);
	static LRESULT CALLBACK dark_listview_groups_proc(HWND, UINT, WPARAM, LPARAM);
	static void install_column_state_subclass(HWND h_listview);
	static void apply_persisted_column_order(HWND h_listview, const char* lv_id);
	static void apply_persisted_column_widths(HWND h_listview, const char* lv_id);

	void apply_column_headers(HWND h_listview)
	{
		if (!h_listview)
			return;
		LONG s = ::GetWindowLong(h_listview, GWL_STYLE);
		LONG ns = g_show_column_headers ? (s & ~LVS_NOCOLUMNHEADER) : (s | LVS_NOCOLUMNHEADER);
		if (ns == s)
			return;
		::SetWindowLong(h_listview, GWL_STYLE, ns);
		// SWP_FRAMECHANGED forces the listview to recalc its client rect so
		// the header band visually appears/disappears immediately.
		::SetWindowPos(h_listview, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		::InvalidateRect(h_listview, NULL, TRUE);
	}

	void enable_column_visibility_menu(HWND h_listview, const char* lv_id)
	{
		if (!h_listview || !lv_id || !*lv_id)
			return;
		HWND h_header = reinterpret_cast<HWND>(::SendMessageW(h_listview, LVM_GETHEADER, 0, 0));
		if (!h_header)
			return;
		// Ensure the header is subclassed (installs dark_header_proc the
		// first time). install_dark_header_subclass is the dark-mode entry
		// point but the subclass also owns context-menu interception now —
		// safe to call regardless of theme.
		install_dark_header_subclass(h_header);
		// The lv_id string must outlive the prop; callers pass string
		// literals (static lifetime), so storing the pointer directly is
		// safe. If a future caller passes dynamic memory, switch to
		// strdup + RemoveProp cleanup on WM_NCDESTROY.
		::SetPropW(h_header, L"xcc.col_menu_lv_id",
			reinterpret_cast<HANDLE>(const_cast<char*>(lv_id)));
		// Restore previously hidden columns from persisted state. Walk
		// every column once: prefer the explicit saved width (set by the
		// hide menu OR by user-resize via the column-state subclass) over
		// the initial post-InsertColumn width.
		const int n = static_cast<int>(::SendMessageW(h_header, HDM_GETITEMCOUNT, 0, 0));
		apply_persisted_column_widths(h_listview, lv_id);
		for (int i = 0; i < n; i++)
		{
			const int hidden = AfxGetApp()->GetProfileInt("Theme", col_key(lv_id, i, "h").c_str(), 0);
			if (!hidden)
				continue;
			const int cur_w = static_cast<int>(::SendMessageW(h_listview, LVM_GETCOLUMNWIDTH, i, 0));
			if (cur_w > 0)
			{
				// First time restoring after fresh InsertColumn — current
				// width IS the last-visible width. Only save if we don't
				// already have one.
				const int saved = AfxGetApp()->GetProfileInt("Theme", col_key(lv_id, i, "w").c_str(), 0);
				if (saved <= 0)
					AfxGetApp()->WriteProfileInt("Theme", col_key(lv_id, i, "w").c_str(), cur_w);
				::SendMessageW(h_listview, LVM_SETCOLUMNWIDTH, i, 0);
			}
		}
		// Restore column display order (drag-to-reorder result).
		apply_persisted_column_order(h_listview, lv_id);
		// Hook HDN_ENDDRAG (order) + HDN_ENDTRACK (resize) to persist user
		// edits. Idempotent.
		install_column_state_subclass(h_listview);
	}

	void apply_listview_groups(HWND h_listview)
	{
		if (!h_listview)
			return;
		static const wchar_t* k_subclassed = L"xcc.dark_lv_groups_subclass";
		static const wchar_t* k_orig_proc = L"xcc.dark_lv_groups_orig_proc";
		if (::GetPropW(h_listview, k_subclassed))
			return; // already installed
		LONG_PTR orig = ::GetWindowLongPtrW(h_listview, GWLP_WNDPROC);
		::SetPropW(h_listview, k_orig_proc, reinterpret_cast<HANDLE>(orig));
		::SetWindowLongPtrW(h_listview, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dark_listview_groups_proc));
		::SetPropW(h_listview, k_subclassed, reinterpret_cast<HANDLE>(1));
		// Trigger a paint so the overlay shows immediately on theme switch.
		::InvalidateRect(h_listview, NULL, TRUE);
	}

	COLORREF bg()          { return is_dark() ? RGB(0x20, 0x20, 0x20) : RGB(0xFF, 0xFF, 0xFF); }
	COLORREF bg_alt()      { return is_dark() ? RGB(0x2B, 0x2B, 0x2B) : RGB(0xF3, 0xF3, 0xF3); }
	COLORREF text()        { return is_dark() ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x00, 0x00, 0x00); }
	COLORREF text_dim()    { return is_dark() ? RGB(0xC8, 0xC8, 0xC8) : RGB(0x55, 0x55, 0x55); }
	COLORREF accent()      { return RGB(0x00, 0x78, 0xD4); }
	COLORREF accent_text() { return RGB(0xFF, 0xFF, 0xFF); }
	COLORREF border()      { return is_dark() ? RGB(0x3A, 0x3A, 0x3A) : RGB(0xD0, 0xD0, 0xD0); }
	COLORREF menu_bg()     { return is_dark() ? RGB(0x2B, 0x2B, 0x2B) : RGB(0xF3, 0xF3, 0xF3); }
	COLORREF menu_hot()    { return is_dark() ? RGB(0x3D, 0x3D, 0x3D) : RGB(0xE5, 0xE5, 0xE5); }

	COLORREF alpha_color() { return g_alpha_color; }
	void set_alpha_color(COLORREF c)
	{
		if (g_alpha_color == c)
			return;
		g_alpha_color = c;
		save();
	}
	COLORREF checker_a() { return g_alpha_color; }
	// When use_checkerboard is off, both squares are the same color so the
	// XOR pattern in callers collapses to a flat fill — no call-site changes
	// needed. When on, the second square is black, giving the classic
	// transparent-preview checker.
	COLORREF checker_b() { return g_use_checkerboard ? RGB(0, 0, 0) : g_alpha_color; }

	bool use_checkerboard() { return g_use_checkerboard; }
	void set_use_checkerboard(bool v)
	{
		if (g_use_checkerboard == v)
			return;
		g_use_checkerboard = v;
		save();
	}

	bool use_external_programs() { return g_use_external_programs; }
	void set_use_external_programs(bool v)
	{
		if (g_use_external_programs == v)
			return;
		g_use_external_programs = v;
		save();
	}

	interpolation interp() { return g_interp; }
	void set_interp(interpolation v)
	{
		if (g_interp == v)
			return;
		g_interp = v;
		save();
	}

	int interp_upscale_factor()
	{
		switch (g_interp)
		{
		case interp_scale2x:
		case interp_hq2x:
		case interp_xbr2x:
		case interp_nnedi2x:
			return 2;
		case interp_scale3x:
			return 3;
		case interp_hq4x:
		case interp_xbr4x:
		case interp_nnedi4x:
			return 4;
		default:
			return 1;
		}
	}
	bool interp_is_pixel_art_upscaler()
	{
		return g_interp >= interp_scale2x && g_interp <= interp_nnedi4x;
	}

	int sharpen_amount() { return g_sharpen_amount; }
	void set_sharpen_amount(int v)
	{
		if (v < 0) v = 0; else if (v > 100) v = 100;
		if (g_sharpen_amount == v)
			return;
		g_sharpen_amount = v;
		save();
	}

	int frame_rate_cap() { return g_fps_cap; }
	void set_frame_rate_cap(int v)
	{
		if (v < 1) v = 1; else if (v > 9999) v = 9999;
		if (g_fps_cap == v) return;
		g_fps_cap = v;
		save();
	}

	namespace
	{
		// Reusable scratch buffers — kept across paints to avoid per-frame
		// heap churn. The player paints from the UI thread only, so a single
		// shared set is fine.
		std::vector<DWORD> g_scratch_out;
		std::vector<float> g_scratch_tmp;

		// Lanczos-2 kernel, single-precision. sinc(x) * sinc(x/a), a = 2,
		// zero outside [-a, a]. a=2 (vs a=3) gives a crisper result at the
		// cost of slightly more aliasing on high-contrast edges. User picked
		// this over a=3 because the previous default looked too soft on VXL.
		inline float lanczos_kernel(float x)
		{
			if (x == 0.0f) return 1.0f;
			if (x < 0) x = -x;
			if (x >= 2.0f) return 0.0f;
			const float pi = 3.14159265358979323846f;
			float px = pi * x;
			return (std::sin(px) * std::sin(px / 2.0f)) / (px * px / 2.0f);
		}

		// Bilinear resample of a 32bpp BGRA top-down image. For each output
		// pixel, find its source-space center and 2x2-blend the four nearest
		// source pixels by fractional distance. Hand-rolled because GDI's
		// HALFTONE degenerates to nearest-neighbor on integer-clean upscale.
		void bilinear_resample(const DWORD* src, int sw, int sh,
			DWORD* dst, int dw, int dh)
		{
			if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
				return;
			const double scale_x = static_cast<double>(sw) / dw;
			const double scale_y = static_cast<double>(sh) / dh;
			// Skip the fork on small destination images — common for SHP/WSA
			// thumbnail-scale paints during scrolling.
			#pragma omp parallel for schedule(static) if(dw * dh >= 65536)
			for (int y = 0; y < dh; y++)
			{
				double sy = (y + 0.5) * scale_y - 0.5;
				int y0 = static_cast<int>(std::floor(sy));
				double fy = sy - y0;
				int y1 = y0 + 1;
				if (y0 < 0) { y0 = 0; fy = 0; }
				if (y1 < 0) y1 = 0;
				if (y0 >= sh) y0 = sh - 1;
				if (y1 >= sh) y1 = sh - 1;
				DWORD* drow = dst + static_cast<size_t>(y) * dw;
				const DWORD* r0 = src + static_cast<size_t>(y0) * sw;
				const DWORD* r1 = src + static_cast<size_t>(y1) * sw;
				for (int x = 0; x < dw; x++)
				{
					double sx = (x + 0.5) * scale_x - 0.5;
					int x0 = static_cast<int>(std::floor(sx));
					double fx = sx - x0;
					int x1 = x0 + 1;
					if (x0 < 0) { x0 = 0; fx = 0; }
					if (x1 < 0) x1 = 0;
					if (x0 >= sw) x0 = sw - 1;
					if (x1 >= sw) x1 = sw - 1;
					DWORD p00 = r0[x0], p01 = r0[x1];
					DWORD p10 = r1[x0], p11 = r1[x1];
					double w00 = (1 - fx) * (1 - fy);
					double w01 = fx       * (1 - fy);
					double w10 = (1 - fx) * fy;
					double w11 = fx       * fy;
					auto chan = [&](int shift) -> int
					{
						double v = w00 * ((p00 >> shift) & 0xff)
							+ w01 * ((p01 >> shift) & 0xff)
							+ w10 * ((p10 >> shift) & 0xff)
							+ w11 * ((p11 >> shift) & 0xff);
						int iv = static_cast<int>(v + 0.5);
						if (iv < 0) iv = 0;
						if (iv > 255) iv = 255;
						return iv;
					};
					drow[x] = chan(0) | (chan(8) << 8) | (chan(16) << 16) | (chan(24) << 24);
				}
			}
		}

		// Resample a 32bpp BGRA top-down image with Lanczos-3. Separable: rows
		// first into a reusable float intermediate, then columns. Output is
		// also 32bpp BGRA. All math single-precision; intermediate buffer
		// (g_scratch_tmp) is reused across calls.
		void lanczos_resample(const DWORD* src, int sw, int sh,
			DWORD* dst, int dw, int dh)
		{
			if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
				return;
			const int a = 2;
			const float scale_x = static_cast<float>(sw) / dw;
			const float scale_y = static_cast<float>(sh) / dh;
			// When upscaling we sample the source kernel at unit width; when
			// downscaling we widen the kernel (anti-alias) by the scale factor.
			const float fwx = scale_x > 1.0f ? scale_x : 1.0f;
			const float fwy = scale_y > 1.0f ? scale_y : 1.0f;
			// Per-axis tap tables. Use plain vectors for taps — they're small
			// (one entry per dest pixel), allocations dominated by the BGRA
			// intermediate, not these.
			struct t_taps { int first; std::vector<float> w; };
			std::vector<t_taps> tx(dw), ty(dh);
			auto build = [&](std::vector<t_taps>& out, int outN, int srcN, float scale, float fw)
			{
				for (int o = 0; o < outN; o++)
				{
					float center = (o + 0.5f) * scale - 0.5f;
					int first = static_cast<int>(std::floor(center - a * fw + 1));
					int last  = static_cast<int>(std::floor(center + a * fw));
					if (first < 0) first = 0;
					if (last >= srcN) last = srcN - 1;
					out[o].first = first;
					out[o].w.resize(last - first + 1);
					float sum = 0;
					for (int i = first; i <= last; i++)
					{
						float w = lanczos_kernel((i - center) / fw);
						out[o].w[i - first] = w;
						sum += w;
					}
					if (sum != 0)
						for (auto& w : out[o].w) w /= sum;
				}
			};
			build(tx, dw, sw, scale_x, fwx);
			build(ty, dh, sh, scale_y, fwy);

			// Pass 1: horizontal — produce a (dw x sh) float intermediate in
			// BGRA channel order. Reused buffer g_scratch_tmp.
			const size_t tmp_n = static_cast<size_t>(dw) * sh * 4;
			if (g_scratch_tmp.size() < tmp_n)
				g_scratch_tmp.resize(tmp_n);
			float* tmp = g_scratch_tmp.data();
			// Lanczos kernel is ~6 taps per axis — heavier per pixel than
			// bilinear, so the break-even is lower (~128x128).
			#pragma omp parallel for schedule(static) if(dw * sh >= 16384)
			for (int y = 0; y < sh; y++)
			{
				const DWORD* row = src + static_cast<size_t>(y) * sw;
				float* drow = tmp + static_cast<size_t>(y) * dw * 4;
				for (int x = 0; x < dw; x++)
				{
					float b = 0, g = 0, r = 0, alpha = 0;
					const auto& t = tx[x];
					const size_t kn = t.w.size();
					const float* w_ = t.w.data();
					const DWORD* p_ = row + t.first;
					for (size_t k = 0; k < kn; k++)
					{
						DWORD p = p_[k];
						float w = w_[k];
						b += w * (p & 0xff);
						g += w * ((p >> 8) & 0xff);
						r += w * ((p >> 16) & 0xff);
						alpha += w * ((p >> 24) & 0xff);
					}
					float* d = drow + x * 4;
					d[0] = b; d[1] = g; d[2] = r; d[3] = alpha;
				}
			}
			// Pass 2: vertical — read the intermediate, write into dst.
			#pragma omp parallel for schedule(static) if(dw * dh >= 16384)
			for (int y = 0; y < dh; y++)
			{
				DWORD* drow = dst + static_cast<size_t>(y) * dw;
				const auto& t = ty[y];
				const size_t kn = t.w.size();
				const float* w_ = t.w.data();
				for (int x = 0; x < dw; x++)
				{
					float b = 0, g = 0, r = 0, alpha = 0;
					for (size_t k = 0; k < kn; k++)
					{
						const float* sp = tmp
							+ (static_cast<size_t>(t.first + k) * dw + x) * 4;
						float w = w_[k];
						b += w * sp[0];
						g += w * sp[1];
						r += w * sp[2];
						alpha += w * sp[3];
					}
					auto clamp = [](float v) -> int
					{
						if (v < 0) return 0;
						if (v > 255) return 255;
						return static_cast<int>(v + 0.5f);
					};
					drow[x] = clamp(b) | (clamp(g) << 8) | (clamp(r) << 16) | (clamp(alpha) << 24);
				}
			}
		}
	}

	// Public forwarder for the file-private bilinear_resample inside the
	// anonymous namespace above. Kept as a thin wrapper rather than moved
	// out of the anon namespace to avoid disturbing the existing splat /
	// stretch_image plumbing that calls bilinear_resample directly.
	void bilinear_resample_bgra(const DWORD* src, int sw, int sh,
		DWORD* dst, int dw, int dh)
	{
		bilinear_resample(src, sw, sh, dst, dw, dh);
	}

	namespace
	{
		// 3x3 unsharp mask on a 32bpp BGRA top-down buffer, in-place.
		// kernel = pixel + amount * (pixel - blur(pixel)), where blur is a
		// box-3 average. amount = sharpen_amount / 100 mapped to [0..1.5] so
		// the slider has a useful top end without going completely cartoon.
		// Edges replicate (clamp), not wrap. Per-channel clamp to [0,255].
		void unsharp_mask(DWORD* buf, int w, int h)
		{
			const int sa = g_sharpen_amount;
			if (sa <= 0 || w < 3 || h < 3) return;
			// amount in [0..1.5] — 1.5 at sa=100 is firmly "crisp" without
			// going past the point where halos dominate.
			const float amount = (sa / 100.0f) * 1.5f;
			// Out-of-place pass so the OpenMP rows don't race on shared
			// reads/writes of buf. Sizes are bounded by viewport area, which
			// is small relative to anything that'd justify pooling.
			std::vector<DWORD> out(static_cast<size_t>(w) * h);
			#pragma omp parallel for schedule(static) if(w * h >= 65536)
			for (int y = 0; y < h; y++)
			{
				const int ym = (y == 0)     ? 0     : y - 1;
				const int yp = (y == h - 1) ? h - 1 : y + 1;
				const DWORD* r0 = buf + static_cast<size_t>(ym) * w;
				const DWORD* r1 = buf + static_cast<size_t>(y)  * w;
				const DWORD* r2 = buf + static_cast<size_t>(yp) * w;
				DWORD* drow = out.data() + static_cast<size_t>(y) * w;
				for (int x = 0; x < w; x++)
				{
					const int xm = (x == 0)     ? 0     : x - 1;
					const int xp = (x == w - 1) ? w - 1 : x + 1;
					DWORD c = r1[x];
					// Per-channel box-3 average and sharpen.
					int out_b = 0, out_g = 0, out_r = 0;
					for (int shift = 0, ch = 0; ch < 3; ch++, shift += 8)
					{
						int sum =
							 ((r0[xm] >> shift) & 0xff) + ((r0[x] >> shift) & 0xff) + ((r0[xp] >> shift) & 0xff)
							+((r1[xm] >> shift) & 0xff) + ((r1[x] >> shift) & 0xff) + ((r1[xp] >> shift) & 0xff)
							+((r2[xm] >> shift) & 0xff) + ((r2[x] >> shift) & 0xff) + ((r2[xp] >> shift) & 0xff);
						const float blur = sum * (1.0f / 9.0f);
						const float center = static_cast<float>((c >> shift) & 0xff);
						float v = center + amount * (center - blur);
						int iv = static_cast<int>(v + 0.5f);
						if (iv < 0) iv = 0; else if (iv > 255) iv = 255;
						if (ch == 0) out_b = iv;
						else if (ch == 1) out_g = iv;
						else out_r = iv;
					}
					drow[x] = static_cast<DWORD>(out_b)
						| (static_cast<DWORD>(out_g) << 8)
						| (static_cast<DWORD>(out_r) << 16)
						| (c & 0xff000000);
				}
			}
			std::memcpy(buf, out.data(), sizeof(DWORD) * static_cast<size_t>(w) * h);
		}
	}

	void stretch_image(CDC* dst, int dx, int dy, int dw, int dh,
		CDC* src_dc, HBITMAP src_dib, const DWORD* src_bits, int sw, int sh)
	{
		stretch_image(dst, dx, dy, dw, dh, src_dc, src_dib, src_bits, sw, sh, g_interp);
	}

	void stretch_image(CDC* dst, int dx, int dy, int dw, int dh,
		CDC* src_dc, HBITMAP src_dib, const DWORD* src_bits, int sw, int sh,
		interpolation mode)
	{
		if (!dst || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
			return;
		// 1:1 fast path: if there's no scaling AND no sharpen pass to apply
		// AND the mode is nearest (or no src_bits available for a sharpen
		// post-pass), BitBlt and bail. Otherwise we fall into the resampler
		// switch below so the unsharp post-pass still runs even at 1:1.
		const bool sharpen_on = g_sharpen_amount > 0 && mode != interp_nearest && src_bits;
		if (dw == sw && dh == sh && src_dc && !sharpen_on)
		{
			dst->BitBlt(dx, dy, dw, dh, src_dc, 0, 0, SRCCOPY);
			return;
		}
		// 1:1 with sharpen: copy src_bits to scratch, run unsharp, blit. We
		// reach here only when sharpen_on AND dw==sw AND dh==sh.
		if (dw == sw && dh == sh && sharpen_on)
		{
			const size_t need = static_cast<size_t>(dw) * dh;
			if (g_scratch_out.size() < need) g_scratch_out.resize(need);
			DWORD* out_buf = g_scratch_out.data();
			std::memcpy(out_buf, src_bits, sizeof(DWORD) * need);
			unsharp_mask(out_buf, dw, dh);
			BITMAPINFO bmi{};
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = dw;
			bmi.bmiHeader.biHeight = -dh;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			::SetDIBitsToDevice(dst->GetSafeHdc(), dx, dy, dw, dh, 0, 0, 0, dh,
				out_buf, &bmi, DIB_RGB_COLORS);
			return;
		}
		switch (mode)
		{
		case interp_bilinear:
			if (src_bits)
			{
				const size_t need = static_cast<size_t>(dw) * dh;
				if (g_scratch_out.size() < need) g_scratch_out.resize(need);
				DWORD* out_buf = g_scratch_out.data();
				bilinear_resample(src_bits, sw, sh, out_buf, dw, dh);
				if (g_sharpen_amount > 0) unsharp_mask(out_buf, dw, dh);
				BITMAPINFO bmi{};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = dw;
				bmi.bmiHeader.biHeight = -dh;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;
				::SetDIBitsToDevice(dst->GetSafeHdc(), dx, dy, dw, dh, 0, 0, 0, dh,
					out_buf, &bmi, DIB_RGB_COLORS);
				return;
			}
			break;
		case interp_bicubic:
			if (src_dib)
			{
				if (g_sharpen_amount > 0)
				{
					// Route the bicubic resample through an offscreen GDI+
					// bitmap so we can run unsharp_mask on the result before
					// blitting to the destination. Without this detour the
					// direct DrawImage-to-DC path skips the post-pass.
					Gdiplus::Bitmap out_bmp(dw, dh, PixelFormat32bppARGB);
					{
						Gdiplus::Graphics g(&out_bmp);
						g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
						g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
						Gdiplus::Bitmap bmp(src_dib, NULL);
						g.DrawImage(&bmp, Gdiplus::Rect(0, 0, dw, dh), 0, 0, sw, sh, Gdiplus::UnitPixel);
					}
					Gdiplus::Rect lock_r(0, 0, dw, dh);
					Gdiplus::BitmapData bd;
					if (out_bmp.LockBits(&lock_r, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
						PixelFormat32bppARGB, &bd) == Gdiplus::Ok)
					{
						// GDI+ may return a stride > dw*4; copy row-by-row into
						// our packed scratch buffer if so.
						const size_t need = static_cast<size_t>(dw) * dh;
						if (g_scratch_out.size() < need) g_scratch_out.resize(need);
						DWORD* out_buf = g_scratch_out.data();
						const int stride_dwords = bd.Stride / 4;
						const DWORD* src_p = static_cast<const DWORD*>(bd.Scan0);
						for (int y = 0; y < dh; y++)
							std::memcpy(out_buf + static_cast<size_t>(y) * dw,
								src_p + static_cast<size_t>(y) * stride_dwords,
								sizeof(DWORD) * dw);
						out_bmp.UnlockBits(&bd);
						unsharp_mask(out_buf, dw, dh);
						BITMAPINFO bmi{};
						bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
						bmi.bmiHeader.biWidth = dw;
						bmi.bmiHeader.biHeight = -dh;
						bmi.bmiHeader.biPlanes = 1;
						bmi.bmiHeader.biBitCount = 32;
						bmi.bmiHeader.biCompression = BI_RGB;
						::SetDIBitsToDevice(dst->GetSafeHdc(), dx, dy, dw, dh, 0, 0, 0, dh,
							out_buf, &bmi, DIB_RGB_COLORS);
						return;
					}
					// Lock failure: fall through to the direct path.
				}
				Gdiplus::Graphics g(dst->GetSafeHdc());
				g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
				g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
				Gdiplus::Bitmap bmp(src_dib, NULL);
				g.DrawImage(&bmp, Gdiplus::Rect(dx, dy, dw, dh), 0, 0, sw, sh, Gdiplus::UnitPixel);
				return;
			}
			break;
		case interp_lanczos:
			if (src_bits)
			{
				const size_t need = static_cast<size_t>(dw) * dh;
				if (g_scratch_out.size() < need) g_scratch_out.resize(need);
				DWORD* out_buf = g_scratch_out.data();
				lanczos_resample(src_bits, sw, sh, out_buf, dw, dh);
				if (g_sharpen_amount > 0) unsharp_mask(out_buf, dw, dh);
				BITMAPINFO bmi{};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = dw;
				bmi.bmiHeader.biHeight = -dh;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;
				::SetDIBitsToDevice(dst->GetSafeHdc(), dx, dy, dw, dh, 0, 0, 0, dh,
					out_buf, &bmi, DIB_RGB_COLORS);
				return;
			}
			break;
		case interp_nearest:
		default:
			break;
		}
		// Fallback: nearest-neighbor.
		if (src_dc)
		{
			dst->SetStretchBltMode(COLORONCOLOR);
			dst->StretchBlt(dx, dy, dw, dh, src_dc, 0, 0, sw, sh, SRCCOPY);
		}
	}

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
		// DWM composition lazily picks up the new attribute. For hidden
		// dialogs that are about to be shown, the first composed frame can
		// still render with the previous (light) titlebar — the "flashbang"
		// reports on the v10.80 Load PAL / Ctrl+F dialogs were this:
		// apply_titlebar ran during OnInitDialog while the window was still
		// hidden, then MFC's post-init ShowWindow produced one light frame
		// before DWM re-composed dark. Forcing SWP_FRAMECHANGED makes the
		// next composed frame honor the attribute. Only do it for hidden
		// windows; on visible windows the existing repaint path already
		// catches the change and an extra SetWindowPos would flicker.
		if (!::IsWindowVisible(h_top_level))
		{
			::SetWindowPos(h_top_level, NULL, 0, 0, 0, 0,
				SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		}
	}

	void apply_window(HWND h)
	{
		if (!h)
			return;
		load_uxtheme();
		if (p_AllowDarkModeForWindow)
			p_AllowDarkModeForWindow(h, is_dark() ? TRUE : FALSE);
		// Class-specific theme override. DarkMode_Explorer gives dark scroll-
		// bars/headers on listviews/edits/treeviews when the process is in
		// ForceDark mode, but it's the wrong subtheme for comboboxes — it
		// produces the white-and-buggy combobox we saw. WinSpy on Win11
		// dark-mode comboboxes (e.g. Everything's search combo) shows no
		// theme set at all; uxtheme picks the right look from the per-HWND
		// AllowDarkModeForWindow + process-wide SetPreferredAppMode flags.
		// So: leave the theme unset for ComboBox; keep DarkMode_Explorer for
		// every other class.
		char cls[64];
		::GetClassNameA(h, cls, sizeof(cls));
		const bool is_combo = _stricmp(cls, "ComboBox") == 0
			|| _stricmp(cls, "ComboLBox") == 0;
		// Group-box BUTTONs paint their frame + label via uxtheme, which
		// hardcodes black text regardless of WM_CTLCOLORSTATIC. Strip the
		// theme so classic painting honors SetTextColor in dark mode.
		bool is_groupbox = false;
		if (_stricmp(cls, "BUTTON") == 0)
		{
			LONG_PTR style = ::GetWindowLongPtrW(h, GWL_STYLE);
			if ((style & 0xF) == BS_GROUPBOX)
				is_groupbox = true;
		}
		if (is_combo || is_groupbox)
		{
			::SetWindowTheme(h, NULL, NULL);
		}
		else if (is_dark())
		{
			::SetWindowTheme(h, L"DarkMode_Explorer", NULL);
		}
		else
		{
			::SetWindowTheme(h, NULL, NULL);
		}
	}

	// Win32 subclass for listview headers in dialogs. The reflection-based
	// CThemedHeaderCtrl works only when the parent listview is an MFC CWnd
	// that participates in NOTIFY reflection AND no other handler in the
	// chain consumes the header's NM_CUSTOMDRAW first. CListCtrlEx (used by
	// every dialog listview here) provides reflection for its own custom
	// draw but the header's notify never bubbles back to the header in the
	// expected form. Bypass MFC entirely: subclass the header's HWND and
	// custom-paint WM_PAINT in dark mode. In light mode forward to the
	// original WindowProc for the system default look.
	static LRESULT CALLBACK dark_header_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

	static void install_dark_header_subclass(HWND h_header)
	{
		if (!h_header)
			return;
		static const wchar_t* k_subclassed = L"xcc.dark_header_subclass";
		static const wchar_t* k_orig_proc = L"xcc.dark_header_orig_proc";
		if (::GetPropW(h_header, k_subclassed))
			return;
		LONG_PTR orig = ::GetWindowLongPtrW(h_header, GWLP_WNDPROC);
		::SetPropW(h_header, k_orig_proc, reinterpret_cast<HANDLE>(orig));
		::SetWindowLongPtrW(h_header, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dark_header_proc));
		::SetPropW(h_header, k_subclassed, reinterpret_cast<HANDLE>(1));
		::InvalidateRect(h_header, NULL, TRUE);
	}

	static void paint_dark_header(HWND h)
	{
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(h, &ps);
		if (!hdc)
			return;
		RECT rc;
		::GetClientRect(h, &rc);
		// Background fill — slightly lighter than dialog bg so the header
		// reads as a distinct strip.
		HBRUSH bk = bg_alt_brush();
		::FillRect(hdc, &rc, bk);

		HFONT hf = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
		HGDIOBJ old_font = hf ? ::SelectObject(hdc, hf) : NULL;
		::SetTextColor(hdc, text());
		::SetBkMode(hdc, TRANSPARENT);

		const int n = static_cast<int>(::SendMessageW(h, HDM_GETITEMCOUNT, 0, 0));
		HPEN pen = ::CreatePen(PS_SOLID, 1, border());
		HGDIOBJ old_pen = ::SelectObject(hdc, pen);

		for (int i = 0; i < n; i++)
		{
			RECT ir;
			if (!::SendMessageW(h, HDM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&ir)))
				continue;
			HDITEMW hdi = {};
			wchar_t buf[256] = {};
			hdi.mask = HDI_TEXT;
			hdi.pszText = buf;
			hdi.cchTextMax = _countof(buf);
			::SendMessageW(h, HDM_GETITEMW, i, reinterpret_cast<LPARAM>(&hdi));

			RECT tr = ir;
			tr.left += 6; tr.right -= 6;
			::DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

			// Right-edge separator between columns.
			::MoveToEx(hdc, ir.right - 1, ir.top + 2, NULL);
			::LineTo(hdc, ir.right - 1, ir.bottom - 2);
		}

		// Bottom edge of the header.
		::MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
		::LineTo(hdc, rc.right, rc.bottom - 1);

		::SelectObject(hdc, old_pen);
		::DeleteObject(pen);
		if (old_font)
			::SelectObject(hdc, old_font);
		::EndPaint(h, &ps);
	}

	static void show_column_menu(HWND h_header, HWND h_listview, const char* lv_id, POINT screen_pt);

	static LRESULT CALLBACK dark_header_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_header_orig_proc";
		static const wchar_t* k_lv_id_prop = L"xcc.col_menu_lv_id";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		if (msg == WM_PAINT && is_dark())
		{
			paint_dark_header(h);
			return 0;
		}
		// Explorer-style per-column hide/show: right-click on the header
		// pops a checkable menu of column names. Only fires for headers
		// that opted in via enable_column_visibility_menu (lv_id prop set).
		if ((msg == WM_CONTEXTMENU || msg == WM_RBUTTONUP) && ::GetPropW(h, k_lv_id_prop))
		{
			const char* lv_id = reinterpret_cast<const char*>(::GetPropW(h, k_lv_id_prop));
			HWND h_lv = ::GetParent(h);
			POINT pt;
			if (msg == WM_CONTEXTMENU)
			{
				pt.x = GET_X_LPARAM(lp);
				pt.y = GET_Y_LPARAM(lp);
				if (pt.x == -1 && pt.y == -1)
				{
					RECT rc; ::GetWindowRect(h, &rc);
					pt.x = (rc.left + rc.right) / 2;
					pt.y = (rc.top + rc.bottom) / 2;
				}
			}
			else
			{
				pt.x = GET_X_LPARAM(lp);
				pt.y = GET_Y_LPARAM(lp);
				::ClientToScreen(h, &pt);
			}
			show_column_menu(h, h_lv, lv_id, pt);
			return 0;
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, L"xcc.dark_header_subclass");
			::RemovePropW(h, k_orig_proc);
			::RemovePropW(h, k_lv_id_prop);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
	}

	// Listview-group dark-mode overlay: Win10/11 ignore CDDS_GROUP
	// clrText/clrTextBk so the only way to fix the dim default group bar is
	// to repaint it over the system's output. Subclass the listview's
	// WM_PAINT; after the default proc has rendered the system group bar,
	// walk visible groups via LVM_GETGROUPRECT(LVGGR_HEADER) and FillRect +
	// DrawTextW the title strip in theme colors. Chevron + collapse arrow
	// are deliberately left to the system (we clip our paint a few pixels
	// shy of the right edge).
	static void paint_dark_group_overlay(HWND h_listview)
	{
		const int n = static_cast<int>(::SendMessageW(h_listview, LVM_GETGROUPCOUNT, 0, 0));
		if (n <= 0)
			return;
		HDC hdc = ::GetDC(h_listview);
		if (!hdc)
			return;
		RECT client;
		::GetClientRect(h_listview, &client);
		HBRUSH bk = bg_alt_brush();
		HFONT hf = reinterpret_cast<HFONT>(::SendMessageW(h_listview, WM_GETFONT, 0, 0));
		HGDIOBJ old_font = hf ? ::SelectObject(hdc, hf) : NULL;
		::SetTextColor(hdc, text());
		::SetBkMode(hdc, TRANSPARENT);
		HPEN pen = ::CreatePen(PS_SOLID, 1, border());
		HGDIOBJ old_pen = ::SelectObject(hdc, pen);
		// Open the TREEVIEW theme once for the chevron glyph. TVP_GLYPH
		// part 2 with GLPS_OPENED / GLPS_CLOSED is the OS-native triangle
		// used by Explorer's tree control — adapts to light/dark theme
		// automatically (we use DarkMode_Explorer per uxtheme app-mode
		// elsewhere). Falls back to a hand-drawn triangle if theme open
		// fails (classic theme / theming disabled).
		HTHEME h_tv = ::OpenThemeData(h_listview, L"TREEVIEW");
		SIZE glyph_sz = { 16, 16 };
		if (h_tv)
			::GetThemePartSize(h_tv, hdc, TVP_GLYPH, GLPS_OPENED, NULL, TS_DRAW, &glyph_sz);
		// Group ids are 0..n-1 by finish_search's assignment order. If a
		// group is scrolled out of view, LVM_GETGROUPRECT returns 0/empty
		// rect and we skip it.
		for (int gid = 0; gid < n; gid++)
		{
			RECT rc = {};
			rc.top = LVGGR_HEADER; // input field doubles as request type
			if (!::SendMessageW(h_listview, LVM_GETGROUPRECT, gid, reinterpret_cast<LPARAM>(&rc)))
				continue;
			// Clip to visible client area; skip wholly off-screen bars.
			if (rc.bottom <= client.top || rc.top >= client.bottom)
				continue;
			LVGROUP gi = {};
			gi.cbSize = sizeof(gi);
			gi.mask = LVGF_HEADER | LVGF_STATE;
			gi.stateMask = LVGS_COLLAPSED;
			wchar_t buf[256] = {};
			gi.pszHeader = buf;
			gi.cchHeader = _countof(buf);
			::SendMessageW(h_listview, LVM_GETGROUPINFO, gid, reinterpret_cast<LPARAM>(&gi));
			const bool collapsed = (gi.state & LVGS_COLLAPSED) != 0;
			::FillRect(hdc, &rc, bk);
			// Title text — left of the chevron area. 28 px reserved on the
			// right covers the 16-px theme glyph + 6 px margin + 6 px gap.
			RECT tr = rc;
			tr.left += 8;
			tr.right -= 28;
			if (tr.right > tr.left)
				::DrawTextW(hdc, buf, -1, &tr,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			// Draw the chevron using the OS TREEVIEW theme glyph (white in
			// dark mode, black in light — matches Explorer's tree
			// chevrons). Falls back to a hand-drawn triangle if theme
			// part is unavailable.
			RECT gr;
			gr.right = rc.right - 6;
			gr.left = gr.right - glyph_sz.cx;
			gr.top = (rc.top + rc.bottom - glyph_sz.cy) / 2;
			gr.bottom = gr.top + glyph_sz.cy;
			const int state = collapsed ? GLPS_CLOSED : GLPS_OPENED;
			if (h_tv)
			{
				::DrawThemeBackground(h_tv, hdc, TVP_GLYPH, state, &gr, NULL);
			}
			else
			{
				const int cx = (gr.left + gr.right) / 2;
				const int cy = (gr.top + gr.bottom) / 2;
				HBRUSH text_br = ::CreateSolidBrush(text());
				HGDIOBJ old_br = ::SelectObject(hdc, text_br);
				HPEN text_pen = ::CreatePen(PS_SOLID, 1, text());
				HGDIOBJ old_pen2 = ::SelectObject(hdc, text_pen);
				POINT tri[3];
				if (collapsed)
				{
					tri[0] = { cx - 3, cy - 4 };
					tri[1] = { cx - 3, cy + 4 };
					tri[2] = { cx + 3, cy };
				}
				else
				{
					tri[0] = { cx - 4, cy - 2 };
					tri[1] = { cx + 4, cy - 2 };
					tri[2] = { cx, cy + 3 };
				}
				::Polygon(hdc, tri, 3);
				::SelectObject(hdc, old_pen2);
				::DeleteObject(text_pen);
				::SelectObject(hdc, old_br);
				::DeleteObject(text_br);
			}
			// Thin separator at the bottom edge so the bar reads as a strip.
			::MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
			::LineTo(hdc, rc.right, rc.bottom - 1);
		}
		if (h_tv)
			::CloseThemeData(h_tv);
		::SelectObject(hdc, old_pen);
		::DeleteObject(pen);
		if (old_font)
			::SelectObject(hdc, old_font);
		::ReleaseDC(h_listview, hdc);
	}

	static LRESULT CALLBACK dark_listview_groups_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_lv_groups_orig_proc";
		static const wchar_t* k_subclassed = L"xcc.dark_lv_groups_subclass";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		if (msg == WM_PAINT && is_dark())
		{
			// Let the default proc paint everything first (rows, scrollbars,
			// system group bars, chevrons) then overlay our themed bars on
			// top. LVS_EX_DOUBLEBUFFER (set by apply_listview) keeps this
			// flicker-free: the system's paint composes to an off-screen
			// buffer and blits once; our GetDC overlay paints on the final
			// surface.
			LRESULT r = orig ? ::CallWindowProcW(orig, h, msg, wp, lp) : 0;
			paint_dark_group_overlay(h);
			return r;
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, k_subclassed);
			::RemovePropW(h, k_orig_proc);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
	}

	// Per-listview column-visibility persistence. Two registry values per
	// column under section "Theme": "col_<lv_id>_<i>_w" (last-visible
	// width) and "col_<lv_id>_<i>_h" (0 = visible, 1 = hidden).
	static std::string col_key(const char* lv_id, int col, const char* suffix)
	{
		std::string k = "col_";
		k += lv_id;
		k += "_";
		k += std::to_string(col);
		k += "_";
		k += suffix;
		return k;
	}

	static void show_column_menu(HWND h_header, HWND h_listview, const char* lv_id, POINT screen_pt)
	{
		const int n = static_cast<int>(::SendMessageW(h_header, HDM_GETITEMCOUNT, 0, 0));
		if (n <= 0)
			return;
		HMENU hm = ::CreatePopupMenu();
		if (!hm)
			return;
		for (int i = 0; i < n; i++)
		{
			wchar_t buf[256] = {};
			HDITEMW hdi = {};
			hdi.mask = HDI_TEXT;
			hdi.pszText = buf;
			hdi.cchTextMax = _countof(buf);
			::SendMessageW(h_header, HDM_GETITEMW, i, reinterpret_cast<LPARAM>(&hdi));
			const int cur_w = static_cast<int>(::SendMessageW(h_listview, LVM_GETCOLUMNWIDTH, i, 0));
			UINT flags = MF_STRING | (cur_w > 0 ? MF_CHECKED : MF_UNCHECKED);
			::AppendMenuW(hm, flags, static_cast<UINT_PTR>(1000 + i), buf[0] ? buf : L"(unnamed)");
		}
		const UINT cmd = static_cast<UINT>(::TrackPopupMenu(hm,
			TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
			screen_pt.x, screen_pt.y, 0, h_header, NULL));
		::DestroyMenu(hm);
		if (cmd < 1000 || cmd >= 1000u + static_cast<UINT>(n))
			return;
		const int col = static_cast<int>(cmd - 1000);
		const int cur_w = static_cast<int>(::SendMessageW(h_listview, LVM_GETCOLUMNWIDTH, col, 0));
		if (cur_w > 0)
		{
			// Hide: remember the current width then collapse.
			AfxGetApp()->WriteProfileInt("Theme", col_key(lv_id, col, "w").c_str(), cur_w);
			AfxGetApp()->WriteProfileInt("Theme", col_key(lv_id, col, "h").c_str(), 1);
			::SendMessageW(h_listview, LVM_SETCOLUMNWIDTH, col, 0);
		}
		else
		{
			// Show: restore saved width (fallback 80 if never saved).
			const int w = AfxGetApp()->GetProfileInt("Theme", col_key(lv_id, col, "w").c_str(), 80);
			AfxGetApp()->WriteProfileInt("Theme", col_key(lv_id, col, "h").c_str(), 0);
			::SendMessageW(h_listview, LVM_SETCOLUMNWIDTH, col, w > 0 ? w : 80);
		}
	}

	// Column display order persistence. Stored as a single comma-separated
	// string under "Theme\col_<lv_id>_order" (e.g. "0,2,1,3,4"). Restored on
	// dialog init after columns are inserted; rewritten on HDN_ENDDRAG.
	static void apply_persisted_column_order(HWND h_listview, const char* lv_id)
	{
		HWND h_header = reinterpret_cast<HWND>(::SendMessageW(h_listview, LVM_GETHEADER, 0, 0));
		const int n = h_header
			? static_cast<int>(::SendMessageW(h_header, HDM_GETITEMCOUNT, 0, 0))
			: 0;
		if (n <= 0)
			return;
		std::string key = "col_";
		key += lv_id;
		key += "_order";
		CString s = AfxGetApp()->GetProfileString("Theme", key.c_str(), "");
		if (s.IsEmpty())
			return;
		std::vector<int> order;
		order.reserve(n);
		int v = 0;
		bool have_digit = false;
		for (int i = 0; i <= s.GetLength(); i++)
		{
			char c = (i < s.GetLength()) ? static_cast<char>(s[i]) : ',';
			if (c >= '0' && c <= '9')
			{
				v = v * 10 + (c - '0');
				have_digit = true;
			}
			else if (c == ',' && have_digit)
			{
				order.push_back(v);
				v = 0;
				have_digit = false;
			}
		}
		// Validate: size matches, all indices in [0,n), no duplicates.
		if (static_cast<int>(order.size()) != n)
			return;
		std::vector<char> seen(n, 0);
		for (int idx : order)
		{
			if (idx < 0 || idx >= n || seen[idx])
				return;
			seen[idx] = 1;
		}
		::SendMessageW(h_listview, LVM_SETCOLUMNORDERARRAY,
			static_cast<WPARAM>(n), reinterpret_cast<LPARAM>(order.data()));
	}

	static void save_column_order(HWND h_listview, const char* lv_id)
	{
		HWND h_header = reinterpret_cast<HWND>(::SendMessageW(h_listview, LVM_GETHEADER, 0, 0));
		const int n = h_header
			? static_cast<int>(::SendMessageW(h_header, HDM_GETITEMCOUNT, 0, 0))
			: 0;
		if (n <= 0)
			return;
		std::vector<int> order(n, 0);
		if (!::SendMessageW(h_listview, LVM_GETCOLUMNORDERARRAY,
				static_cast<WPARAM>(n), reinterpret_cast<LPARAM>(order.data())))
			return;
		CString out;
		for (int i = 0; i < n; i++)
		{
			if (i)
				out += ',';
			CString tmp;
			tmp.Format("%d", order[i]);
			out += tmp;
		}
		std::string key = "col_";
		key += lv_id;
		key += "_order";
		AfxGetApp()->WriteProfileString("Theme", key.c_str(), out);
	}

	// Restore per-column widths previously saved by the user (either via
	// the hide menu's stash or via the column-state subclass on
	// HDN_ENDTRACK). Skips columns with no saved value so the
	// InsertColumn-default width is preserved.
	static void apply_persisted_column_widths(HWND h_listview, const char* lv_id)
	{
		HWND h_header = reinterpret_cast<HWND>(::SendMessageW(h_listview, LVM_GETHEADER, 0, 0));
		const int n = h_header
			? static_cast<int>(::SendMessageW(h_header, HDM_GETITEMCOUNT, 0, 0))
			: 0;
		for (int i = 0; i < n; i++)
		{
			// Hidden columns keep width 0 — handled by the hide-restore
			// loop in enable_column_visibility_menu, so skip here.
			const int hidden = AfxGetApp()->GetProfileInt("Theme", col_key(lv_id, i, "h").c_str(), 0);
			if (hidden)
				continue;
			const int saved = AfxGetApp()->GetProfileInt("Theme", col_key(lv_id, i, "w").c_str(), 0);
			if (saved > 0)
				::SendMessageW(h_listview, LVM_SETCOLUMNWIDTH, i,
					static_cast<LPARAM>(saved));
		}
	}

	// Listview subclass that watches the header's HDN_ENDDRAG (order
	// change) and HDN_ENDTRACK (resize complete) reflected through
	// WM_NOTIFY. Persists changes to the same "Theme\col_<lv_id>_*" keys
	// the hide menu uses. lv_id pointer comes from the header's
	// xcc.col_menu_lv_id prop (set by enable_column_visibility_menu).
	static LRESULT CALLBACK column_state_lv_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.col_state_orig_proc";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		auto call_orig = [&](UINT m, WPARAM w, LPARAM l) -> LRESULT {
			return orig
				? ::CallWindowProcW(orig, h, m, w, l)
				: ::DefWindowProcW(h, m, w, l);
		};
		if (msg == WM_NOTIFY)
		{
			NMHDR* nh = reinterpret_cast<NMHDR*>(lp);
			HWND h_header = reinterpret_cast<HWND>(::SendMessageW(h, LVM_GETHEADER, 0, 0));
			if (nh && nh->hwndFrom == h_header)
			{
				if (nh->code == HDN_ENDDRAG)
				{
					// Let the default proc commit the reorder first, then
					// snapshot the new order. HDN_ENDDRAG fires before the
					// header's internal order array updates, so we have to
					// post a self-message and read on the next pump.
					LRESULT r = call_orig(msg, wp, lp);
					const char* lv_id = reinterpret_cast<const char*>(
						::GetPropW(h_header, L"xcc.col_menu_lv_id"));
					if (lv_id)
					{
						// PostMessage so we read the order AFTER the
						// default handler's commit completes.
						::PostMessageW(h, WM_APP + 0x40, 0, 0);
					}
					return r;
				}
				if (nh->code == HDN_ENDTRACK)
				{
					LRESULT r = call_orig(msg, wp, lp);
					NMHEADERW* hdr = reinterpret_cast<NMHEADERW*>(lp);
					const char* lv_id = reinterpret_cast<const char*>(
						::GetPropW(h_header, L"xcc.col_menu_lv_id"));
					if (lv_id && hdr && hdr->iItem >= 0)
					{
						const int w = static_cast<int>(
							::SendMessageW(h, LVM_GETCOLUMNWIDTH, hdr->iItem, 0));
						if (w > 0)
							AfxGetApp()->WriteProfileInt("Theme",
								col_key(lv_id, hdr->iItem, "w").c_str(), w);
					}
					return r;
				}
			}
		}
		if (msg == WM_APP + 0x40)
		{
			HWND h_header = reinterpret_cast<HWND>(::SendMessageW(h, LVM_GETHEADER, 0, 0));
			const char* lv_id = h_header
				? reinterpret_cast<const char*>(::GetPropW(h_header, L"xcc.col_menu_lv_id"))
				: nullptr;
			if (lv_id)
				save_column_order(h, lv_id);
			return 0;
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, L"xcc.col_state_subclass");
			::RemovePropW(h, k_orig_proc);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return call_orig(msg, wp, lp);
	}

	static void install_column_state_subclass(HWND h_listview)
	{
		if (!h_listview)
			return;
		static const wchar_t* k_subclassed = L"xcc.col_state_subclass";
		if (::GetPropW(h_listview, k_subclassed))
			return;
		LONG_PTR orig = ::GetWindowLongPtrW(h_listview, GWLP_WNDPROC);
		::SetPropW(h_listview, L"xcc.col_state_orig_proc",
			reinterpret_cast<HANDLE>(orig));
		::SetWindowLongPtrW(h_listview, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(column_state_lv_proc));
		::SetPropW(h_listview, k_subclassed, reinterpret_cast<HANDLE>(1));
	}

	// Group-box BUTTONs (BS_GROUPBOX) paint their own border + caption via
	// either uxtheme (DarkMode_Explorer hardcodes dark-on-dark) or classic
	// painting (uses COLOR_BTNTEXT, ignores SetTextColor from parent's
	// WM_CTLCOLORSTATIC). Neither cooperates with dark mode, so subclass and
	// owner-paint the frame + caption ourselves.
	static void paint_dark_groupbox(HWND h)
	{
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(h, &ps);
		if (!hdc)
			return;
		RECT rc;
		::GetClientRect(h, &rc);

		HFONT hf = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
		HGDIOBJ old_font = hf ? ::SelectObject(hdc, hf) : NULL;

		// Measure caption text to position the frame and the caption strip.
		wchar_t buf[256] = {};
		::GetWindowTextW(h, buf, _countof(buf));
		SIZE sz = {};
		::GetTextExtentPoint32W(hdc, buf, lstrlenW(buf), &sz);

		// Frame top sits at vertical middle of caption text. The interior
		// (where children live) is intentionally NOT filled — children paint
		// themselves on top via WM_PAINT and we'd just be clobbering them.
		RECT frame = rc;
		frame.top += sz.cy / 2;

		// Paint top edge as two segments (left of caption, right of caption)
		// so the caption interrupts the line, like a real group box.
		HPEN pen = ::CreatePen(PS_SOLID, 1, border());
		HGDIOBJ old_pen = ::SelectObject(hdc, pen);
		const int cap_left = rc.left + 9;
		const int cap_right = cap_left + sz.cx + 4;
		// Top edge segments
		::MoveToEx(hdc, frame.left, frame.top, NULL);
		::LineTo(hdc, cap_left, frame.top);
		::MoveToEx(hdc, cap_right, frame.top, NULL);
		::LineTo(hdc, frame.right, frame.top);
		// Left edge
		::MoveToEx(hdc, frame.left, frame.top, NULL);
		::LineTo(hdc, frame.left, frame.bottom - 1);
		// Right edge
		::MoveToEx(hdc, frame.right - 1, frame.top, NULL);
		::LineTo(hdc, frame.right - 1, frame.bottom - 1);
		// Bottom edge
		::MoveToEx(hdc, frame.left, frame.bottom - 1, NULL);
		::LineTo(hdc, frame.right, frame.bottom - 1);
		::SelectObject(hdc, old_pen);
		::DeleteObject(pen);

		// Caption text in light color (no background fill — the parent dialog
		// already painted bg behind us via WM_ERASEBKGND).
		if (buf[0])
		{
			::SetTextColor(hdc, text());
			::SetBkMode(hdc, TRANSPARENT);
			RECT tr;
			tr.left = cap_left + 2;
			tr.top = rc.top;
			tr.right = cap_right - 2;
			tr.bottom = tr.top + sz.cy;
			::DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}

		if (old_font)
			::SelectObject(hdc, old_font);
		::EndPaint(h, &ps);
	}

	static LRESULT CALLBACK dark_groupbox_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_groupbox_orig_proc";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		if (is_dark() && msg == WM_PAINT)
		{
			paint_dark_groupbox(h);
			return 0;
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, L"xcc.dark_groupbox_subclass");
			::RemovePropW(h, k_orig_proc);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
	}

	static void install_dark_groupbox_subclass(HWND h)
	{
		if (!h)
			return;
		static const wchar_t* k_subclassed = L"xcc.dark_groupbox_subclass";
		static const wchar_t* k_orig_proc = L"xcc.dark_groupbox_orig_proc";
		if (::GetPropW(h, k_subclassed))
			return;
		LONG_PTR orig = ::GetWindowLongPtrW(h, GWLP_WNDPROC);
		::SetPropW(h, k_orig_proc, reinterpret_cast<HANDLE>(orig));
		::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dark_groupbox_proc));
		::SetPropW(h, k_subclassed, reinterpret_cast<HANDLE>(1));
		::InvalidateRect(h, NULL, TRUE);
	}

	// Radio / checkbox BUTTONs paint their indicator glyph (the circle dot or
	// the box tick) via uxtheme's Button parts. Even with DarkMode_Explorer
	// applied, the *label* text is drawn by the same uxtheme call using the
	// theme's hardcoded foreground color — which is black, regardless of any
	// SetTextColor we apply via WM_CTLCOLORSTATIC. So in dark mode the labels
	// come out unreadable.
	//
	// Notepad++ pattern: subclass each radio/checkbox, intercept WM_PAINT,
	// use DrawThemeBackground for ONLY the indicator rect (theme draws the
	// dark-mode-correct circle/check), then DrawText the label ourselves with
	// theme::text() so the label honors dark mode. Light mode falls through
	// to the original wndproc untouched.
	static int button_state_id(HWND h, bool is_radio)
	{
		// Per vsstyle.h: RBS_UNCHECKED*=1..4, RBS_CHECKED*=5..8;
		// CBS_UNCHECKED*=1..4, CBS_CHECKED*=5..8.
		const LRESULT chk = ::SendMessageW(h, BM_GETCHECK, 0, 0);
		const bool checked = chk == BST_CHECKED;
		const bool disabled = !::IsWindowEnabled(h);
		const bool hot = ::SendMessageW(h, BM_GETSTATE, 0, 0) & BST_HOT;
		const bool pushed = ::SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED;
		int base = checked ? 5 : 1;	// CBS_CHECKED*=5, CBS_UNCHECKED*=1
		if (disabled)     base += 3;
		else if (pushed)  base += 2;
		else if (hot)     base += 1;
		(void)is_radio;	// state ids are identical for BP_RADIOBUTTON & BP_CHECKBOX
		return base;
	}

	static void paint_dark_button(HWND h, bool is_radio)
	{
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(h, &ps);
		if (!hdc)
			return;
		RECT rc;
		::GetClientRect(h, &rc);

		// Fill background with parent's dark bg so we don't get a flash of
		// the system COLOR_BTNFACE. Parent's WM_CTLCOLORSTATIC already
		// returns bg_brush() so any non-painted pixels match too, but we
		// also need the indicator's bounding rect to be clean before
		// DrawThemeBackground composites on top of it.
		HBRUSH bg_br = bg_brush();
		::FillRect(hdc, &rc, bg_br);

		HFONT hf = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
		HGDIOBJ old_font = hf ? ::SelectObject(hdc, hf) : NULL;

		// Open the Button theme and ask for the natural size of the indicator
		// glyph. Falls back to a 13-px square (the classic Win32 indicator
		// size) if uxtheme isn't loaded or part lookup fails.
		HTHEME ht = ::OpenThemeData(h, L"Button");
		const int part = is_radio ? 2 /*BP_RADIOBUTTON*/ : 3 /*BP_CHECKBOX*/;
		const int state = button_state_id(h, is_radio);
		SIZE glyph_sz = { 13, 13 };
		if (ht)
			::GetThemePartSize(ht, hdc, part, state, NULL, TS_DRAW, &glyph_sz);

		// Indicator is left-anchored, vertically centered.
		RECT glyph_rc;
		glyph_rc.left = rc.left;
		glyph_rc.top = rc.top + (rc.bottom - rc.top - glyph_sz.cy) / 2;
		glyph_rc.right = glyph_rc.left + glyph_sz.cx;
		glyph_rc.bottom = glyph_rc.top + glyph_sz.cy;

		if (ht)
		{
			::DrawThemeBackground(ht, hdc, part, state, &glyph_rc, NULL);
			::CloseThemeData(ht);
		}
		else
		{
			// Classic fallback: draw a simple framed circle/square so the
			// control is at least visible.
			HPEN pen = ::CreatePen(PS_SOLID, 1, border());
			HGDIOBJ old_pen = ::SelectObject(hdc, pen);
			HBRUSH br = bg_alt_brush();
			HGDIOBJ old_br = ::SelectObject(hdc, br);
			if (is_radio)
				::Ellipse(hdc, glyph_rc.left, glyph_rc.top, glyph_rc.right, glyph_rc.bottom);
			else
				::Rectangle(hdc, glyph_rc.left, glyph_rc.top, glyph_rc.right, glyph_rc.bottom);
			::SelectObject(hdc, old_br);
			::SelectObject(hdc, old_pen);
			::DeleteObject(pen);
		}

		// Label text in the remaining rect to the right of the indicator,
		// with a small gap. Disabled labels use the dimmed text color so
		// they read as inactive.
		wchar_t buf[256] = {};
		::GetWindowTextW(h, buf, _countof(buf));
		if (buf[0])
		{
			RECT tr = rc;
			tr.left = glyph_rc.right + 4;
			::SetBkMode(hdc, TRANSPARENT);
			::SetTextColor(hdc, ::IsWindowEnabled(h) ? text() : text_dim());
			// Match the system Button's text alignment: vertically centered,
			// horizontally left for both radio and checkbox unless the style
			// asks otherwise. BS_RIGHT / BS_CENTER could be honored here, but
			// every radio/checkbox in this codebase is BS_LEFT (the default).
			UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
			LONG_PTR bs = ::GetWindowLongPtrW(h, GWL_STYLE);
			if (bs & BS_MULTILINE) fmt = DT_LEFT | DT_WORDBREAK;
			::DrawTextW(hdc, buf, -1, &tr, fmt);
		}

		// No focus rectangle. The File/Computed radios above use plain LTEXT
		// labels (statics) that route clicks back through STN_CLICKED, so they
		// never receive keyboard focus and never grew a focus box. To match
		// that visual exactly, we skip drawing one here entirely.

		if (old_font)
			::SelectObject(hdc, old_font);
		::EndPaint(h, &ps);
	}

	static LRESULT CALLBACK dark_button_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_button_orig_proc";
		static const wchar_t* k_is_radio  = L"xcc.dark_button_is_radio";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		const bool is_radio = ::GetPropW(h, k_is_radio) != NULL;
		if (is_dark())
		{
			// Repaint on focus, enable, and check transitions so the indicator
			// state stays in sync.
			if (msg == WM_PAINT)
			{
				paint_dark_button(h, is_radio);
				return 0;
			}
			if (msg == WM_ERASEBKGND)
			{
				// Suppress default erase so we don't briefly flash the system
				// button face. Our WM_PAINT FillRect handles the bg.
				return 1;
			}
			if (msg == BM_SETCHECK || msg == BM_SETSTATE
				|| msg == WM_SETFOCUS || msg == WM_KILLFOCUS
				|| msg == WM_ENABLE)
			{
				LRESULT r = orig
					? ::CallWindowProcW(orig, h, msg, wp, lp)
					: ::DefWindowProcW(h, msg, wp, lp);
				::InvalidateRect(h, NULL, FALSE);
				return r;
			}
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, L"xcc.dark_button_subclass");
			::RemovePropW(h, k_orig_proc);
			::RemovePropW(h, k_is_radio);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
	}

	static void install_dark_button_subclass(HWND h, bool is_radio)
	{
		if (!h)
			return;
		static const wchar_t* k_subclassed = L"xcc.dark_button_subclass";
		static const wchar_t* k_orig_proc  = L"xcc.dark_button_orig_proc";
		static const wchar_t* k_is_radio   = L"xcc.dark_button_is_radio";
		if (::GetPropW(h, k_subclassed))
			return;
		LONG_PTR orig = ::GetWindowLongPtrW(h, GWLP_WNDPROC);
		::SetPropW(h, k_orig_proc, reinterpret_cast<HANDLE>(orig));
		if (is_radio)
			::SetPropW(h, k_is_radio, reinterpret_cast<HANDLE>(1));
		::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dark_button_proc));
		::SetPropW(h, k_subclassed, reinterpret_cast<HANDLE>(1));
		::InvalidateRect(h, NULL, TRUE);
	}

	// ---------- Edit-control dark border (NppDarkMode CustomBorderSubclass port) ----------
	//
	// Standard Edit controls (single-line WC_EDIT) with WS_EX_CLIENTEDGE paint
	// a hardcoded light 1px sunken frame in dark mode — uxtheme draws the
	// frame even after AllowDarkModeForWindow + DarkMode_Explorer, producing
	// the "light-bordered edit on dark dialog" look. Filter edits in
	// CLoadPalDlg / CSearchInPaneDlg made this glaring.
	//
	// Fix mirrors NppDarkMode.cpp:1849-1995 (CustomBorderSubclass) + the
	// install dispatcher at :3087-3128: strip WS_EX_CLIENTEDGE in dark mode
	// so uxtheme stops drawing the light frame, then own WM_NCPAINT to
	// repaint the border ourselves (inner 1px in bg(), outer 1px in
	// border() or accent() on focus/hover). WM_NCCALCSIZE inflates the
	// client rect inward by SM_CXEDGE/CYEDGE so the text doesn't kiss our
	// border. WM_MOUSEMOVE/WM_MOUSELEAVE track hover via TrackMouseEvent for
	// the accent-color hover state. WS_EX_CLIENTEDGE is restored on theme
	// flip back to light by light-mode default path (uninstall not needed —
	// the subclass no-ops when !is_dark()).
	//
	// Edge metrics are queried per-process from SM_CXEDGE / SM_CYEDGE /
	// SM_CXVSCROLL / SM_CYVSCROLL. Mixer doesn't ship DPI-per-monitor (the
	// app is Win32-MFC vintage), so the once-at-install snapshot is fine —
	// no WM_DPICHANGED handling like Npp needs.
	struct dark_edit_metrics
	{
		LONG x_edge   = ::GetSystemMetrics(SM_CXEDGE);
		LONG y_edge   = ::GetSystemMetrics(SM_CYEDGE);
		LONG x_scroll = ::GetSystemMetrics(SM_CXVSCROLL);
		LONG y_scroll = ::GetSystemMetrics(SM_CYVSCROLL);
	};

	static LRESULT CALLBACK dark_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_edit_orig_proc";
		static const wchar_t* k_metrics   = L"xcc.dark_edit_metrics";
		static const wchar_t* k_hot       = L"xcc.dark_edit_hot";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		dark_edit_metrics* m = reinterpret_cast<dark_edit_metrics*>(::GetPropW(h, k_metrics));

		if (is_dark() && m)
		{
			switch (msg)
			{
			case WM_NCPAINT:
			{
				// Let the default proc paint the non-client area first so any
				// scrollbar / system widgets render, then overpaint the border
				// rect with our colors. Same order Npp uses.
				if (orig) ::CallWindowProcW(orig, h, msg, wp, lp);
				HDC hdc = ::GetWindowDC(h);
				if (!hdc) return 0;
				RECT rc{};
				::GetClientRect(h, &rc);
				// Expand client rect to window rect by adding the edge inset we
				// stole back in WM_NCCALCSIZE, plus the scrollbar widths if the
				// control has WS_VSCROLL/WS_HSCROLL.
				rc.right += 2 * m->x_edge;
				rc.bottom += 2 * m->y_edge;
				const LONG_PTR style = ::GetWindowLongPtrW(h, GWL_STYLE);
				if (style & WS_VSCROLL) rc.right += m->x_scroll;
				if (style & WS_HSCROLL) rc.bottom += m->y_scroll;

				const bool enabled = ::IsWindowEnabled(h) == TRUE;
				const bool has_focus = ::GetFocus() == h;
				const bool is_hot = ::GetPropW(h, k_hot) != NULL;

				// Inner 1px: paint with dialog bg so the frame visually melts
				// into the surrounding edit fill instead of producing a double
				// border. Matches Npp's enabled vs disabled split.
				HPEN inner = ::CreatePen(PS_SOLID, 1, enabled ? bg() : bg_alt());
				RECT rc_inner = rc;
				::InflateRect(&rc_inner, -1, -1);
				HGDIOBJ old_pen = ::SelectObject(hdc, inner);
				HGDIOBJ old_brush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
				::Rectangle(hdc, rc_inner.left, rc_inner.top, rc_inner.right, rc_inner.bottom);

				// Outer 1px: border() normally, accent() on focus or hover.
				HPEN outer = ::CreatePen(PS_SOLID, 1,
					(has_focus || is_hot) ? accent() :
					(enabled ? border() : bg_alt()));
				::SelectObject(hdc, outer);
				::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

				::SelectObject(hdc, old_brush);
				::SelectObject(hdc, old_pen);
				::DeleteObject(inner);
				::DeleteObject(outer);
				::ReleaseDC(h, hdc);
				return 0;
			}
			case WM_NCCALCSIZE:
			{
				// Steal the area uxtheme would have used for its light frame
				// so we have somewhere to paint our dark border. Without this,
				// our WM_NCPAINT would draw over the first row of text pixels.
				LPRECT prc = reinterpret_cast<LPRECT>(lp);
				if (prc)
					::InflateRect(prc, -m->x_edge, -m->y_edge);
				break;
			}
			case WM_MOUSEMOVE:
			{
				if (::GetFocus() == h) break; // focus owns the accent already
				TRACKMOUSEEVENT tme{};
				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = h;
				tme.dwHoverTime = HOVER_DEFAULT;
				::TrackMouseEvent(&tme);
				if (!::GetPropW(h, k_hot))
				{
					::SetPropW(h, k_hot, reinterpret_cast<HANDLE>(1));
					::SetWindowPos(h, NULL, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
				}
				break;
			}
			case WM_MOUSELEAVE:
			{
				if (::GetPropW(h, k_hot))
				{
					::RemovePropW(h, k_hot);
					::SetWindowPos(h, NULL, 0, 0, 0, 0,
						SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
				}
				break;
			}
			case WM_SETFOCUS:
			case WM_KILLFOCUS:
			{
				// Repaint the border so the focus state's accent color
				// appears/disappears. CallWindowProc first so the caret /
				// selection state updates, then frame-invalidate.
				LRESULT r = orig
					? ::CallWindowProcW(orig, h, msg, wp, lp)
					: ::DefWindowProcW(h, msg, wp, lp);
				::SetWindowPos(h, NULL, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
				return r;
			}
			}
		}
		if (msg == WM_NCDESTROY)
		{
			delete m;
			::RemovePropW(h, k_metrics);
			::RemovePropW(h, k_hot);
			::RemovePropW(h, L"xcc.dark_edit_subclass");
			::RemovePropW(h, k_orig_proc);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
	}

	static void install_dark_edit_subclass(HWND h)
	{
		if (!h) return;
		static const wchar_t* k_subclassed = L"xcc.dark_edit_subclass";
		static const wchar_t* k_orig_proc  = L"xcc.dark_edit_orig_proc";
		static const wchar_t* k_metrics    = L"xcc.dark_edit_metrics";
		if (::GetPropW(h, k_subclassed))
			return;
		LONG_PTR orig = ::GetWindowLongPtrW(h, GWLP_WNDPROC);
		::SetPropW(h, k_orig_proc, reinterpret_cast<HANDLE>(orig));
		::SetPropW(h, k_metrics, reinterpret_cast<HANDLE>(new dark_edit_metrics));
		::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dark_edit_proc));
		::SetPropW(h, k_subclassed, reinterpret_cast<HANDLE>(1));
		// Force a non-client recalc + paint NOW so our border shows on the
		// initial dialog paint. Without this, sync_edit_client_edge's earlier
		// SWP_FRAMECHANGED happened before our subclass existed, so the first
		// WM_NCPAINT went to the default proc (which drew nothing because
		// WS_EX_CLIENTEDGE was already stripped). Border only appeared after
		// the user tabbed away and back. Now the border paints on open.
		::SetWindowPos(h, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED |
			SWP_NOACTIVATE);
		::RedrawWindow(h, NULL, NULL,
			RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
	}

	// Theme-aware sync of the edit's WS_EX_CLIENTEDGE bit. In dark mode the
	// uxtheme-drawn light sunken frame fights our WM_NCPAINT, so strip the
	// style. In light mode restore it so the control gets the standard
	// system look. SWP_FRAMECHANGED forces non-client recalc + repaint.
	// Per Npp this is applied to every edit, not only the subclassed ones —
	// the WS_EX_CLIENTEDGE removal alone is most of the visible improvement,
	// and the subclass painting layers a proper border on top.
	static void sync_edit_client_edge(HWND h)
	{
		if (!h) return;
		LONG_PTR ex = ::GetWindowLongPtrW(h, GWL_EXSTYLE);
		const bool want_edge = !is_dark();
		const bool has_edge = (ex & WS_EX_CLIENTEDGE) != 0;
		if (want_edge == has_edge) return;
		if (want_edge) ex |= WS_EX_CLIENTEDGE;
		else           ex &= ~WS_EX_CLIENTEDGE;
		::SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
		::SetWindowPos(h, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED |
			SWP_NOACTIVATE);
	}

	// Public wrapper: theme a standalone Edit (one not reached by
	// apply_dialog's child walk). Same order as theme_child_proc's Edit
	// branch — subclass first so the SWP_FRAMECHANGED from sync routes through
	// our wndproc — plus apply_window for dark scrollbars/consistency.
	void apply_edit(HWND h_edit)
	{
		if (!h_edit)
			return;
		apply_window(h_edit);
		if (is_dark())
			install_dark_edit_subclass(h_edit);
		sync_edit_client_edge(h_edit);
		::InvalidateRect(h_edit, NULL, TRUE);
	}

	// ---------- Trackbar (msctls_trackbar32) dark custom-draw ----------
	//
	// Trackbars (CSliderCtrl) paint a hardcoded light channel + thumb on
	// dark dialogs because uxtheme's trackbar parts ignore the dark mode
	// flag. NppDarkMode handles this via parent-side NM_CUSTOMDRAW
	// reflection (NppDarkMode.cpp:3404-3481 darkTrackBarNotifyCustomDraw,
	// dispatched from a parent SetWindowSubclass that intercepts
	// WM_NOTIFY).
	//
	// We do the same — install a parent-side GWLP_WNDPROC subclass on each
	// dialog themed via apply_dialog, intercept WM_NOTIFY → NM_CUSTOMDRAW
	// for trackbar children, fill the channel + thumb with dark colors,
	// return CDRF_SKIPDEFAULT so uxtheme doesn't repaint over us. Tick
	// marks aren't currently styled — all Mixer trackbars use TBS_NOTICKS
	// so there's nothing visible to fix there.
	static LRESULT trackbar_custom_draw(NMCUSTOMDRAW* nmcd)
	{
		switch (nmcd->dwDrawStage)
		{
		case CDDS_PREPAINT:
			return is_dark() ? CDRF_NOTIFYITEMDRAW : CDRF_DODEFAULT;
		case CDDS_ITEMPREPAINT:
			if (!is_dark())
				return CDRF_DODEFAULT;
			switch (nmcd->dwItemSpec)
			{
			case TBCD_CHANNEL:
			{
				// Channel: filled rect in bg_alt (the "elevated surface"
				// color, matches edit/listview bg) with a 1px border in
				// theme::border() so the channel reads as a distinct
				// rail rather than a flat band.
				const bool enabled = ::IsWindowEnabled(nmcd->hdr.hwndFrom) == TRUE;
				::FillRect(nmcd->hdc, &nmcd->rc, enabled ? bg_alt_brush() : bg_brush());
				HPEN pen = ::CreatePen(PS_SOLID, 1, border());
				HGDIOBJ op = ::SelectObject(nmcd->hdc, pen);
				HGDIOBJ ob = ::SelectObject(nmcd->hdc, ::GetStockObject(NULL_BRUSH));
				::Rectangle(nmcd->hdc, nmcd->rc.left, nmcd->rc.top,
					nmcd->rc.right, nmcd->rc.bottom);
				::SelectObject(nmcd->hdc, ob);
				::SelectObject(nmcd->hdc, op);
				::DeleteObject(pen);
				return CDRF_SKIPDEFAULT;
			}
			case TBCD_THUMB:
			{
				// Thumb: solid accent fill with a 1px border. Win11 trackbars
				// use a circular thumb but the rect Win32 hands us is the
				// bounding box; FillRect + border is close enough to the
				// vanilla msctls look and reads correctly in dark.
				const bool enabled = ::IsWindowEnabled(nmcd->hdr.hwndFrom) == TRUE;
				const bool pressed = (nmcd->uItemState & CDIS_SELECTED) != 0;
				COLORREF fill_c = !enabled ? bg_alt()
					: pressed ? text() : accent();
				HBRUSH fill = ::CreateSolidBrush(fill_c);
				::FillRect(nmcd->hdc, &nmcd->rc, fill);
				::DeleteObject(fill);
				HPEN pen = ::CreatePen(PS_SOLID, 1, enabled ? text() : border());
				HGDIOBJ op = ::SelectObject(nmcd->hdc, pen);
				HGDIOBJ ob = ::SelectObject(nmcd->hdc, ::GetStockObject(NULL_BRUSH));
				::Rectangle(nmcd->hdc, nmcd->rc.left, nmcd->rc.top,
					nmcd->rc.right, nmcd->rc.bottom);
				::SelectObject(nmcd->hdc, ob);
				::SelectObject(nmcd->hdc, op);
				::DeleteObject(pen);
				return CDRF_SKIPDEFAULT;
			}
			default:
				return CDRF_DODEFAULT;
			}
		}
		return CDRF_DODEFAULT;
	}

	// Parent-dialog subclass that intercepts trackbar NM_CUSTOMDRAW and
	// routes it through trackbar_custom_draw. Installed by apply_dialog on
	// every themed dialog. Idempotent.
	static LRESULT CALLBACK dark_parent_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_parent_orig_proc";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		if (msg == WM_NOTIFY)
		{
			NMHDR* hdr = reinterpret_cast<NMHDR*>(lp);
			if (hdr && hdr->code == NM_CUSTOMDRAW && hdr->hwndFrom)
			{
				char cls[32];
				::GetClassNameA(hdr->hwndFrom, cls, sizeof(cls));
				if (_stricmp(cls, "msctls_trackbar32") == 0)
				{
					LRESULT r = trackbar_custom_draw(
						reinterpret_cast<NMCUSTOMDRAW*>(lp));
					if (r != CDRF_DODEFAULT)
						return r;
				}
			}
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, L"xcc.dark_parent_subclass");
			::RemovePropW(h, k_orig_proc);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
	}

	static void install_dark_parent_subclass(HWND h)
	{
		if (!h) return;
		static const wchar_t* k_subclassed = L"xcc.dark_parent_subclass";
		static const wchar_t* k_orig_proc  = L"xcc.dark_parent_orig_proc";
		if (::GetPropW(h, k_subclassed))
			return;
		LONG_PTR orig = ::GetWindowLongPtrW(h, GWLP_WNDPROC);
		::SetPropW(h, k_orig_proc, reinterpret_cast<HANDLE>(orig));
		::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dark_parent_proc));
		::SetPropW(h, k_subclassed, reinterpret_cast<HANDLE>(1));
	}

	// Public alias for non-dialog parents (e.g. CXCCFileView's player band).
	void install_trackbar_parent_subclass(HWND h_parent)
	{
		install_dark_parent_subclass(h_parent);
	}

	void apply_listview(HWND h_listview)
	{
		if (!h_listview)
			return;
		apply_window(h_listview);
		HWND h_header = reinterpret_cast<HWND>(::SendMessage(h_listview, LVM_GETHEADER, 0, 0));
		if (h_header)
		{
			// AllowDarkModeForWindow on the header itself — without this, the
			// SetWindowTheme(L"DarkMode_ItemsView") silently no-ops because
			// uxtheme refuses dark themes for windows that haven't opted in.
			// apply_window only opted in the listview, not its header child.
			load_uxtheme();
			if (p_AllowDarkModeForWindow)
				p_AllowDarkModeForWindow(h_header, is_dark() ? TRUE : FALSE);
			if (is_dark())
			{
				// On Win10/11 the dark-aware variant. ItemsView alone leaves the header light.
				if (::SetWindowTheme(h_header, L"DarkMode_ItemsView", NULL) != S_OK)
					::SetWindowTheme(h_header, L"ItemsView", NULL);
			}
			else
			{
				::SetWindowTheme(h_header, NULL, NULL);
			}
			// SetWindowTheme(DarkMode_ItemsView) repaints the header's
			// background dark but the system common-control still draws the
			// column text with COLOR_BTNTEXT, so labels stay near-black on
			// the dark fill. Subclass with CThemedHeaderCtrl which reflects
			// NM_CUSTOMDRAW and paints both bg and text from the theme. We
			// track per-HWND via a window property so we only subclass once
			// per header (idempotent across re-themes); the CThemedHeaderCtrl
			// instance is intentionally heap-leaked — process exit reclaims it.
			install_dark_header_subclass(h_header);
			::InvalidateRect(h_header, NULL, TRUE);
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
		// Always-on double-buffer: the listview composes each paint into an
		// offscreen DC and blits in one shot, eliminating row-by-row tear
		// during scrolls and resizes. Composes correctly with the existing
		// CDDS_ITEMPREPAINT (row bg) and CDDS_ITEMPOSTPAINT (manual gridlines)
		// paths in CListCtrlEx and CXCCMixerView — the back buffer receives
		// both passes before the final blit. No measurable perf cost.
		DWORD ex = static_cast<DWORD>(::SendMessage(h_listview, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0));
		ex |= LVS_EX_DOUBLEBUFFER;
		::SendMessage(h_listview, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, ex);
		::InvalidateRect(h_listview, NULL, TRUE);
	}

	// ---------- Dialog dark-mode ----------

	// Apply dark-mode treatment to a treeview: dark explorer-style scrollbars
	// and dark item background/text. Without this the tree paints with
	// COLOR_WINDOW (white) regardless of SetWindowTheme.
	static void apply_treeview(HWND h_tree)
	{
		if (!h_tree)
			return;
		apply_window(h_tree);
		if (is_dark())
		{
			TreeView_SetBkColor(h_tree, bg());
			TreeView_SetTextColor(h_tree, text());
			TreeView_SetLineColor(h_tree, border());
		}
		else
		{
			TreeView_SetBkColor(h_tree, ::GetSysColor(COLOR_WINDOW));
			TreeView_SetTextColor(h_tree, ::GetSysColor(COLOR_WINDOWTEXT));
			TreeView_SetLineColor(h_tree, CLR_DEFAULT);
		}
		::InvalidateRect(h_tree, NULL, TRUE);
	}

	// Per-window enumerator: applies dark-mode theming to a child control.
	// Listviews and treeviews get class-specific subtreatments. Everything
	// else gets the generic DarkMode_Explorer treatment, which buys dark
	// scrollbars and dark scroll arrows on the control frame.
	static BOOL CALLBACK theme_child_proc(HWND h, LPARAM)
	{
		char cls[64];
		::GetClassNameA(h, cls, sizeof(cls));
		if (_stricmp(cls, "SysListView32") == 0)
			apply_listview(h);
		else if (_stricmp(cls, "SysTreeView32") == 0)
			apply_treeview(h);
		else if (_stricmp(cls, "msctls_statusbar32") == 0)
		{
			apply_window(h);
			// SB_SETBKCOLOR only sticks if the status bar isn't themed; pulling
			// off the explorer theme lets our background color show through.
			::SetWindowTheme(h, L"", L"");
			::SendMessage(h, SB_SETBKCOLOR,
				0, is_dark() ? bg() : CLR_DEFAULT);
			::InvalidateRect(h, NULL, TRUE);
		}
		else
			apply_window(h);
		// BUTTON-class controls need per-style subclassing in dark mode.
		// uxtheme + classic both ignore SetTextColor for groupbox captions
		// and radio/checkbox labels, so we owner-paint those ourselves.
		// Pushbuttons (BS_PUSHBUTTON / BS_DEFPUSHBUTTON) are left alone —
		// DarkMode_Explorer paints them acceptably.
		if (_stricmp(cls, "BUTTON") == 0)
		{
			LONG_PTR style = ::GetWindowLongPtrW(h, GWL_STYLE);
			const LONG_PTR sub = style & 0xF;
			if (sub == BS_GROUPBOX)
				install_dark_groupbox_subclass(h);
			else if (sub == BS_AUTORADIOBUTTON || sub == BS_RADIOBUTTON)
				install_dark_button_subclass(h, true);
			else if (sub == BS_AUTOCHECKBOX || sub == BS_CHECKBOX || sub == BS_AUTO3STATE || sub == BS_3STATE)
				install_dark_button_subclass(h, false);
		}
		// ComboBoxes get the Notepad++-style WM_PAINT subclass for proper
		// dark/light transition. subclass_combobox is idempotent and
		// refuses owner-draw combos (their parents handle painting).
		if (_stricmp(cls, "ComboBox") == 0)
			subclass_combobox(h);
		// Standard Edit controls — strip WS_EX_CLIENTEDGE in dark and
		// install our own border subclass. Ported from NppDarkMode's
		// CustomBorderSubclass — see install_dark_edit_subclass comment.
		// Combobox-internal edit children (CBS_DROPDOWN) come through here
		// too, but they're handled by combobox_subclass already; skip them
		// here so we don't double-paint the combo's border. ID -1 is the
		// magic value Win32 uses for combo's edit child.
		if (_stricmp(cls, "Edit") == 0)
		{
			const int ctrl_id = ::GetDlgCtrlID(h);
			if (ctrl_id != -1)
			{
				// Order matters: install the subclass BEFORE syncing
				// WS_EX_CLIENTEDGE so the SWP_FRAMECHANGED that
				// sync_edit_client_edge triggers routes WM_NCPAINT through
				// our wndproc. Otherwise the first paint happens with the
				// default proc + no border style and stays blank until the
				// user changes focus.
				if (is_dark())
					install_dark_edit_subclass(h);
				sync_edit_client_edge(h);
			}
		}
		// Force redraw so any cached non-client decoration repaints with the
		// new theme.
		::InvalidateRect(h, NULL, TRUE);
		return TRUE;
	}

	void apply_dialog(HWND h_dialog)
	{
		if (!h_dialog)
			return;
		// Title bar (DWM immersive) + dark scrollbars/headers on the dialog
		// frame itself.
		apply_titlebar(h_dialog);
		apply_window(h_dialog);
		// Install the parent-side WM_NOTIFY interceptor that handles trackbar
		// (msctls_trackbar32) NM_CUSTOMDRAW so sliders render dark. Idempotent.
		// Must run before EnumChildWindows so a trackbar's first paint goes
		// through our handler.
		install_dark_parent_subclass(h_dialog);
		// Walk every descendant and apply per-control theming.
		::EnumChildWindows(h_dialog, theme_child_proc, 0);
		// Force a full repaint so backgrounds picked up via WM_CTLCOLOR* in
		// the new mode get rendered immediately.
		::InvalidateRect(h_dialog, NULL, TRUE);
	}

	HBRUSH on_ctl_color(HDC dc, HWND hwnd_ctl, UINT msg)
	{
		if (!is_dark() || !dc)
			return NULL;
		// Default: dark window background + primary text. WM_CTLCOLOREDIT and
		// WM_CTLCOLORLISTBOX use bg_alt to nudge those control surfaces a hair
		// lighter than the dialog itself, the same way Win11 dark-mode lists/
		// edits sit on a slightly elevated surface.
		COLORREF bk = bg();
		HBRUSH br = bg_brush();
		switch (msg)
		{
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			bk = bg_alt();
			br = bg_alt_brush();
			break;
		case WM_CTLCOLORSCROLLBAR:
			// Scrollbars don't really use this — the dark scrollbar comes from
			// SetWindowTheme(DarkMode_Explorer). Hand back the dialog brush so
			// any classic-painted edges blend in.
			break;
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		default:
			break;
		}
		::SetTextColor(dc, text());
		::SetBkColor(dc, bk);
		::SetBkMode(dc, OPAQUE);
		return br;
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

		bool is_bar = false;
		const char* text_a = menu_item_label(mis->itemData, &is_bar);
		int total_w = 0;
		int max_h = 0;
		if (text_a)
		{
			const char* tab = strchr(text_a, '\t');
			SIZE szL = { 0, 0 };
			SIZE szR = { 0, 0 };
			if (tab)
			{
				::GetTextExtentPoint32(hdc, text_a, static_cast<int>(tab - text_a), &szL);
				::GetTextExtentPoint32(hdc, tab + 1, static_cast<int>(strlen(tab + 1)), &szR);
				total_w = szL.cx + szR.cx + 32; // gap between label and shortcut
				max_h = szL.cy > szR.cy ? szL.cy : szR.cy;
			}
			else
			{
				::GetTextExtentPoint32(hdc, text_a, static_cast<int>(strlen(text_a)), &szL);
				total_w = szL.cx;
				max_h = szL.cy;
			}
		}

		// Popup items reserve a left gutter (~24 px) so checked/unchecked
		// items don't shift horizontally — that gutter is filled by the
		// checkmark glyph in on_draw_menu_item when ODS_CHECKED is set.
		// Menu-bar items (top-level File/View/...) never have checkmarks
		// and don't need the extra padding; without this exclusion they
		// pick up the same +16+24 gap that's invisible in light mode but
		// shows up clearly under the dark owner-draw painter.
		if (is_bar)
		{
			// Match the per-item gap Windows adds in light (system-measured)
			// mode. Owner-drawn items don't get that gap for free; without
			// the +12 here, "File Conversion View Launch" runs together with
			// no breathing room. 12 = 6px each side, matches light visually.
			mis->itemWidth = total_w + 12;
			mis->itemHeight = max_h + 4;
		}
		else
		{
			const int gutter = 24;
			mis->itemWidth = total_w + 16 + gutter;
			mis->itemHeight = max_h + 6;
		}

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

		bool is_bar = false;
		const char* text_a = menu_item_label(dis->itemData, &is_bar);

		// The popup gutter holds the check/bullet glyph; menu-bar items have
		// no check and use a flush-left text rect instead.
		const int gutter = 24;
		const bool checked = !is_bar && (dis->itemState & ODS_CHECKED) != 0;
		// Radio vs check: ODS_CHECKED is set for both MF_CHECKED and
		// MF_RADIOCHECK items. To distinguish, query MIIM_FTYPE on the parent
		// HMENU (dis->hwndItem is the HMENU for ODT_MENU). MFT_RADIOCHECK in
		// the type flags → draw bullet; otherwise draw checkmark.
		bool is_radio = false;
		if (checked && dis->hwndItem)
		{
			MENUITEMINFO mii = {};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_FTYPE;
			if (::GetMenuItemInfo(reinterpret_cast<HMENU>(dis->hwndItem),
				dis->itemID, FALSE, &mii))
			{
				is_radio = (mii.fType & MFT_RADIOCHECK) != 0;
			}
		}
		if (checked)
		{
			// Try the OS theme engine first — gives pixel-accurate Win11
			// check/bullet glyphs. Fall back to GDI primitives if the theme
			// handle or DrawThemeBackground fails (older Windows / safe mode).
			int cx_mid = r.left + gutter / 2;
			int cy_mid = (r.top + r.bottom) / 2;
			HTHEME hth = ::OpenThemeData(NULL, L"DarkMode_Menu::Menu");
			if (!hth)
				hth = ::OpenThemeData(NULL, L"Menu");

			bool drawn = false;
			if (hth)
			{
				int part = MENU_POPUPCHECK;
				int state;
				if (is_radio)
					state = disabled ? MC_BULLETDISABLED : MC_BULLETNORMAL;
				else
					state = disabled ? MC_CHECKMARKDISABLED : MC_CHECKMARKNORMAL;

				SIZE sz = { 16, 16 };
				::GetThemePartSize(hth, hdc, part, state, NULL, TS_DRAW, &sz);
				if (sz.cx <= 0 || sz.cy <= 0)
				{
					sz.cx = 16;
					sz.cy = 16;
				}
				RECT cr;
				cr.left   = cx_mid - sz.cx / 2;
				cr.top    = cy_mid - sz.cy / 2;
				cr.right  = cr.left + sz.cx;
				cr.bottom = cr.top  + sz.cy;
				if (::DrawThemeBackground(hth, hdc, part, state, &cr, NULL) == S_OK)
					drawn = true;
				::CloseThemeData(hth);
			}

			if (!drawn)
			{
				COLORREF mark = disabled ? text_dim() : text();
				if (is_radio)
				{
					HBRUSH br = ::CreateSolidBrush(mark);
					HGDIOBJ old_br = ::SelectObject(hdc, br);
					HPEN pen = ::CreatePen(PS_SOLID, 1, mark);
					HGDIOBJ old_pen = ::SelectObject(hdc, pen);
					::Ellipse(hdc, cx_mid - 3, cy_mid - 3, cx_mid + 4, cy_mid + 4);
					::SelectObject(hdc, old_pen);
					::SelectObject(hdc, old_br);
					::DeleteObject(pen);
					::DeleteObject(br);
				}
				else
				{
					// Win11-ish thin tick: 3-segment polyline through the gutter
					// midpoint. Stroke color follows text color so disabled items dim.
					HPEN pen = ::CreatePen(PS_SOLID, 2, mark);
					HGDIOBJ old_pen = ::SelectObject(hdc, pen);
					POINT pts[3] = {
						{ cx_mid - 5, cy_mid + 0 },
						{ cx_mid - 1, cy_mid + 4 },
						{ cx_mid + 6, cy_mid - 4 },
					};
					::Polyline(hdc, pts, 3);
					::SelectObject(hdc, old_pen);
					::DeleteObject(pen);
				}
			}
		}

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
			if (is_bar)
			{
				// Symmetric inset matching the +12 added in measure so the
				// hover/select fill has a few px of breathing room around the
				// label and items don't touch their neighbors visually.
				tr.left += 6;
				tr.right -= 6;
			}
			else
			{
				tr.left += gutter + 8;
				tr.right -= 8;
			}
			const char* tab = strchr(text_a, '\t');
			if (tab && !is_bar)
			{
				::DrawTextA(hdc, text_a, static_cast<int>(tab - text_a), &tr,
					DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_HIDEPREFIX);
				::DrawTextA(hdc, tab + 1, -1, &tr,
					DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_HIDEPREFIX);
			}
			else
			{
				::DrawTextA(hdc, text_a, -1, &tr,
					DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_HIDEPREFIX);
			}

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

	// ---------- UAH menu-bar subclass ----------
	//
	// Notepad++ pattern: Windows draws the popup menus natively (we already
	// call SetPreferredAppMode + FlushMenuThemes via theme::apply_app_mode);
	// the menu-BAR strip is custom-drawn via undocumented UAH messages.
	//
	// The subclass consumes only WM_UAHDRAWMENU / WM_UAHDRAWMENUITEM /
	// WM_NCDESTROY plus a theme-cache refresh on WM_THEMECHANGED. Every
	// other message falls through to DefSubclassProc, which lets MFC's
	// CFrameWnd::WindowProc see WM_COMMAND, WM_NOTIFY, WM_DRAWITEM,
	// WM_MEASUREITEM, WM_CTLCOLOR*, WM_TIMER, WM_HSCROLL, etc. as before —
	// the player band (CXCCFileView), splitter, status bar, accelerators
	// all continue to work unchanged.

	namespace
	{
		struct UAHMenuThemeCache
		{
			HTHEME h_theme = NULL;
			HTHEME ensure(HWND h)
			{
				if (!h_theme)
					h_theme = ::OpenThemeData(h, VSCLASS_MENU);
				return h_theme;
			}
			void close()
			{
				if (h_theme)
				{
					::CloseThemeData(h_theme);
					h_theme = NULL;
				}
			}
		};

		constexpr UINT_PTR k_uah_subclass_id = 0x58444D55u; // 'UMDX'

		void draw_uah_menu_bar(HWND h, HDC hdc)
		{
			// Fill the bar rect with the menu background brush. The DC
			// passed in WM_UAHDRAWMENU is already clipped to the bar.
			MENUBARINFO mbi = {};
			mbi.cbSize = sizeof(mbi);
			if (!::GetMenuBarInfo(h, OBJID_MENU, 0, &mbi))
				return;
			RECT win;
			::GetWindowRect(h, &win);
			RECT bar = mbi.rcBar;
			::OffsetRect(&bar, -win.left, -win.top);
			bar.top -= 1; // include the hairline below the bar
			::FillRect(hdc, &bar, menu_bg_brush());
		}

		void draw_uah_menu_item(UAHDRAWMENUITEM& udmi, HTHEME h_theme)
		{
			// Resolve label width from MIIM_STRING. Windows' UAH path passes
			// the bar HMENU + iPosition; itemID in dis is not populated for
			// bar items.
			wchar_t buf[256] = {};
			MENUITEMINFOW mii = {};
			mii.cbSize = sizeof(mii);
			mii.fMask = MIIM_STRING;
			mii.dwTypeData = buf;
			mii.cch = (sizeof(buf) / sizeof(buf[0])) - 1;
			::GetMenuItemInfoW(udmi.um.hmenu, static_cast<UINT>(udmi.umi.iPosition), TRUE, &mii);

			DWORD dt_flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
			if ((udmi.dis.itemState & ODS_NOACCEL) != 0)
				dt_flags |= DT_HIDEPREFIX;

			int text_state;
			int bg_state;
			if ((udmi.dis.itemState & ODS_SELECTED) != 0)
			{
				text_state = MBI_PUSHED;
				bg_state   = MBI_PUSHED;
			}
			else if ((udmi.dis.itemState & ODS_HOTLIGHT) != 0)
			{
				text_state = ((udmi.dis.itemState & ODS_INACTIVE) != 0) ? MBI_DISABLEDHOT : MBI_HOT;
				bg_state   = MBI_HOT;
			}
			else if ((udmi.dis.itemState & (ODS_GRAYED | ODS_DISABLED | ODS_INACTIVE)) != 0)
			{
				text_state = MBI_DISABLED;
				bg_state   = MBI_DISABLED;
			}
			else
			{
				text_state = MBI_NORMAL;
				bg_state   = MBI_NORMAL;
			}

			// Background: fill with theme brush regardless of state — we
			// don't trust Windows' MENU_BARITEM background in dark mode.
			HBRUSH bg_brush;
			switch (bg_state)
			{
				case MBI_HOT:
				case MBI_DISABLEDHOT:
					bg_brush = menu_hot_brush();
					break;
				default:
					bg_brush = menu_bg_brush();
					break;
			}
			::FillRect(udmi.um.hdc, &udmi.dis.rcItem, bg_brush);

			DTTOPTS dtt = {};
			dtt.dwSize = sizeof(dtt);
			dtt.dwFlags = DTT_TEXTCOLOR;
			switch (text_state)
			{
				case MBI_DISABLED:
				case MBI_DISABLEDHOT:
				case MBI_DISABLEDPUSHED:
					dtt.crText = text_dim();
					break;
				default:
					dtt.crText = text();
					break;
			}
			if (h_theme)
			{
				::DrawThemeTextEx(h_theme, udmi.um.hdc, MENU_BARITEM, text_state,
					buf, static_cast<int>(mii.cch), dt_flags, &udmi.dis.rcItem, &dtt);
			}
			else
			{
				// Fallback if theme handle failed: GDI draw.
				::SetBkMode(udmi.um.hdc, TRANSPARENT);
				::SetTextColor(udmi.um.hdc, dtt.crText);
				::DrawTextW(udmi.um.hdc, buf, static_cast<int>(mii.cch),
					&udmi.dis.rcItem, dt_flags);
			}
		}

		void draw_uah_nc_bottom_line(HWND h)
		{
			// Windows paints a 1-px white line below the menu bar in the
			// non-client area; Notepad++ overpaints it with the dark brush.
			MENUBARINFO mbi = {};
			mbi.cbSize = sizeof(mbi);
			if (!::GetMenuBarInfo(h, OBJID_MENU, 0, &mbi))
				return;
			RECT cli;
			::GetClientRect(h, &cli);
			::MapWindowPoints(h, NULL, reinterpret_cast<POINT*>(&cli), 2);
			RECT win;
			::GetWindowRect(h, &win);
			::OffsetRect(&cli, -win.left, -win.top);
			RECT line = cli;
			line.bottom = line.top;
			line.top--;
			HDC hdc = ::GetWindowDC(h);
			if (!hdc) return;
			::FillRect(hdc, &line, menu_bg_brush());
			::ReleaseDC(h, hdc);
		}

		LRESULT CALLBACK uah_menu_subclass(
			HWND h, UINT msg, WPARAM wp, LPARAM lp,
			UINT_PTR id_subclass, DWORD_PTR ref_data)
		{
			UAHMenuThemeCache* cache = reinterpret_cast<UAHMenuThemeCache*>(ref_data);

			// In light mode (or while theme is unresolved) we still need to
			// service WM_NCDESTROY for cleanup, but we want every other
			// message to fall straight through. Don't even try to paint.
			if (msg != WM_NCDESTROY && !is_dark())
				return ::DefSubclassProc(h, msg, wp, lp);

			switch (msg)
			{
				case WM_NCDESTROY:
				{
					::RemoveWindowSubclass(h, uah_menu_subclass, id_subclass);
					delete cache;
					break; // fall through to DefSubclassProc
				}
				case WM_THEMECHANGED:
				case WM_DPICHANGED:
				{
					if (cache) cache->close();
					break; // fall through so MFC sees it too
				}
				case WM_UAHDRAWMENU:
				{
					UAHMENU* p = reinterpret_cast<UAHMENU*>(lp);
					if (p) draw_uah_menu_bar(h, p->hdc);
					return 0;
				}
				case WM_UAHDRAWMENUITEM:
				{
					UAHDRAWMENUITEM* p = reinterpret_cast<UAHDRAWMENUITEM*>(lp);
					if (p)
					{
						HTHEME ht = cache ? cache->ensure(h) : NULL;
						draw_uah_menu_item(*p, ht);
					}
					return 0;
				}
				case WM_NCACTIVATE:
				case WM_NCPAINT:
				{
					LRESULT lr = ::DefSubclassProc(h, msg, wp, lp);
					draw_uah_nc_bottom_line(h);
					return lr;
				}
				default:
					break;
			}
			return ::DefSubclassProc(h, msg, wp, lp);
		}

		// ---------- Combobox subclass (Notepad++ pattern) ----------
		//
		// Ports NppDarkMode::ComboBoxSubclass + paintCombobox. The subclass
		// owns WM_PAINT in dark mode and draws:
		//   * closed-state field with theme background
		//   * dropdown arrow via DrawThemeBackground(VSCLASS_COMBOBOX,
		//     CP_DROPDOWNBUTTONRIGHT) — falls back to a Unicode chevron if
		//     theme open fails
		//   * 1px frame around the whole combobox using edge color (hot edge
		//     when hovered/focused, disabled edge when grayed)
		//   * selected item text via DrawThemeTextEx(CP_DROPDOWNITEM)
		// Light mode: every message except WM_NCDESTROY falls through to
		// DefSubclassProc — the system painter draws the combobox as normal.
		//
		// MFC safety: WM_COMMAND (CBN_SELCHANGE/CBN_DROPDOWN/CBN_CLOSEUP),
		// WM_NOTIFY, WM_CTLCOLOR*, WM_KEYDOWN, WM_MOUSE* all fall through to
		// DefSubclassProc → MFC's CDialog/CFormView reflection. We only
		// touch WM_PAINT/WM_ERASEBKGND/WM_THEMECHANGED/WM_NCDESTROY/WM_ENABLE.

		constexpr UINT_PTR k_combo_subclass_id = 0x58434243u; // 'CBCX'
		constexpr int k_combo_win11_corner = 4;

		// Combobox edge / fill colors. Hot edge tracks accent; normal edge is
		// the same border color list-views use for their grid. Control bg is
		// our existing bg_alt() (slight lift over the dialog bg).
		inline COLORREF combo_edge_color()          { return border(); }
		inline COLORREF combo_hot_edge_color()      { return accent(); }
		inline COLORREF combo_disabled_edge_color() { return RGB(0x55, 0x55, 0x55); }
		inline COLORREF combo_disabled_text_color() { return text_dim(); }

		struct ComboboxThemeCache
		{
			HTHEME h_theme = NULL;
			LONG_PTR cb_style = CBS_SIMPLE; // CBS_DROPDOWN or CBS_DROPDOWNLIST
			HDC mem_dc = NULL;
			HBITMAP mem_bmp = NULL;
			int mem_w = 0;
			int mem_h = 0;

			HTHEME ensure(HWND h)
			{
				if (!h_theme)
					h_theme = ::OpenThemeData(h, VSCLASS_COMBOBOX);
				return h_theme;
			}
			void close()
			{
				if (h_theme)
				{
					::CloseThemeData(h_theme);
					h_theme = NULL;
				}
			}
			bool ensure_buffer(HDC ref, const RECT& rc)
			{
				int w = rc.right - rc.left;
				int h = rc.bottom - rc.top;
				if (w <= 0 || h <= 0) return false;
				if (mem_dc && (w != mem_w || h != mem_h))
				{
					::DeleteObject(mem_bmp);
					::DeleteDC(mem_dc);
					mem_dc = NULL;
					mem_bmp = NULL;
				}
				if (!mem_dc)
				{
					mem_dc = ::CreateCompatibleDC(ref);
					if (!mem_dc) return false;
					mem_bmp = ::CreateCompatibleBitmap(ref, w, h);
					if (!mem_bmp)
					{
						::DeleteDC(mem_dc);
						mem_dc = NULL;
						return false;
					}
					::SelectObject(mem_dc, mem_bmp);
					mem_w = w;
					mem_h = h;
				}
				return true;
			}
			~ComboboxThemeCache()
			{
				close();
				if (mem_bmp) ::DeleteObject(mem_bmp);
				if (mem_dc)  ::DeleteDC(mem_dc);
			}
		};

		void paint_round_frame_rect(HDC hdc, const RECT& rc, HPEN pen, int rx = 0, int ry = 0)
		{
			HGDIOBJ old_pen = ::SelectObject(hdc, pen);
			HGDIOBJ old_br  = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
			::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, rx, ry);
			::SelectObject(hdc, old_br);
			::SelectObject(hdc, old_pen);
		}

		void paint_combobox(HWND h, HDC hdc, ComboboxThemeCache& cache)
		{
			HTHEME h_theme = cache.ensure(h);

			COMBOBOXINFO cbi = {};
			cbi.cbSize = sizeof(cbi);
			::GetComboBoxInfo(h, &cbi);

			RECT rc;
			::GetClientRect(h, &rc);

			POINT cursor;
			::GetCursorPos(&cursor);
			::ScreenToClient(h, &cursor);

			const bool is_disabled = ::IsWindowEnabled(h) == FALSE;
			const bool is_hot      = !is_disabled && ::PtInRect(&rc, cursor) == TRUE;
			bool has_focus = false;

			::SelectObject(hdc, reinterpret_cast<HFONT>(::SendMessage(h, WM_GETFONT, 0, 0)));
			::SetBkMode(hdc, TRANSPARENT);

			RECT rc_arrow = cbi.rcButton;
			rc_arrow.left -= 1;

			HBRUSH bg_brush_sel = is_disabled ? bg_brush()
				: (is_hot ? menu_hot_brush() : bg_alt_brush());

			// CBS_DROPDOWNLIST: we draw the selected text. CBS_DROPDOWN's edit
			// child paints itself (WM_CTLCOLOREDIT in the parent handles colors).
			if (cache.cb_style == CBS_DROPDOWNLIST)
			{
				::FillRect(hdc, &rc, bg_brush_sel);

				int idx = static_cast<int>(::SendMessage(h, CB_GETCURSEL, 0, 0));
				if (idx != CB_ERR)
				{
					// Use ANSI vs Unicode message variant based on the actual
					// window type. Mixer creates combos via MFC's ANSI build
					// (CComboBox::Create + AddString narrow overload), so the
					// internal item storage is ANSI — sending CB_GETLBTEXT
					// (which routes via IsWindowUnicode here) returns wide
					// chars only on Unicode combos. Reading ANSI bytes as
					// wchar_t* renders as random CJK glyphs.
					const bool is_unicode = ::IsWindowUnicode(h) != FALSE;
					RECT rc_text = cbi.rcItem;
					::InflateRect(&rc_text, -2, 0);
					const DWORD dt_flags = DT_NOPREFIX | DT_LEFT | DT_VCENTER | DT_SINGLELINE;
					const COLORREF text_color = is_disabled ? combo_disabled_text_color() : text();

					if (is_unicode)
					{
						int len = static_cast<int>(::SendMessageW(h, CB_GETLBTEXTLEN, idx, 0));
						if (len > 0 && len < 1024)
						{
							std::vector<wchar_t> buf(static_cast<size_t>(len) + 1, L'\0');
							::SendMessageW(h, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buf.data()));
							if (h_theme)
							{
								DTTOPTS dtt = {};
								dtt.dwSize = sizeof(dtt);
								dtt.dwFlags = DTT_TEXTCOLOR;
								dtt.crText = text_color;
								const int CP_DROPDOWNITEM = 9;
								::DrawThemeTextEx(h_theme, hdc, CP_DROPDOWNITEM,
									is_disabled ? CBXSR_DISABLED : CBXSR_NORMAL,
									buf.data(), -1, dt_flags, &rc_text, &dtt);
							}
							else
							{
								::SetTextColor(hdc, text_color);
								::DrawTextW(hdc, buf.data(), -1, &rc_text, dt_flags);
							}
						}
					}
					else
					{
						int len = static_cast<int>(::SendMessageA(h, CB_GETLBTEXTLEN, idx, 0));
						if (len > 0 && len < 1024)
						{
							std::vector<char> buf(static_cast<size_t>(len) + 1, '\0');
							::SendMessageA(h, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buf.data()));
							// DrawThemeTextEx is wide-only — promote ANSI to
							// UTF-16 via the active code page so theme path
							// still works on ANSI combos.
							if (h_theme)
							{
								int wlen = ::MultiByteToWideChar(CP_ACP, 0, buf.data(), len, NULL, 0);
								if (wlen > 0)
								{
									std::vector<wchar_t> wbuf(static_cast<size_t>(wlen) + 1, L'\0');
									::MultiByteToWideChar(CP_ACP, 0, buf.data(), len, wbuf.data(), wlen);
									DTTOPTS dtt = {};
									dtt.dwSize = sizeof(dtt);
									dtt.dwFlags = DTT_TEXTCOLOR;
									dtt.crText = text_color;
									const int CP_DROPDOWNITEM = 9;
									::DrawThemeTextEx(h_theme, hdc, CP_DROPDOWNITEM,
										is_disabled ? CBXSR_DISABLED : CBXSR_NORMAL,
										wbuf.data(), wlen, dt_flags, &rc_text, &dtt);
								}
							}
							else
							{
								::SetTextColor(hdc, text_color);
								::DrawTextA(hdc, buf.data(), -1, &rc_text, dt_flags);
							}
						}
					}
				}

				has_focus = ::GetFocus() == h;
				// Skip DrawFocusRect: it's XOR-based and lands as a solid
				// white block over our dark fill instead of the dotted gray
				// it produces on light backgrounds. The hot-edge frame color
				// already signals focus visually.
			}
			else if (cache.cb_style == CBS_DROPDOWN && cbi.hwndItem)
			{
				has_focus = ::GetFocus() == cbi.hwndItem;
				::FillRect(hdc, &rc_arrow, bg_brush_sel);
			}

			COLORREF edge_color = is_disabled ? combo_disabled_edge_color()
				: ((is_hot || has_focus) ? combo_hot_edge_color() : combo_edge_color());
			HPEN frame_pen = ::CreatePen(PS_SOLID, 1, edge_color);
			HGDIOBJ old_pen = ::SelectObject(hdc, frame_pen);

			// Dropdown arrow. Theme path is preferred; falls back to a small
			// Unicode chevron if uxtheme can't open VSCLASS_COMBOBOX.
			if (cache.cb_style != CBS_SIMPLE)
			{
				if (h_theme)
				{
					RECT rc_themed_arrow = { rc_arrow.left, rc_arrow.top - 1, rc_arrow.right, rc_arrow.bottom - 1 };
					::DrawThemeBackground(h_theme, hdc, CP_DROPDOWNBUTTONRIGHT,
						is_disabled ? CBXSR_DISABLED : CBXSR_NORMAL, &rc_themed_arrow, NULL);
				}
				else
				{
					COLORREF arrow_color = is_disabled ? combo_disabled_text_color()
						: (is_hot ? text() : text_dim());
					::SetTextColor(hdc, arrow_color);
					wchar_t arrow[] = L"˅"; // small down chevron
					::DrawTextW(hdc, arrow, -1, &rc_arrow,
						DT_NOPREFIX | DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
				}
			}

			// Exclude the inner area we've already drawn so the round-rect
			// frame doesn't overwrite our text/arrow.
			if (cache.cb_style == CBS_DROPDOWNLIST)
			{
				RECT rc_inner = rc;
				::InflateRect(&rc_inner, -1, -1);
				::ExcludeClipRect(hdc, rc_inner.left, rc_inner.top, rc_inner.right, rc_inner.bottom);
			}
			else if (cache.cb_style == CBS_DROPDOWN)
			{
				POINT edge[] = {
					{ rc_arrow.left - 1, rc_arrow.top    },
					{ rc_arrow.left - 1, rc_arrow.bottom }
				};
				::Polyline(hdc, edge, 2);
				::ExcludeClipRect(hdc, cbi.rcItem.left, cbi.rcItem.top, cbi.rcItem.right, cbi.rcItem.bottom);
				::ExcludeClipRect(hdc, rc_arrow.left - 1, rc_arrow.top, rc_arrow.right, rc_arrow.bottom);

				HPEN inner_pen = ::CreatePen(PS_SOLID, 1, is_disabled ? bg() : bg_alt());
				RECT rc_inner = rc;
				::InflateRect(&rc_inner, -1, -1);
				rc_inner.right = rc_arrow.left - 1;
				paint_round_frame_rect(hdc, rc_inner, inner_pen);
				::DeleteObject(inner_pen);
				::InflateRect(&rc_inner, -1, -1);
				::FillRect(hdc, &rc_inner, is_disabled ? bg_brush() : bg_alt_brush());
			}

			// Detect Win11 via build number for slight corner rounding.
			OSVERSIONINFOEXW os = {};
			os.dwOSVersionInfoSize = sizeof(os);
			::GetVersionExW(reinterpret_cast<OSVERSIONINFOW*>(&os));
			int round_r = (os.dwBuildNumber >= 22000) ? k_combo_win11_corner : 0;

			paint_round_frame_rect(hdc, rc, frame_pen, round_r, round_r);

			::SelectObject(hdc, old_pen);
			::DeleteObject(frame_pen);
		}

		LRESULT CALLBACK combobox_subclass(
			HWND h, UINT msg, WPARAM wp, LPARAM lp,
			UINT_PTR id_subclass, DWORD_PTR ref_data)
		{
			ComboboxThemeCache* cache = reinterpret_cast<ComboboxThemeCache*>(ref_data);

			switch (msg)
			{
				case WM_NCDESTROY:
				{
					::RemoveWindowSubclass(h, combobox_subclass, id_subclass);
					delete cache;
					break;
				}
				case WM_THEMECHANGED:
				case WM_DPICHANGED:
				{
					if (cache) cache->close();
					break;
				}
				case WM_ERASEBKGND:
				{
					if (is_dark() && cache && cache->ensure(h))
						return TRUE;
					break;
				}
				case WM_PAINT:
				{
					if (!is_dark() || !cache)
						break;
					PAINTSTRUCT ps = {};
					HDC hdc = ::BeginPaint(h, &ps);
					if (!hdc) return 0;

					if (cache->cb_style != CBS_DROPDOWN)
					{
						// Double-buffer to avoid flicker on dropdown-list combos.
						RECT rc;
						::GetClientRect(h, &rc);
						if (cache->ensure_buffer(hdc, rc))
						{
							int saved = ::SaveDC(cache->mem_dc);
							::IntersectClipRect(cache->mem_dc,
								ps.rcPaint.left, ps.rcPaint.top,
								ps.rcPaint.right, ps.rcPaint.bottom);
							paint_combobox(h, cache->mem_dc, *cache);
							::RestoreDC(cache->mem_dc, saved);
							::BitBlt(hdc,
								ps.rcPaint.left, ps.rcPaint.top,
								ps.rcPaint.right - ps.rcPaint.left,
								ps.rcPaint.bottom - ps.rcPaint.top,
								cache->mem_dc,
								ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
						}
					}
					else
					{
						// CBS_DROPDOWN has a live edit child; double-buffer
						// would flicker the caret.
						paint_combobox(h, hdc, *cache);
					}
					::EndPaint(h, &ps);
					return 0;
				}
				case WM_ENABLE:
				{
					if (!is_dark()) break;
					LRESULT lr = ::DefSubclassProc(h, msg, wp, lp);
					::RedrawWindow(h, NULL, NULL, RDW_INVALIDATE);
					return lr;
				}
			}
			return ::DefSubclassProc(h, msg, wp, lp);
		}
	}

	void install_uah_menu_subclass(HWND h_frame)
	{
		if (!h_frame)
			return;
		// Idempotent: GetWindowSubclass returns TRUE when already installed.
		if (::GetWindowSubclass(h_frame, uah_menu_subclass, k_uah_subclass_id, NULL))
			return;
		UAHMenuThemeCache* cache = new UAHMenuThemeCache();
		if (!::SetWindowSubclass(h_frame, uah_menu_subclass, k_uah_subclass_id,
			reinterpret_cast<DWORD_PTR>(cache)))
		{
			delete cache;
		}
	}

	// Subclass for a combobox's dropdown listbox ("ComboLBox"). In dark mode
	// uxtheme leaves the listbox selection bar painted with COLOR_HIGHLIGHT
	// (white in our process — confirmed handle 002D090A on the Game Grid
	// combo), making the dark-on-dark item text invisible behind it. We own
	// WM_PAINT in dark mode and paint each visible row ourselves; in light
	// mode we fall through to system painting.
	constexpr UINT_PTR k_combolbox_subclass_id = 0x584C4243u; // 'CBLX'

	struct ComboLBoxState
	{
		int last_sel = -2; // -2 = never sampled; -1 = no selection
	};

	LRESULT CALLBACK combolbox_subclass(
		HWND h, UINT msg, WPARAM wp, LPARAM lp,
		UINT_PTR id_subclass, DWORD_PTR ref_data)
	{
		ComboLBoxState* state = reinterpret_cast<ComboLBoxState*>(ref_data);

		switch (msg)
		{
		case WM_NCDESTROY:
			::RemoveWindowSubclass(h, combolbox_subclass, id_subclass);
			delete state;
			break;
		case WM_MOUSEMOVE:
			// Combobox dropdowns retarget LB_GETCURSEL to follow the mouse as
			// the user hovers; uxtheme then paints the moved selection bar in
			// COLOR_HIGHLIGHT (white) on top of our WM_PAINT. To overwrite
			// it, we invalidate — but only on actual selection-row change.
			// Mouse hardware delivers WM_MOUSEMOVE at ~1000 Hz; without this
			// gate the listbox burns a full CPU core during any hover and
			// floods the message loop with paints, which is what caused the
			// "100% CPU while SHP playing" regression.
			if (is_dark() && state)
			{
				LRESULT lr = ::DefSubclassProc(h, msg, wp, lp);
				const int sel = static_cast<int>(::SendMessageW(h, LB_GETCURSEL, 0, 0));
				if (sel != state->last_sel)
				{
					state->last_sel = sel;
					::InvalidateRect(h, NULL, FALSE);
				}
				return lr;
			}
			break;
		case WM_ERASEBKGND:
			if (is_dark())
			{
				HDC hdc = reinterpret_cast<HDC>(wp);
				RECT rc;
				::GetClientRect(h, &rc);
				::FillRect(hdc, &rc, bg_brush());
				return TRUE;
			}
			break;
		case WM_PAINT:
		{
			if (!is_dark())
				break;
			PAINTSTRUCT ps = {};
			HDC hdc = ::BeginPaint(h, &ps);
			if (!hdc)
				return 0;
			RECT rc;
			::GetClientRect(h, &rc);
			::FillRect(hdc, &rc, bg_brush());

			HFONT hf = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
			HGDIOBJ old_font = hf ? ::SelectObject(hdc, hf) : NULL;
			::SetBkMode(hdc, TRANSPARENT);

			const int count = static_cast<int>(::SendMessageW(h, LB_GETCOUNT, 0, 0));
			const int cur_sel = static_cast<int>(::SendMessageW(h, LB_GETCURSEL, 0, 0));
			const int top_idx = static_cast<int>(::SendMessageW(h, LB_GETTOPINDEX, 0, 0));

			for (int i = (top_idx > 0 ? top_idx : 0); i < count; i++)
			{
				RECT ir = {};
				if (::SendMessageW(h, LB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&ir)) == LB_ERR)
					continue;
				if (ir.top >= ps.rcPaint.bottom)
					break;
				if (ir.bottom <= ps.rcPaint.top)
					continue;

				const bool sel = (i == cur_sel);
				::FillRect(hdc, &ir, sel ? menu_hot_brush() : bg_brush());
				::SetTextColor(hdc, sel ? accent_text() : text());

				wchar_t buf[256] = {};
				const int len = static_cast<int>(::SendMessageW(h, LB_GETTEXTLEN, i, 0));
				if (len > 0 && len < static_cast<int>(_countof(buf)))
				{
					::SendMessageW(h, LB_GETTEXT, i, reinterpret_cast<LPARAM>(buf));
					RECT tr = ir;
					tr.left += 4;
					::DrawTextW(hdc, buf, -1, &tr,
						DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
				}
			}

			if (old_font) ::SelectObject(hdc, old_font);
			::EndPaint(h, &ps);
			return 0;
		}
		}
		return ::DefSubclassProc(h, msg, wp, lp);
	}

	void subclass_combolbox(HWND h_list)
	{
		if (!h_list)
			return;
		if (::GetWindowSubclass(h_list, combolbox_subclass, k_combolbox_subclass_id, NULL))
			return;
		ComboLBoxState* state = new ComboLBoxState();
		if (::SetWindowSubclass(h_list, combolbox_subclass, k_combolbox_subclass_id,
			reinterpret_cast<DWORD_PTR>(state)))
		{
			::InvalidateRect(h_list, NULL, TRUE);
		}
		else
		{
			delete state;
		}
	}

	void subclass_combobox(HWND h_combo)
	{
		if (!h_combo)
			return;
		// Refuse owner-draw combos — the subclass owns WM_PAINT and would
		// fight the parent's WM_DRAWITEM handler. Callers must drop
		// CBS_OWNERDRAWFIXED/VARIABLE before reaching here.
		LONG_PTR style = ::GetWindowLongPtrW(h_combo, GWL_STYLE);
		if ((style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) != 0)
			return;
		if (::GetWindowSubclass(h_combo, combobox_subclass, k_combo_subclass_id, NULL))
			return;
		ComboboxThemeCache* cache = new ComboboxThemeCache();
		cache->cb_style = (style & CBS_DROPDOWNLIST) ? CBS_DROPDOWNLIST
			: ((style & CBS_DROPDOWN) ? CBS_DROPDOWN : CBS_SIMPLE);
		if (!::SetWindowSubclass(h_combo, combobox_subclass, k_combo_subclass_id,
			reinterpret_cast<DWORD_PTR>(cache)))
		{
			delete cache;
		}
		else
		{
			::InvalidateRect(h_combo, NULL, TRUE);
		}
		// Dark-paint the dropdown listbox too. The combobox subclass above
		// only owns the closed-state field + arrow; the popup list is a
		// separate HWND (hwndList in COMBOBOXINFO).
		COMBOBOXINFO cbi = {};
		cbi.cbSize = sizeof(cbi);
		if (::GetComboBoxInfo(h_combo, &cbi) && cbi.hwndList)
			subclass_combolbox(cbi.hwndList);
	}
}

// ---------- CThemedStatusBar ----------

BEGIN_MESSAGE_MAP(CThemedStatusBar, CStatusBar)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CThemedStatusBar::OnEraseBkgnd(CDC* pDC)
{
	if (theme::is_dark())
	{
		CRect r;
		GetClientRect(&r);
		pDC->FillSolidRect(&r, theme::bg());
		return TRUE;
	}
	return CStatusBar::OnEraseBkgnd(pDC);
}

void CThemedStatusBar::OnPaint()
{
	if (!theme::is_dark())
	{
		Default();
		return;
	}
	CPaintDC dc(this);
	CRect cr;
	GetClientRect(&cr);
	dc.FillSolidRect(&cr, theme::bg());
	dc.SetTextColor(theme::text());
	dc.SetBkMode(TRANSPARENT);

	HFONT hf = reinterpret_cast<HFONT>(::SendMessage(GetSafeHwnd(), WM_GETFONT, 0, 0));
	HGDIOBJ old_font = NULL;
	if (hf)
		old_font = dc.SelectObject(hf);

	int n = static_cast<int>(GetCount());
	for (int i = 0; i < n; i++)
	{
		CRect r;
		GetItemRect(i, &r);
		CString s = GetPaneText(i);
		r.left += 4;
		dc.DrawText(s, &r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
	}

	if (old_font)
		dc.SelectObject(old_font);
}

BOOL CThemedStatusBar::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// lParam != 0 means a control notification (a child sent it), not a menu
	// command. Forward those to the frame so its command handlers (e.g. the
	// Load PAL button's ON_BN_CLICKED) get a chance to run.
	if (lParam)
	{
		CWnd* p = GetParent();
		if (p && p->SendMessage(WM_COMMAND, wParam, lParam))
			return TRUE;
	}
	return CStatusBar::OnCommand(wParam, lParam);
}

// ---------- CThemedSplitterWnd ----------

BEGIN_MESSAGE_MAP(CThemedSplitterWnd, CSplitterWnd)
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

int CThemedSplitterWnd::HitTest(CPoint pt) const
{
	int ht = CSplitterWnd::HitTest(pt);
	// One Pane mode: middle column (col 1) is collapsed to 0px, so bars 201
	// (left|middle) and 202 (middle|fileinfo) sit at the same X. Column
	// bars are 201..215 (hSplitterBar1..15) per MFC's winsplit.cpp.
	//
	// MFC's TrackColumnSize(x, col) sets m_pColInfo[col].nIdealSize = x:
	//   - Bar 201 tracks col 0 → drag grows col 0 (left listview); col 1
	//     stays at min=0 since it can't shrink, col 2 absorbs the loss.
	//     This is the gesture the user actually wants ("expand left pane").
	//   - Bar 202 tracks col 1 → drag grows col 1 (the hidden middle pane),
	//     re-expanding it into view. This is the bug.
	// So in one-pane mode swallow 202, keep 201 active.
	if (m_columns_locked && ht == 202)
		return 0;	// noHit
	return ht;
}

void CThemedSplitterWnd::OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rect)
{
	if (!theme::is_dark() || pDC == NULL)
	{
		CSplitterWnd::OnDrawSplitter(pDC, nType, rect);
		return;
	}
	switch (nType)
	{
	case splitBox:
	case splitBar:
	case splitIntersection:
		pDC->FillSolidRect(rect, theme::bg());
		break;
	case splitBorder:
		pDC->FillSolidRect(rect, theme::border());
		break;
	default:
		CSplitterWnd::OnDrawSplitter(pDC, nType, rect);
		break;
	}
}

void CThemedSplitterWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	CSplitterWnd::OnLButtonUp(nFlags, point);
	// After a drag completes, the panes are repositioned but their interiors
	// often have stale paint (worse in dark mode because the inverted drag
	// rectangle leaves artifacts). Force everything under the splitter to
	// repaint cleanly.
	::RedrawWindow(GetSafeHwnd(), NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// ---------- CThemedHeaderCtrl ----------

BEGIN_MESSAGE_MAP(CThemedHeaderCtrl, CHeaderCtrl)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
END_MESSAGE_MAP()

void CThemedHeaderCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMCUSTOMDRAW* cd = reinterpret_cast<NMCUSTOMDRAW*>(pNMHDR);
	if (!theme::is_dark())
	{
		*pResult = CDRF_DODEFAULT;
		return;
	}

	switch (cd->dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	case CDDS_ITEMPREPAINT:
	{
		HDC hdc = cd->hdc;
		RECT r = cd->rc;
		::FillRect(hdc, &r, theme::bg_alt_brush());

		HPEN pen = ::CreatePen(PS_SOLID, 1, theme::border());
		HGDIOBJ old_pen = ::SelectObject(hdc, pen);
		::MoveToEx(hdc, r.right - 1, r.top, NULL);
		::LineTo(hdc, r.right - 1, r.bottom);
		::SelectObject(hdc, old_pen);
		::DeleteObject(pen);

		::SetTextColor(hdc, theme::text());
		::SetBkMode(hdc, TRANSPARENT);
		*pResult = CDRF_NEWFONT;
		return;
	}
	default:
		*pResult = CDRF_DODEFAULT;
		return;
	}
}
