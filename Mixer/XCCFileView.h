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
	int				m_x;
	int				m_y;
	int				m_y_inc;
	CBrush test_brush;
	static const int offset = 4;
};
