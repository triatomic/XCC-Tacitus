#pragma once

#include <cc_file.h>
#include <mix_file.h>
#include <mix_file_rd.h>
#include <palette.h>
#include "palette_filter.h"

struct t_text_cache_entry
{
	CRect text_extent;
	string t;
};

using t_text_cache = vector<t_text_cache_entry>;

class CXCCFileView : public CScrollView
{
protected:
	CXCCFileView();           // protected constructor used by dynamic creation
	DECLARE_DYNCREATE(CXCCFileView)
public:
	// Re-runs theme::apply_window on every player-band child control and
	// forces a full repaint. Call from CMainFrame::apply_theme_to_children
	// when the user toggles light <-> dark.
	void reapply_player_theme();
	// Bump the player BGRA cache version so the next paint reconverts each
	// frame. Called from MainFrm when theme settings that affect SHP/WSA
	// rendering change (alpha color, checkerboard toggle, shp transparency).
	// Bump cache version + immediately re-prefill if in SHP/WSA player mode
	// so the next animation tick is a hit instead of a miss-burst.
	void invalidate_player_bgra_cache()
	{
		m_player_bgra_version++;
		if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
	}
	// Convert a single SHP/WSA paletted frame to BGRA into dst (cx*cy entries),
	// applying the current side color, BG, shadow-pair, and palette state.
	// Used both by player_draw (per-paint, on cache miss) and the player_enter
	// prefill so the first playthrough is already cache-hit.
	void player_convert_frame_to_bgra(int frame_idx, DWORD* dst) const;
	// Run player_convert_frame_to_bgra over all frames in parallel and stash
	// the results in m_player_bgra. Called at the end of player_enter so the
	// timer-driven repaints during the first animation loop don't trigger
	// per-frame conversion bursts.
	void player_prefill_bgra_cache();
	bool can_auto_select();
	void auto_select();
	void close_f();
	const t_palette_entry* get_default_palette();
	void load_color_table(const t_palette palette, bool convert_palette);
	void draw_image8(const byte* s, int cx_s, int cy_s, CDC* pDC, int x_d);
	void draw_image24(const byte* s, int cx_s, int cy_s, CDC* pDC);
	void draw_image32(const byte* s, int cx_s, int cy_s, CDC* pDC, bool bgra = false);
	void draw_image48(const byte* s, int cx_s, int cy_s, CDC* pDC);
	void draw_image64(const byte* s, int cx_s, int cy_s, CDC* pDC);
	void draw_info(string n, string d);
	// void set_game(t_game);
	void open_f(int id, Cmix_file& mix_f, t_game game, t_palette palette);
	void open_f(const string& name);
	void post_open(Ccc_file& f);
	// Re-open the currently-displayed file (used by Theme toggles that change
	// what post_open should do — currently Load Full Hierarchy). No-op when
	// nothing is open. Picks the original source: MIX path if m_source_mix is
	// non-null, disk path otherwise.
	void reload_current();

	//{{AFX_VIRTUAL(CXCCFileView)
protected:
	virtual void OnDraw(CDC* pDC);      // overridden to draw this view
	virtual void OnInitialUpdate();     // first time after construct
	//}}AFX_VIRTUAL

protected:
	virtual ~CXCCFileView();
	//{{AFX_MSG(CXCCFileView)
	afx_msg void OnDisable(CCmdUI* pCmdUI);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnPlayerPlay();
	afx_msg void OnPlayerReverse();
	afx_msg void OnPlayerGrid();
	afx_msg void OnPlayerNative();
	afx_msg void OnPlayerFpsChange();
	afx_msg void OnPlayerShadows();
	afx_msg void OnPlayerBg();
	afx_msg void OnPlayerSide(UINT id);
	afx_msg void OnPlayerSideCustom();
	afx_msg void OnVxlSide(UINT id);
	afx_msg void OnVxlSideCustom();
	afx_msg void OnVxlHvaLoad();
	afx_msg void OnVxlHvaLoop();
	afx_msg void OnLoadPal();
	afx_msg void OnPlayerGridSel();
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	bool m_show_alpha_only = false;
	int m_zoom_pct = 100;
	bool m_zoomable_file = false;

	bool is_playable_file() const;
	void player_enter();
	void player_exit();
	void player_toggle_play();
	void player_set_frame(int f);
	void player_layout_controls();
	void player_update_label();
	int player_total_frames() const;
	void player_decode_frames();
	void player_draw(CDC* pDC);

