#include "stdafx.h"
#include "theme.h"

#include <cmath>
#include <vector>

#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>

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
		size_format g_size_fmt = size_auto;
		vxl_ss g_vxl_ss = vxl_ss_4;
		bool g_vxl_shading = false;
		vxl_normal_source g_vxl_normal_src = vxl_normals_computed;
		vxl_normal_method g_vxl_normal_method = vxl_method_basic;
		vxl_normal_kernel g_vxl_normal_kernel = vxl_kernel_3;
		// Defaults below match the original hand-tuned constants in
		// CXCCFileView's splat path: light_x=-0.40825, light_y=-0.40825,
		// light_z=+0.81650 corresponds to az=225°, el≈54.7356° (atan2 of
		// 0.81650 vs sqrt(0.40825^2*2)). Ambient/diffuse are unchanged.
		const float k_default_az = 225.0f;
		const float k_default_el = 54.7356f;
		const float k_default_ambient = 0.55f;
		const float k_default_diffuse = 0.85f;
		float g_vxl_light_az = k_default_az;
		float g_vxl_light_el = k_default_el;
		float g_vxl_light_ambient = k_default_ambient;
		float g_vxl_light_diffuse = k_default_diffuse;
		int g_vxl_lighting_version = 0;
		// Set to true by any of the four lighting slider setters whenever they
		// would otherwise have called save(). flush_lighting_save() consumes
		// it. Decouples slider-drag tick rate from registry write rate.
		bool g_vxl_lighting_save_pending = false;
		bool g_limit_vxl_cpu = false;
		bool g_vxl_full_hierarchy = true;
		bool g_parallel_extract = true;
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
		g_shp_transparency = AfxGetApp()->GetProfileInt("Theme", "shp_transparency", 0) != 0;
		g_alpha_color = static_cast<COLORREF>(AfxGetApp()->GetProfileInt("Theme", "alpha_color", RGB(0, 255, 0)));
		g_use_checkerboard = AfxGetApp()->GetProfileInt("Theme", "use_checkerboard", 1) != 0;
		g_use_external_programs = AfxGetApp()->GetProfileInt("Theme", "use_external_programs", 0) != 0;
		int iv = AfxGetApp()->GetProfileInt("Theme", "interpolation", interp_nearest);
		// Out-of-range falls back to nearest (covers stale interp_ewa=4 values).
		if (iv < interp_nearest || iv > interp_lanczos) iv = interp_nearest;
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
		g_limit_vxl_cpu = AfxGetApp()->GetProfileInt("Theme", "limit_vxl_cpu", 0) != 0;
		g_vxl_full_hierarchy = AfxGetApp()->GetProfileInt("Theme", "vxl_full_hierarchy", 1) != 0;
		g_parallel_extract = AfxGetApp()->GetProfileInt("Theme", "parallel_extract", 1) != 0;
		create_brushes();
	}

	void save()
	{
		AfxGetApp()->WriteProfileInt("Theme", "mode", g_mode);
		AfxGetApp()->WriteProfileInt("Theme", "show_grid", g_show_grid ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "shp_transparency", g_shp_transparency ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "alpha_color", static_cast<int>(g_alpha_color));
		AfxGetApp()->WriteProfileInt("Theme", "use_checkerboard", g_use_checkerboard ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "use_external_programs", g_use_external_programs ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "interpolation", static_cast<int>(g_interp));
		AfxGetApp()->WriteProfileInt("Theme", "sharpen_amount", g_sharpen_amount);
		AfxGetApp()->WriteProfileInt("Theme", "fps_cap", g_fps_cap);
		AfxGetApp()->WriteProfileInt("Theme", "size_format", static_cast<int>(g_size_fmt));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_supersample", static_cast<int>(g_vxl_ss));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_shading", g_vxl_shading ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_normal_src", static_cast<int>(g_vxl_normal_src));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_normal_method", static_cast<int>(g_vxl_normal_method));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_normal_kernel", static_cast<int>(g_vxl_normal_kernel));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_az", static_cast<int>(g_vxl_light_az * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_el", static_cast<int>(g_vxl_light_el * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_ambient", static_cast<int>(g_vxl_light_ambient * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "vxl_light_diffuse", static_cast<int>(g_vxl_light_diffuse * 1000.0f));
		AfxGetApp()->WriteProfileInt("Theme", "limit_vxl_cpu", g_limit_vxl_cpu ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "vxl_full_hierarchy", g_vxl_full_hierarchy ? 1 : 0);
		AfxGetApp()->WriteProfileInt("Theme", "parallel_extract", g_parallel_extract ? 1 : 0);
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

	size_format size_fmt() { return g_size_fmt; }

	void set_size_fmt(size_format v)
	{
		if (g_size_fmt == v)
			return;
		g_size_fmt = v;
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

	float vxl_light_azimuth()   { return g_vxl_light_az; }
	float vxl_light_elevation() { return g_vxl_light_el; }
	float vxl_light_ambient()   { return g_vxl_light_ambient; }
	float vxl_light_diffuse()   { return g_vxl_light_diffuse; }
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
		g_vxl_lighting_version++;
		save();
	}
	void vxl_light_direction(float& x, float& y, float& z)
	{
		const float pi = 3.14159265358979323846f;
		float az_rad = g_vxl_light_az * pi / 180.0f;
		float el_rad = g_vxl_light_el * pi / 180.0f;
		float ce = std::cos(el_rad);
		// Convention: az=0 → +X, az=90° → +Y; elevation lifts toward +Z.
		// Default az=225°, el=54.7356° → x=-0.40825, y=-0.40825, z=+0.81650
		// (matches the original hand-tuned constants).
		x = ce * std::cos(az_rad);
		y = ce * std::sin(az_rad);
		z = std::sin(el_rad);
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

	bool vxl_full_hierarchy() { return g_vxl_full_hierarchy; }

	void set_vxl_full_hierarchy(bool v)
	{
		if (g_vxl_full_hierarchy == v)
			return;
		g_vxl_full_hierarchy = v;
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

	static LRESULT CALLBACK dark_header_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const wchar_t* k_orig_proc = L"xcc.dark_header_orig_proc";
		WNDPROC orig = reinterpret_cast<WNDPROC>(::GetPropW(h, k_orig_proc));
		if (msg == WM_PAINT && is_dark())
		{
			paint_dark_header(h);
			return 0;
		}
		if (msg == WM_NCDESTROY)
		{
			::RemovePropW(h, L"xcc.dark_header_subclass");
			::RemovePropW(h, k_orig_proc);
			::SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		}
		return orig
			? ::CallWindowProcW(orig, h, msg, wp, lp)
			: ::DefWindowProcW(h, msg, wp, lp);
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
		// Group-box BUTTONs need a subclass to owner-draw their caption in
		// dark mode (uxtheme / classic both ignore SetTextColor for them).
		if (_stricmp(cls, "BUTTON") == 0)
		{
			LONG_PTR style = ::GetWindowLongPtrW(h, GWL_STYLE);
			if ((style & 0xF) == BS_GROUPBOX)
				install_dark_groupbox_subclass(h);
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

		// The popup gutter holds the checkmark glyph; menu-bar items have no
		// checkmark and use a flush-left text rect instead.
		const int gutter = 24;
		const bool checked = !is_bar && (dis->itemState & ODS_CHECKED) != 0;
		if (checked)
		{
			// Win11-ish thin tick: 3-segment polyline through the gutter
			// midpoint. Stroke color follows text color so disabled items dim.
			COLORREF mark = disabled ? text_dim() : text();
			HPEN pen = ::CreatePen(PS_SOLID, 2, mark);
			HGDIOBJ old_pen = ::SelectObject(hdc, pen);
			int cx = r.left + gutter / 2;
			int cy = (r.top + r.bottom) / 2;
			POINT pts[3] = {
				{ cx - 5, cy + 0 },
				{ cx - 1, cy + 4 },
				{ cx + 6, cy - 4 },
			};
			::Polyline(hdc, pts, 3);
			::SelectObject(hdc, old_pen);
			::DeleteObject(pen);
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
