#pragma once

#include <afxwin.h>
#include <afxext.h>

// Splitter that paints its gutters with theme colors when dark mode is on.
// Drop-in replacement for CSplitterWnd.
class CThemedSplitterWnd : public CSplitterWnd
{
public:
	// When true, HitTest() returns noHit for any column-splitter bar,
	// effectively making the splitter non-draggable. Used by the One Pane
	// layout: with middle col collapsed to 0px, bars 201 and 202 sit at the
	// same X and both are easy to grab accidentally; disabling them entirely
	// removes the trap. Row splitters (vSplitterBar*) still work, the only
	// active row case in this app is none anyway.
	void set_columns_locked(bool locked) { m_columns_locked = locked; }
	bool columns_locked() const { return m_columns_locked; }

protected:
	int HitTest(CPoint pt) const override;
	void OnDrawSplitter(CDC* pDC, ESplitType nType, const CRect& rect) override;
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	bool m_columns_locked = false;
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
		mode_system = 2, // Follow Windows' AppsUseLightTheme setting.
	};

	void load();
	void save();
	mode get();
	void set(mode m);
	bool is_dark();
	// Reads the Windows "AppsUseLightTheme" registry value. Returns true when
	// the system is configured for dark apps. Used by mode_system; safe to call
	// even when the registry value is absent (returns false).
	bool system_prefers_dark();
	// Call from WM_SETTINGCHANGE handlers. When in mode_system, re-queries the
	// system preference and returns true if the resolved dark/light state
	// flipped (caller should rebuild theme-dependent UI). Returns false in any
	// other mode or when the resolved state didn't change.
	bool refresh_system_mode();

	bool show_grid();
	void set_show_grid(bool v);

	bool show_column_headers();
	void set_show_column_headers(bool v);

	// When on, search / picker dialogs (Ctrl+F Search in current location,
	// Load PAL...) hide all rows when the filter edit is empty. Default off
	// preserves the populate-everything-on-open behavior shipped in v10.71.
	bool hide_empty_results();
	void set_hide_empty_results(bool v);

	// Size column format: Auto = "1.5 MB" via totalSize(); Bytes = grouped
	// raw byte count "1,572,864". Applies to the listview panes and to the
	// right-pane MIX/PAK/BIG/RG-MIX content listings.
	enum size_format
	{
		size_auto = 0,
		size_bytes = 1,
	};
	size_format size_fmt();
	void set_size_fmt(size_format v);
	// Format a byte count using the active size_format. Pass negative for
	// "no size" (returns empty string).
	std::string format_size(long long bytes);

	// Screenshot -> Copy to Clipboard payload format.
	// Indexed = 8bpp paletted CF_DIB (m_color_table as palette, raw indexed
	// pixels - smallest payload, round-trips into Mixer's own Paste-as-SHP).
	// RGB = 32bpp BGRA CF_DIB built from the WYSIWYG composite (whatever the
	// player shows is what lands on the clipboard, including BG=Color/Alpha/
	// Pane bake-in - same as Record's wysiwyg path).
	enum clipboard_format
	{
		clipboard_indexed = 0,
		clipboard_rgb = 1,
	};
	clipboard_format clipboard_fmt();
	void set_clipboard_fmt(clipboard_format v);

	// VXL supersampling: render the voxel splat at NxN canvas resolution then
	// let the chosen interpolation downscale it. Off = native 1x (cheapest,
	// no real silhouette AA); 2/4/8 = progressively cleaner edges at higher
	// memory and per-paint cost.
	enum vxl_ss
	{
		vxl_ss_off = 1,
		vxl_ss_2 = 2,
		vxl_ss_4 = 4,
		vxl_ss_8 = 8,
		vxl_ss_16 = 16,
	};
	vxl_ss vxl_supersample();
	void set_vxl_supersample(vxl_ss v);


	// VXL directional shading: lights voxels by a camera-relative directional
	// light. Adds depth/form to flat-colored voxel models.
	bool vxl_shading();
	void set_vxl_shading(bool v);

	// VXL lighting parameters. The shader is a simple Lambertian directional
	// light in camera space. Direction is stored as azimuth (0..360°, around
	// the screen Z axis from +X) and elevation (-90..+90°, tilt above the XY
	// plane). Ambient is the floor brightness for surfaces facing fully away
	// from the light; Diffuse is the additive range above ambient. Final
	// brightness range is [ambient, ambient + diffuse].
	//
	// Defaults: az=225°, el=54.7° → light_x=-0.40825, light_y=-0.40825,
	// light_z=0.81650 (the original hand-tuned upper-left-front direction).
	// ambient=0.55, diffuse=0.85.
	float vxl_light_azimuth();         // degrees, 0..360
	float vxl_light_elevation();       // degrees, -90..90
	float vxl_light_ambient();         // 0..1
	float vxl_light_diffuse();         // 0..1
	// Specular: peak intensity above (ambient + diffuse) when a face is
	// fully lit. Ported from vxl-renderer's per-colorset VPL curve
	// (mainwindow.cpp:543) where f<1 ? amb+f*dif : amb+dif+(f-1)*(dif+spec).
	// 0 = strict Lambertian (matches Mixer's pre-specular behavior). 1.2 =
	// vxl-renderer default. Range 0..5.
	float vxl_light_specular();        // 0..5
	void set_vxl_light_azimuth(float v);
	void set_vxl_light_elevation(float v);
	void set_vxl_light_ambient(float v);
	void set_vxl_light_diffuse(float v);
	void set_vxl_light_specular(float v);
	void reset_vxl_lighting();
	// Bumped on every lighting setter; used as a cheap cache key by the VXL
	// splat cache so changing lighting invalidates without explicit flushes.
	int vxl_lighting_version();

	// Flush deferred lighting writes to the registry. The four slider setters
	// (azimuth/elevation/ambient/diffuse) update the in-memory value + bump
	// the lighting version but skip the per-call save() so rapid slider
	// drags don't drown the registry. The dialog calls this on slider release
	// (TB_ENDTRACK / OnLButtonUp).
	void flush_lighting_save();

	// Source of per-voxel normals for the shading dot product:
	// - computed (0, default): 6-neighbor occupancy at file load
	//   (lit-able cube faces). Smooth, view-independent of disk contents.
	// - file (1): the on-disk Westwood normal index looked up into the
	//   per-section table (TS = 36 entries, RA2/YR = 244). What the engine
	//   itself used to shade these units.
	// Changing this requires a *cloud* rebuild (not just splat cache), so
	// the dialog handler also bumps the file view's m_open_token.
	enum vxl_normal_source
	{
		vxl_normals_computed = 0,
		vxl_normals_file = 1,
	};
	vxl_normal_source vxl_normal_src();
	void set_vxl_normal_src(vxl_normal_source v);

	// Algorithm used to derive normals when vxl_normal_src() == computed.
	// Cheaper -> better:
	//   basic    = legacy 6-neighbor empty-side sum (~7 unique directions).
	//   weighted = Vengi-style 26-neighbor filled-neighbor sum at
	//              face/edge/corner weights (~26 distinct directions, less
	//              faceting on diagonals).
	//   gradient = central-difference gradient of a separable-Gaussian-blurred
	//              occupancy field; continuous directions, smooth on curved
	//              hulls. The industry-standard volume-viz normal.
	enum vxl_normal_method
	{
		vxl_method_basic    = 0,
		vxl_method_weighted = 1,
		vxl_method_gradient = 2,
	};
	vxl_normal_method vxl_normals_method();
	void set_vxl_normals_method(vxl_normal_method v);

	// Kernel size for the Gaussian blur in vxl_method_gradient. 3^3 keeps
	// fine features (antennas, barrels) crisp; 5^3 smooths blocky hulls more
	// aggressively at the cost of softening thin features.
	enum vxl_normal_kernel
	{
		vxl_kernel_3 = 0,
		vxl_kernel_5 = 1,
	};
	vxl_normal_kernel vxl_normals_kernel();
	void set_vxl_normals_kernel(vxl_normal_kernel v);

	// Ambient occlusion: view-independent, baked per-voxel at file open.
	// Strength multiplies the baked occlusion term in the shade pass:
	// final = base * (1 - (ao/255) * strength/100). 0 = AO disabled,
	// 100 = full strength. Bake runs eagerly regardless of `enabled` so
	// toggling on/off is instant.
	bool vxl_ao_enabled();
	void set_vxl_ao_enabled(bool v);
	int  vxl_ao_strength();        // 0..100
	void set_vxl_ao_strength(int v);

	// Bake quality. Affects ray count + max ray length used by the per-voxel
	// AO bake. Higher tiers smooth crevice gradients and capture broader
	// concavities; cost scales roughly linearly with ray_count. Tier change
	// requires a cloud rebuild (the dialog handler routes through
	// CMainFrame::invalidate_vxl_cloud_in_file_view).
	enum vxl_ao_quality
	{
		ao_q_low   = 0,	// 16 rays, 5-cell range
		ao_q_high  = 1,	// 32 rays, 8-cell range — default
		ao_q_ultra = 2,	// 64 rays, 8-cell range
	};
	vxl_ao_quality vxl_ao_quality_v();
	void set_vxl_ao_quality(vxl_ao_quality v);
	// Resolve azimuth/elevation into a unit direction vector in camera space.
	void vxl_light_direction(float& x, float& y, float& z);

	// Light-direction indicator overlay. Auto-shown while the VXL Lighting
	// dialog is open (visibility flag, not persisted) and drawn by the VXL
	// player's paint path. Placement mode is persisted across sessions so the
	// user's choice sticks.
	enum vxl_light_indicator_placement
	{
		vxl_light_indicator_overlay = 0,  // drawn on top of the model
		vxl_light_indicator_corner  = 1,  // small widget in the top-right corner
	};
	// When true, VPL section selection uses the engine-faithful
	// `(1 - N·L) / 2 * 31` formula — Ambient and Diffuse sliders are
	// ignored for VPL shading. When false, VPL goes through the same
	// `ambient + diffuse * max(0, N·L)` modulation as synthetic shading,
	// which gives extra creative control but isn't what the engine does.
	// Default true so out-of-the-box VPL output matches in-game.
	bool vxl_vpl_engine_faithful();
	void set_vxl_vpl_engine_faithful(bool v);

	bool vxl_light_indicator_visible();
	void set_vxl_light_indicator_visible(bool v);
	vxl_light_indicator_placement vxl_light_indicator_mode();
	void set_vxl_light_indicator_mode(vxl_light_indicator_placement v);

	// Limit VXL CPU: when on, the VXL splat OpenMP region runs at half the
	// hardware thread count (min 1). Lets the user trade splat-rebuild latency
	// for lower idle-orbit core wake-up + heat without losing parallelism
	// entirely. The splat itself is cached across paints (vxl_splat_cache in
	// CXCCFileView), so this only affects the cost of cache misses (drag,
	// ss/shading toggle, file change).
	bool limit_vxl_cpu();
	void set_limit_vxl_cpu(bool v);

	// When on, opening a VXL body (e.g. apoc.vxl) also auto-loads sibling
	// parts apoctur.vxl (turret) and apocbarl.vxl (barrel) from the same MIX,
	// the opposite Mixer pane's MIX, or — for disk-loaded VXLs — the same
	// folder. Each part auto-pairs with its exact-name .hva. Matches Vengi's
	// behavior (VXLFormat.cpp). Default on.
	bool vxl_full_hierarchy();
	void set_vxl_full_hierarchy(bool v);

	// Parallel batch extract: when on, right-click → Extract / Extract
	// preserving structure reads each selected entry's bytes serially on the
	// UI thread (the MIX file handle isn't thread-safe — Ccc_file shares the
	// parent's HANDLE via Cmix_file, so concurrent seek/read would race),
	// then writes the gathered Cvirtual_binary blobs to disk in parallel.
	// Real wins on large extracts to a fast SSD; degrades to roughly serial
	// speed on slow media because writes are I/O-bound.
	bool parallel_extract();
	void set_parallel_extract(bool v);

	// When on, paletted Westwood images (SHP/PCX/CPS/WSA) treat palette index 0
	// as transparent — the engine convention. Off = paint index 0 with whatever
	// color the palette says, like older XCC builds.
	bool shp_transparency();
	void set_shp_transparency(bool v);
	// Apply the grid setting to a listview's extended style.
	void apply_grid(HWND h_listview);
	// Apply the column-headers setting to a listview by flipping
	// LVS_NOCOLUMNHEADER. Hides/shows the SysHeader32 band; rows
	// reflow on the next layout cycle.
	void apply_column_headers(HWND h_listview);

	// Enable the Explorer-style right-click-on-header → checkable popup
	// of column names. Each click toggles a column's visibility by
	// setting its width to 0 (hide) or its saved last-visible width
	// (show). Per-listview state persists under
	// "Theme\\col_<lv_id>" keys. `lv_id` must be a short stable string
	// (e.g. "main_pane_left", "search_file"). Call after the listview's
	// columns have been inserted and the theme has been applied.
	// Idempotent. Also restores any previously hidden columns from
	// persisted state.
	void enable_column_visibility_menu(HWND h_listview, const char* lv_id);

	// Install a WM_PAINT subclass on a listview that has
	// EnableGroupView(TRUE). In dark mode, overlays each group header
	// title bar with the theme background + white text after the system
	// paints it (Win10/11 ignore CDDS_GROUP clrText/clrTextBk overrides,
	// leaving the dim default-light group bar unreadable on a dark
	// background). No-op in light mode. Idempotent across theme switches.
	void apply_listview_groups(HWND h_listview);

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
		// Pixel-art aware upscalers. These don't run inside stretch_image —
		// they pre-scale the source BGRA into the SHP/WSA cache at 2x/3x/4x
		// before stretch_image's regular interpolation kicks in. SHP/WSA
		// only; VXL already has its own supersample slider for the same
		// purpose. Selecting any of these forces stretch_image to use
		// interp_bilinear for the downscale step (running bicubic / lanczos
		// on top of the algorithmic reconstruction adds unwanted blur).
		interp_scale2x = 4,   // AdvanceMAME Scale2x — corner-fill jaggy smooth
		interp_scale3x = 5,   // AdvanceMAME Scale3x
		interp_hq2x    = 6,   // Maxim Stepin HQ2x — heavy smoothing
		interp_hq4x    = 7,   // HQ4x (HQ2x chained twice or direct)
		interp_xbr2x   = 8,   // Hyllian xBR-2x — best on curves/diagonals
		interp_xbr4x   = 9,   // xBR-4x (xBR-2x chained twice)
		// NNEDI3 — scalar port of znedi3 (Tritical's neural-network edge-
		// directed interpolation). Substantially higher quality than xBR on
		// hand-drawn 2D content, ~4-8x slower per pixel. Weights ship as
		// the NNEDI3_WEIGHTS RCDATA resource (~200 KB). SHP/WSA only.
		interp_nnedi2x = 10,
		interp_nnedi4x = 11,
	};
	interpolation interp();
	void set_interp(interpolation v);

	// Pre-upscale factor for the SHP/WSA BGRA cache when a pixel-art
	// upscaler is selected. Returns 2/3/4 for those modes, 1 for any
	// regular kernel. Centralized so the cache-fill site and the
	// stretch_image source-dim math agree.
	int interp_upscale_factor();
	// True iff theme::interp() is a pixel-art upscaler. The downstream
	// stretch_image switches to bilinear in this case — applying bicubic
	// or Lanczos on top of an algorithmically reconstructed image adds
	// unwanted blur.
	bool interp_is_pixel_art_upscaler();

	// Unsharp-mask amount applied as a post-pass to the non-nearest
	// interpolation paths in stretch_image. 0 = off (current behavior),
	// 100 = full strength. Lets the user dial in crispness independently
	// of which kernel they pick — also makes the interpolation menu have
	// a visible effect at zooms where source and destination sizes match
	// (e.g. SHP/WSA at Native), where the kernel would otherwise be a
	// no-op identity convolution.
	int sharpen_amount();             // 0..100
	void set_sharpen_amount(int v);

	// Global frame-rate cap for CXCCFileView paints. Coalesces rapid
	// invalidates (slider drag, orbit drag) into at most N paints per
	// second. Stored as a plain int so the user can pick any value via a
	// "Custom..." menu item; presets at 30/60/120/666 are exposed in the
	// Theme menu. 666 == the "Unlimited" preset (soft cap). Default 60.
	// Persisted as "Theme\\fps_cap". Clamped to 1..9999 on load/set.
	int frame_rate_cap();
	void set_frame_rate_cap(int v);
	constexpr int fps_unlimited_value = 1000;
	// Stretch-blit src onto dst using the configured interpolation. Source must
	// be a 32bpp top-down DIB; src_bits is the linear pixel array (only used
	// for the Lanczos path — others read from src_dc).
	void stretch_image(CDC* dst, int dx, int dy, int dw, int dh,
		CDC* src_dc, HBITMAP src_dib, const DWORD* src_bits, int sw, int sh);
	// Same, but with an explicit interpolation mode (overrides theme::interp()).
	// Used by the VXL viewer to force nearest, since 2D image filters don't
	// improve the output of an already-rasterized 3D voxel splat.
	void stretch_image(CDC* dst, int dx, int dy, int dw, int dh,
		CDC* src_dc, HBITMAP src_dib, const DWORD* src_bits, int sw, int sh,
		interpolation mode);
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

	// Install a window subclass on the top-level frame that intercepts
	// UAH menu-bar messages (WM_UAHDRAWMENU / WM_UAHDRAWMENUITEM) so the
	// menu-bar strip paints in the dark palette. Idempotent — calling more
	// than once is a no-op. Popups themselves are rendered natively by
	// Windows (via SetPreferredAppMode + FlushMenuThemes); UAH only covers
	// the strip behind the top-level items.
	void install_uah_menu_subclass(HWND h_frame);

	// Install a window subclass on a Win32 ComboBox so it paints in dark
	// mode using the Notepad++ ComboBoxSubclass pattern: WM_PAINT draws the
	// closed-state field + dropdown arrow + frame against the theme palette
	// in dark, and falls through to the system painter in light. Works for
	// CBS_DROPDOWN and CBS_DROPDOWNLIST. Idempotent. Safe to call on a
	// ComboBox that also has CBS_HASSTRINGS — must NOT be combined with
	// CBS_OWNERDRAWFIXED/VARIABLE (the subclass owns WM_PAINT itself).
	void subclass_combobox(HWND h_combo);

	// Bilinear resample of a 32bpp BGRA top-down image. The same routine
	// the splat path uses internally; exposed so the turntable Record
	// dialog can downscale supersampled frames to logical size before
	// GIF / PNG encoding (so the output matches what the user sees on
	// screen instead of being SS-bigger). Both upscale and downscale.
	void bilinear_resample_bgra(const DWORD* src, int sw, int sh,
		DWORD* dst, int dw, int dh);
}