	bool m_player_mode = false;
	bool m_player_playing = false;
	int m_player_frame = 0;
	int m_player_fps = 15;
	int m_player_cx = 0;
	int m_player_cy = 0;
	int m_player_cf = 0;
	vector<Cvirtual_binary> m_player_frames;
	CButton m_player_play;
	CButton m_player_reverse;
	CButton m_player_grid;
	CButton m_player_native;
	bool m_player_native_size = false;
	// Ctrl+wheel zoom override for the player (SHP/WSA/VXL). 0 = follow
	// auto-fit / Native mode; otherwise an explicit percentage 25..1600.
	int m_player_zoom_pct = 0;
	// Right-drag pan offset (added to the centered image x_d/y_d in
	// player_draw). Reset when entering the player, when zoom changes, and
	// on Native toggle. Useful when Ctrl+wheel zoom makes the SHP/WSA/VXL
	// larger than the viewport.
	int  m_player_pan_x = 0;
	int  m_player_pan_y = 0;
	bool m_player_panning = false;
	CPoint m_player_pan_origin = CPoint(0, 0);
	int  m_player_pan_x0 = 0;
	int  m_player_pan_y0 = 0;
	bool m_player_reverse_dir = false;
	CSliderCtrl m_player_slider;
	CStatic m_player_label;
	CStatic m_player_fps_label;
	CEdit m_player_fps_edit;
	CSpinButtonCtrl m_player_fps_spin;
	bool m_player_controls_created = false;

	// SHP-player-only controls (ASE preview parity).
	CButton m_player_shadows;       // "Shadows" pair-mode toggle
	CButton m_player_bg;            // "BG" — show palette-color-0 background
	CButton m_player_side[8];       // 8 side-color preset swatches
	CButton m_player_side_custom;   // 9th swatch — opens color picker
	CComboBox m_player_iso_grid;    // Game Grid: None / TS / RA2
	bool m_player_shadows_on = false;
	bool m_player_bg_on = true;     // default = show background (matches ASE)
	int m_player_side_idx = -1;     // -1 = no remap; 0..7 = preset; 8 = custom
	COLORREF m_player_side_custom_color = RGB(0xff, 0xff, 0xff);
	int m_player_grid_mode = 0;     // 0 = none, 1 = TS (48px), 2 = RA2 (60px)

	// VXL-only parallel of the SHP side-color swatches. Kept separate from the
	// SHP set so toggling house color on a VXL doesn't leak state into a SHP
	// preview and vice versa. Same retint convention (palette indices 16..31).
	CButton m_vxl_side[8];
	CButton m_vxl_side_custom;
	int m_vxl_side_idx = -1;
	COLORREF m_vxl_side_custom_color = RGB(0xff, 0xff, 0xff);

	// VXL interactive 3D viewer state. When the file is a .vxl, player mode
	// becomes a 3dsmax-style orbit viewer instead of an animation player.
	struct t_vxl_voxel { double x, y, z; unsigned char color; float nx, ny, nz; };
	vector<t_vxl_voxel> m_vxl_cloud;
	int m_vxl_half = 0;
	double m_vxl_yaw = 0.0;
	double m_vxl_pitch = 30.0 * 3.14159265358979323846 / 180.0;
	bool m_vxl_dragging = false;
	CPoint m_vxl_drag_origin;
	double m_vxl_drag_yaw0 = 0.0;
	double m_vxl_drag_pitch0 = 0.0;
	bool is_vxl_view() const { return m_player_mode && m_ft == ft_vxl; }
	int player_band_h() const { return 64; }
	void do_zoom_step(int sign);

	// HVA (Hierarchical Voxel Animation) overlay for the current VXL. When
	// loaded, m_hva_data holds the parsed .hva file and the player band shows
	// transport controls + slider so the user can scrub through HVA frames.
	// Each frame supplies per-section transform matrices that replace the
	// VXL's static section transforms when rebuilding m_vxl_cloud. Cleared on
	// every post_open of a VXL (HVA is paired with one VXL only).
	Cvirtual_binary m_hva_data;
	bool m_hva_loaded = false;
	// Cached worst-case half-extent across all HVA keyframes (0 = not computed
	// yet). Stabilizes the auto-fit scale during playback: without it, m_vxl_half
	// would be recomputed each frame from the rotated cloud's max radius, and
	// auto-fit would pulse as rotors/turrets swing off-axis (their OBB grows when
	// rotated, shrinks when aligned). Computed once when HVA loads; reset to 0 on
	// HVA unload or VXL switch so the next decode rebuilds it.
	int m_hva_vxl_half = 0;
	CButton m_vxl_hva_load;
	CButton m_vxl_hva_loop;
	bool m_hva_loop = true;
	// Non-owning pointer to the MIX the current file came from (NULL when
	// browsing the filesystem). Set by open_f(int, Cmix_file&) and cleared by
	// open_f(string). Used by Load HVA so the user can pick from the MIX's
	// own .hva entries before falling back to a disk browse.
	Cmix_file* m_source_mix = nullptr;

	// Directory the current file was loaded from on disk. Empty when the file
	// came from a MIX. Set by open_f(string) so VXL full-hierarchy lookup can
	// resolve sibling tur/barl files in the same folder (mirrors Vengi's
	// cachingArchive). m_fname is basename-only so we can't reconstruct from it.
	string m_disk_dir;

	// Auto-loaded sibling parts for VXLs (turret + barrel), populated by
	// post_open when theme::vxl_full_hierarchy() is on and matching files
	// exist in the source MIX, the opposite pane's MIX, or the same folder.
	// Each entry's bytes feed an additional Cvxl_file in player_decode_frames
	// which appends its voxels to the merged m_vxl_cloud. Each part can have
	// its own exact-name HVA (e.g. apoctur.hva for apoctur.vxl) driving its
	// motion independently of the body's HVA.
	struct t_vxl_part
	{
		Cvirtual_binary vxl_data;
		Cvirtual_binary hva_data;
		bool hva_loaded = false;
		string name;
	};
	vector<t_vxl_part> m_vxl_parts;

	// Try to find `name` (case-insensitive) in: (1) the source MIX of the
	// currently-open file, (2) the other Mixer pane's MIX, (3) the same disk
	// folder. Returns an empty Cvirtual_binary when not found.
	Cvirtual_binary find_in_sources(const string& name);
	// Populate m_vxl_parts for the currently-open VXL by attempting to load
	// `<base>tur.vxl` and `<base>barl.vxl` (+ matching .hva for each) via
	// find_in_sources. Called from post_open when the toggle is on.
	void vxl_load_parts();

	// Persistent "Load PAL..." button. Unlike the HVA button, this lives
	// across the whole view lifetime (created in OnInitialUpdate) so it can
	// also appear in grid view, not just in player mode. Shown for all
	// paletted file types (SHP/WSA/TMP/PCX/CPS/VXL). Loading a PAL appends
	// to MainFrame::m_pal_list and selects it via set_palette(), keeping
	// Ctrl+Q traversal and auto_select() compatible with the new entry.
	CButton m_load_pal_btn;
	bool m_load_pal_btn_created = false;
	// Height of the bottom mini-band reserved for the Load PAL button when
	// the current file is paletted AND the player is not active. Player mode
	// uses its own band (player_band_h) and parks the button into it.
	int pal_band_h() const { return 32; }
	bool is_paletted_file() const;
	void load_pal_btn_layout();
	void load_pal_btn_update_visibility();
	// Wires the PAL bytes into MainFrame's palette list as a fresh entry
	// (under a synthetic "Loaded" tree node) and calls set_palette() so the
	// file repaints with it. Returns false on parse failure.
	bool apply_loaded_pal(const Cvirtual_binary& data, const string& display_name);
	// On opening a paletted file, scan the source MIX for a .pal whose
	// basename (sans extension, lowercased) matches the file's basename
	// exactly. If found, load it via apply_loaded_pal so the file paints
	// with its paired palette without the user clicking Load PAL. No-op
	// for non-paletted files, files opened from disk (no source MIX), or
	// when no exact-stem .pal exists.
	void try_auto_load_paired_pal();

private:
	COLORREF  m_colour = RGB(40, 40, 40);
	CRect			clientRect;
	bool			m_can_pick;
	CRect			m_clip_rect;
	DWORD			m_color_table[256];
	int				m_cx;
	int				m_cy;
	int				m_cx_dib;
	Cvirtual_binary	m_data;
	CDC*			m_dc;
	string			m_fname;
	t_game			m_game;
	HBITMAP			mh_dib;
	DWORD*			mp_dib;
	CFont			m_font;
	t_file_type		m_ft;
	int				m_id;
	bool			m_is_open = false;
	t_palette_entry*	m_palette;
	Cpalette_filter m_palette_filter;
	long long		m_size;
	t_text_cache	m_text_cache;
	bool			m_text_cache_valid;
	// SHP/SHP_TS/SHP_Dune2 grid cache. Built lazily on the first OnDraw of
	// each new file; reused while the same file is open. Lets scrolling skip
	// the per-frame walk (cum_y[] + binary search to first visible) and the
	// per-frame RLE decode (decoded[] is filled on first paint of each
	// frame). Invalidated by post_open via m_grid_cache_token.
	struct shp_grid_cache
	{
		int token = -1;	// matches m_open_token when valid
		// cum_y[i] = vertical offset of frame i relative to the first frame's
		// top. cum_y has c_images + 1 entries; cum_y[c_images] = total height,
		// used by upper_bound to locate the first-visible frame.
		std::vector<int> cum_y;
		// Lazily-filled decoded frame bytes (paletted, cx*cy). Size 0 = not
		// yet decoded; populated on first paint of that frame. Only used by
		// the SHP_TS path (compressed frames need RLEZeroTSDecompress);
		// SHP_Dune2 also benefits since LCWDecompress + decode2 is per-frame.
		std::vector<Cvirtual_binary> decoded;
	};
	shp_grid_cache m_shp_grid;
	int m_open_token = 0;	// bumped in post_open; cache token must match

	// VXL splat cache. The point-splat into the supersample paletted framebuffer
	// is camera-only (depends on yaw/pitch/ss/shading + the file), so it can be
	// reused across paints when nothing has changed. Per-paint work then drops
	// to: palette->BGRA pass, optional grid overlay, scale + blit. This is what
	// makes idle viewing (no orbit) cheap; without it the OpenMP splat region
	// fires on every WM_PAINT regardless of whether anything moved.
	//
	// Key = (token, yaw, pitch, ss, shading). All components are exact: yaw and
	// pitch are doubles compared bit-for-bit since drag writes them and a paint
	// reads the same values. Anything else (side color, BG toggle, palette,
	// zoom, pan, viewport) doesn't affect the splat output and is applied
	// downstream.
	struct vxl_splat_cache
	{
		int token = -1;
		double yaw = 0.0;
		double pitch = 0.0;
		int ss = 0;
		bool shading = false;
		int lighting_version = -1;	// theme::vxl_lighting_version() at build time
		int cx_s = 0;
		int cy_s = 0;
		Cvirtual_binary buf;	// paletted supersample framebuffer (cx_s*cy_s bytes)
		std::vector<unsigned char> shade;	// per-pixel shade factor; empty when shading off
	};
	vxl_splat_cache m_vxl_splat;

	// VXL BGRA cache. The splat output (m_vxl_splat) is the paletted
	// supersample buffer; this caches the composited BGRA result of running
	// the palette/side-color/bg/shading loop over it. Idle viewing of a still
	// VXL drops to memcpy + StretchBlt; orbiting still rebuilds (because the
	// underlying splat changed). Side color / BG / grid / checker toggles
	// just bump the version int and the next paint rebuilds — only one
	// buffer's worth of work, so no prefill needed unlike the SHP cache.
	struct vxl_bgra_cache
	{
		int splat_token = -1;
		double splat_yaw = 0.0;
		double splat_pitch = 0.0;
		int splat_ss = 0;
		bool splat_shading = false;
		int splat_lighting_version = -1;
		int side = -2;	// -1 = no remap; -2 = "never matched" sentinel
		COLORREF custom_color = 0;
		bool bg_on = false;
		int grid_mode = 0;
		COLORREF ck_a = 0;
		COLORREF ck_b = 0;
		int cx_s = 0;
		int cy_s = 0;
		std::vector<DWORD> bgra;
	};
	vxl_bgra_cache m_vxl_bgra;

	// SHP/WSA player BGRA cache. Per-paint palette->BGRA conversion (with
	// side-color remap, shadow blend, BG checker, shading) is the dominant
	// cost during animation playback — at 15-30 fps a 100x100 frame's loop
	// repeats every tick along with a full CreateDIBSection call. The
	// converted bytes only change when something user-visible changes, so we
	// cache them lazily per frame and invalidate by bumping a version int.
	//
	// version mismatch on frames[i] => rebuild that frame's BGRA from
	// m_player_frames[i]. Animation tick runs the cheap path: if cached
	// version matches, just memcpy into the per-paint DIB and StretchBlt.
	// Bumped by: player_enter, post_open, side/bg/shadow toggle, palette
	// change, alpha checker color change.
	struct shp_bgra_cache_entry
	{
		int version = -1;
		std::vector<DWORD> bgra;	// cx_s * cy_s entries
	};
	std::vector<shp_bgra_cache_entry> m_player_bgra;
	int m_player_bgra_version = 0;
	int				m_x;
	int				m_y;
	int				m_y_inc;
	CBrush test_brush;
	static const int offset = 4;
};
