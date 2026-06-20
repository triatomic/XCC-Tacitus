#include "stdafx.h"
#include "MainFrm.h"
#include "XCCFileView.h"
#include "keybinds.h"
#include "pixel_upscale.h"
#include "resource.h"
#include <algorithm>
#include <array>
#include <csf_file.h>
#include <aud_file.h>
#include <big_file.h>
#include <cfloat>
#include <cmath>
#include <cps_file.h>
#include <dds_file.h>
#include <fname.h>
#include <fnt_file.h>
#include <fstream>
#include <gdiplus.h>
#include <hva_file.h>
#include <id_log.h>
#include <map_ra_ini_reader.h>
#include <map_td_ini_reader.h>
#include <map_ts_ini_reader.h>
#include <mp3_file.h>
#include <mix_rg_file.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <pak_file.h>
#include <pal_file.h>
#include <pcx_decode.h>
#include <pcx_file.h>
#include <pcx_file_write.h>
#include <pkt_ts_ini_reader.h>
#include <shp_decode.h>
#include <shp_dune2_file.h>
#include <shp_file.h>
#include <shp_images.h>
#include <shp_ts_file.h>
#include <sstream>
#include <st_file.h>
#include <string_conversion.h>
#include <tga_file.h>
#include <theme_ts_ini_reader.h>
#include <tmp_file.h>
#include <tmp_ra_file.h>
#include <tmp_ts_file.h>
#include <virtual_tfile.h>
#include <voc_file.h>
#include <vqa_file.h>
#include <vpl_file.h>
#include <vxl_file.h>
#include <vxl_normals.h>
#include <wav_file.h>
#include <wsa_dune2_file.h>
#include <wsa_file.h>
#include <png_file.h>
#include <numbers>
#include "theme.h"
#include "TurntableDlg.h"
#include "LoadPalDlg.h"
#include "ColorPickerDlg.h"
// gif.h is a public-domain single-header animated-GIF encoder by Charlie
// Tangora. Vendored under Mixer/gif.h. Used only by OnPlayerTurntable below;
// no other TU should include it (it defines functions in-header without
// inline/static guards beyond what's natural for a single-TU embed).
#include "gif.h"

// Defined further down this TU; forward-declared so the early Load-PAL button
// helpers (which route through the frame) can use it.
static CMainFrame* GetMainFrame();

IMPLEMENT_DYNCREATE(CXCCFileView, CListView)

CXCCFileView::CXCCFileView()
{
}

CXCCFileView::~CXCCFileView()
{
}

BEGIN_MESSAGE_MAP(CXCCFileView, CScrollView)
	ON_UPDATE_COMMAND_UI(ID_FILE_NEW, OnDisable)
	ON_UPDATE_COMMAND_UI(ID_FILE_OPEN, OnDisable)
	ON_UPDATE_COMMAND_UI(ID_FILE_CLOSE, OnDisable)
	ON_WM_MOUSEWHEEL()
	ON_WM_MOUSEHWHEEL()
	ON_WM_ERASEBKGND()
	ON_WM_KEYDOWN()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_PLAYER_PLAY, OnPlayerPlay)
	ON_BN_CLICKED(IDC_PLAYER_REVERSE, OnPlayerReverse)
	ON_BN_CLICKED(IDC_PLAYER_GRID, OnPlayerGrid)
	ON_BN_CLICKED(IDC_PLAYER_NATIVE, OnPlayerNative)
	ON_BN_CLICKED(IDC_PLAYER_SCREENSHOT, OnPlayerScreenshot)
	ON_BN_CLICKED(IDC_PLAYER_TURNTABLE, OnPlayerTurntable)
	ON_EN_CHANGE(IDC_PLAYER_FPS_EDIT, OnPlayerFpsChange)
	ON_BN_CLICKED(IDC_PLAYER_SHADOWS, OnPlayerShadows)
	ON_BN_CLICKED(IDC_PLAYER_BG, OnPlayerBg)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_PLAYER_SIDE0, IDC_PLAYER_SIDE7, OnPlayerSide)
	ON_BN_CLICKED(IDC_PLAYER_SIDE_CUSTOM, OnPlayerSideCustom)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_VXL_SIDE0, IDC_VXL_SIDE7, OnVxlSide)
	ON_BN_CLICKED(IDC_VXL_SIDE_CUSTOM, OnVxlSideCustom)
	ON_BN_CLICKED(IDC_VXL_HVA_LOAD, OnVxlHvaLoad)
	ON_BN_CLICKED(IDC_VXL_HVA_LOOP, OnVxlHvaLoop)
	ON_BN_CLICKED(IDC_VXL_VPL_LOAD, OnVxlVplLoad)
	ON_BN_CLICKED(IDC_LOAD_PAL, OnLoadPal)
	ON_BN_CLICKED(IDC_PLAYER_GRID_SEL, OnPlayerGridSel)
	ON_WM_DRAWITEM()
	ON_WM_MEASUREITEM()
	ON_WM_PAINT()
	ON_WM_CTLCOLOR()
	ON_WM_VSCROLL()
END_MESSAGE_MAP()

void CXCCFileView::OnLButtonDown(UINT nFlags, CPoint point)
{
	// Click steals focus so M actually reaches our key handler.
	SetFocus();
	if (is_vxl_view())
	{
		bool ctrl  = (nFlags & MK_CONTROL) != 0;
		bool shift = (nFlags & MK_SHIFT) != 0;
		bool alt   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		UINT action = 0;
		if (keybinds::match_mouse(keybinds::scope_file_view, keybinds::mb_left, ctrl, shift, alt, action)
			&& action == keybinds::vact_orbit_drag)
		{
			// Only start orbit drag in the image area, not over the controls band.
			CRect cr;
			GetClientRect(&cr);
			if (point.y < cr.bottom - player_band_h())
			{
				m_vxl_dragging = true;
				m_vxl_drag_origin = point;
				m_vxl_drag_yaw0 = m_vxl_yaw;
				m_vxl_drag_pitch0 = m_vxl_pitch;
				m_interactive_low_ss = true;
				SetCapture();
				// Clip the cursor to the image area so dragging doesn't drift onto
				// other windows / the desktop while orbiting. Cleared on button up.
				CRect clip = cr;
				clip.bottom -= player_band_h();
				ClientToScreen(&clip);
				::ClipCursor(&clip);
				return;
			}
		}
	}
	CScrollView::OnLButtonDown(nFlags, point);
}

void CXCCFileView::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_vxl_dragging)
	{
		m_vxl_dragging = false;
		m_interactive_low_ss = false;
		// Force the splat cache to miss for the final paint so it
		// re-rasterizes at the user's chosen supersample factor. Cheapest
		// way is to nudge the cached ss to a sentinel value; the next
		// paint's cache check will mismatch and rebuild.
		m_vxl_splat.ss = -1;
		ReleaseCapture();
		::ClipCursor(NULL);
		// Re-render with the user's chosen interpolation + full SS now that
		// orbit stopped.
		CRect cr; GetClientRect(&cr);
		cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	CScrollView::OnLButtonUp(nFlags, point);
}

void CXCCFileView::OnRButtonDown(UINT nFlags, CPoint point)
{
	// Right-drag pan: only meaningful in player mode, and only when the
	// click lands inside the image area (not on the control band).
	if (m_player_mode)
	{
		bool ctrl  = (nFlags & MK_CONTROL) != 0;
		bool shift = (nFlags & MK_SHIFT) != 0;
		bool alt   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		UINT action = 0;
		if (keybinds::match_mouse(keybinds::scope_file_view, keybinds::mb_right, ctrl, shift, alt, action)
			&& action == keybinds::vact_pan_drag)
		{
			CRect cr;
			GetClientRect(&cr);
			if (point.y < cr.bottom - player_band_h())
			{
				m_player_panning = true;
				m_interactive_low_ss = true;
				m_player_pan_origin = point;
				m_player_pan_x0 = m_player_pan_x;
				m_player_pan_y0 = m_player_pan_y;
				SetCapture();
				::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
				CRect clip = cr;
				clip.bottom -= player_band_h();
				ClientToScreen(&clip);
				::ClipCursor(&clip);
				return;
			}
		}
	}
	CScrollView::OnRButtonDown(nFlags, point);
}

void CXCCFileView::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (m_player_panning)
	{
		m_player_panning = false;
		m_interactive_low_ss = false;
		m_vxl_splat.ss = -1;	// force final paint to rebuild at user's SS
		ReleaseCapture();
		::ClipCursor(NULL);
		// Same as orbit: bring back the full-quality interpolation + SS.
		CRect cr; GetClientRect(&cr);
		cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	CScrollView::OnRButtonUp(nFlags, point);
}

void CXCCFileView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_vxl_dragging && (nFlags & MK_LBUTTON))
	{
		// Drop mouse moves that arrive within the cap window so we don't
		// burn CPU on the orbit math + repaint plumbing at 1000Hz. The
		// dropped events don't update m_vxl_drag_origin, so the next
		// accepted event sees the cumulative delta — visually identical
		// to processing every event.
		if (!throttle_input_tick())
			return;
		// 3dsmax-style orbit: horizontal drag = yaw, vertical drag = pitch.
		// ~0.4 deg per pixel feels right for the tiny VXL framebuffer.
		// Incremental from the last mouse position rather than absolute from
		// the click origin: lets us warp the cursor back when it hits the
		// clip edge so the user can keep dragging in the same direction
		// indefinitely (Blender/3dsmax-style infinite orbit).
		const double k = 0.4 * 3.14159265358979323846 / 180.0;
		int dx = point.x - m_vxl_drag_origin.x;
		int dy = point.y - m_vxl_drag_origin.y;
		if (dx == 0 && dy == 0)
			return;
		m_vxl_yaw   += dx * k;
		m_vxl_pitch += dy * k;
		CRect cr;
		GetClientRect(&cr);
		const int band = player_band_h();
		cr.bottom -= band;
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		// If the cursor is near an edge of the image area, warp it back to
		// the center of the area so the next move has full travel in any
		// direction. ClipCursor keeps it inside; this prevents getting
		// stuck against an edge.
		const int margin = 8;
		bool near_edge =
			point.x <= cr.left + margin || point.x >= cr.right - margin ||
			point.y <= cr.top  + margin || point.y >= cr.bottom - margin;
		if (near_edge)
		{
			CPoint c((cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2);
			m_vxl_drag_origin = c;
			ClientToScreen(&c);
			::SetCursorPos(c.x, c.y);
		}
		else
		{
			m_vxl_drag_origin = point;
		}
		request_repaint(&cr);
		return;
	}
	if (m_player_panning && (nFlags & MK_RBUTTON))
	{
		if (!throttle_input_tick())
			return;
		m_player_pan_x = m_player_pan_x0 + (point.x - m_player_pan_origin.x);
		m_player_pan_y = m_player_pan_y0 + (point.y - m_player_pan_origin.y);
		// Repaint the image area only (margins included). Skip the control
		// band so the panning doesn't strobe the buttons.
		CRect cr;
		GetClientRect(&cr);
		cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		request_repaint(&cr);
		return;
	}
	CScrollView::OnMouseMove(nFlags, point);
}

BOOL CXCCFileView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	// Hover-help: when the cursor is over a player-band child, push that
	// control's short description to the main frame's status bar. Cleared
	// when the cursor leaves the band. Lookup is keyed on HWND so the
	// per-mode BG-button label change doesn't affect the help text.
	update_player_hover_help(pWnd);

	// In player mode, show the hand cursor over the image area to advertise
	// right-drag-to-pan. Outside the image area (control band) and outside
	// player mode, fall through to the default arrow.
	if (m_player_mode && nHitTest == HTCLIENT)
	{
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		CRect cr;
		GetClientRect(&cr);
		if (pt.y >= 0 && pt.y < cr.bottom - player_band_h())
		{
			::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
			return TRUE;
		}
	}
	return CScrollView::OnSetCursor(pWnd, nHitTest, message);
}

void CXCCFileView::update_player_hover_help(CWnd* pWnd)
{
	if (!m_player_controls_created)
		return;
	HWND h = pWnd ? pWnd->GetSafeHwnd() : nullptr;
	const char* msg = nullptr;
	if (h == m_player_play.GetSafeHwnd())          msg = "Play / pause animation.";
	else if (h == m_player_reverse.GetSafeHwnd())  msg = "Reverse playback direction.";
	else if (h == m_player_grid.GetSafeHwnd())     msg = "Back: exit the player and return to the static grid of all frames.";
	else if (h == m_player_native.GetSafeHwnd())   msg = "Display at native resolution (no scaling).";
	else if (h == m_player_screenshot.GetSafeHwnd()) msg = "Screenshot: Save As... (PNG/TGA/PCX, Ctrl+Shift+S) or Copy to Clipboard (format: Theme > Image > Clipboard Format).";
	else if (h == m_player_slider.GetSafeHwnd())   msg = "Scrub timeline. Pauses playback while dragging.";
	else if (h == m_player_fps_edit.GetSafeHwnd()) msg = "Playback speed in frames per second.";
	else if (h == m_player_fps_spin.GetSafeHwnd()) msg = "Playback speed in frames per second.";
	else if (h == m_player_shadows.GetSafeHwnd())  msg = "Cycle: Off / Shadows RA2-TS (palette idx 0 = transparent) / Shadows TD-RA1 (palette idx 4 = transparent). Composites frame[i + cf/2] as shadow; halves the timeline.";
	else if (h == m_player_bg.GetSafeHwnd())       msg = "Cycle background for transparent pixels: palette color 0 / alpha checker / pane color.";
	else if (h == m_player_side_custom.GetSafeHwnd()) msg = "Custom side color: opens picker. Click again to clear.";
	else if (h == m_player_iso_grid.GetSafeHwnd()) msg = "Overlay isometric tile grid: click cycles off / TS 48px / RA2 60px. Drawn before scaling.";
	else if (h == m_vxl_hva_load.GetSafeHwnd())    msg = "Load an HVA animation. Lists matching HVAs from the source MIX.";
	else if (h == m_vxl_hva_loop.GetSafeHwnd())    msg = "Loop HVA animation. When off, playback stops at the last keyframe.";
	else if (h == m_vxl_vpl_load.GetSafeHwnd())    msg = "Load a VPL palette mapper for engine-faithful VXL shading. Auto-loads voxels.vpl on file open.";
	else if (h == m_player_turntable.GetSafeHwnd()) msg = is_vxl_view()
		? "Record an animated turntable: rotate the voxel 360\xb0 (and/or play the HVA), save as GIF or PNG sequence. Honors current SS, lighting, VPL."
		: "Record the SHP/WSA animation frames as GIF or PNG sequence. Honors current side color, BG, shadows, palette.";
	else if (h == m_vxl_side_custom.GetSafeHwnd()) msg = "Custom VXL side color: opens picker. Click again to clear.";
	else
	{
		for (int i = 0; i < 8; i++)
		{
			if (h == m_player_side[i].GetSafeHwnd()) { msg = "Recolor palette indices 16-31 (Westwood remap range). Click active swatch to clear."; break; }
			if (h == m_vxl_side[i].GetSafeHwnd())    { msg = "Recolor VXL palette indices 16-31. Click active swatch to clear."; break; }
		}
	}

	if (CFrameWnd* mf = GetTopLevelFrame())
	{
		if (msg && msg != m_last_hover_help)
		{
			mf->SetMessageText(msg);
			m_last_hover_help = msg;
		}
		else if (!msg && m_last_hover_help)
		{
			mf->SetMessageText(AFX_IDS_IDLEMESSAGE);
			m_last_hover_help = nullptr;
		}
	}
}

void CXCCFileView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
	bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
	UINT action = 0;
	if (keybinds::match_view(keybinds::scope_file_view, nChar, ctrl, shift, alt, action))
	{
		switch (action)
		{
		case keybinds::vact_alpha_toggle:
			m_show_alpha_only = !m_show_alpha_only;
			Invalidate();
			return;
		case keybinds::vact_player_toggle:
			if (is_playable_file())
			{
				if (m_player_mode)
					player_exit();
				else
					player_enter();
			}
			return;
		case keybinds::vact_player_prev:
			if (m_player_mode)
				player_set_frame(m_player_frame - 1);
			return;
		case keybinds::vact_player_next:
			if (m_player_mode)
				player_set_frame(m_player_frame + 1);
			return;
		case keybinds::vact_player_space:
			if (m_player_mode && !is_vxl_view())
				OnPlayerPlay();
			return;
		case keybinds::vact_zoom_100:
			if (m_player_mode)
			{
				m_player_zoom_pct = 100;
				m_player_pan_x = m_player_pan_y = 0;
				Invalidate(FALSE);
			}
			else if (m_zoomable_file && m_zoom_pct != 100)
			{
				m_zoom_pct = 100;
				m_text_cache_valid = false;
				Invalidate();
			}
			return;
		case keybinds::vact_zoom_in:
			do_zoom_step(+1);
			return;
		case keybinds::vact_zoom_out:
			do_zoom_step(-1);
			return;
		}
	}
	CScrollView::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CXCCFileView::OnEraseBkgnd(CDC* pDC)
{
	GetClientRect(clientRect);
	if (m_player_mode)
	{
		const int band = player_band_h();
		// Image area
		CRect r_img = clientRect;
		r_img.bottom -= band;
		if (r_img.bottom > r_img.top)
			pDC->FillSolidRect(r_img, theme::bg());
		// Controls area: fill once with bg (controls redraw themselves on top of this).
		CRect r_ctrl = clientRect;
		r_ctrl.top = r_ctrl.bottom - band;
		if (r_ctrl.top < clientRect.bottom)
			pDC->FillSolidRect(r_ctrl, theme::bg());
	}
	else
	{
		pDC->FillSolidRect(clientRect, theme::bg());
	}
	return TRUE;
}

// Apply a +/- 25% zoom step. Handles both player-mode zoom and the
// CScrollView image-grid zoom, depending on what the file view is currently
// displaying. Centralized so wheel-tick and a key-bound zoom hit the same
// state machine.
void CXCCFileView::do_zoom_step(int sign, CPoint anchor)
{
	if (m_player_mode)
	{
		CRect rc; GetClientRect(&rc);
		const int avail_w = rc.Width();
		const int avail_h = rc.Height() - player_band_h();
		int cur = m_player_zoom_pct;
		if (cur <= 0)
		{
			if (m_player_native_size)
				cur = 100;
			else
			{
				if (m_player_cx > 0 && m_player_cy > 0 && avail_w > 0 && avail_h > 0)
				{
					int sx = avail_w * 100 / m_player_cx;
					int sy = avail_h * 100 / m_player_cy;
					cur = std::min(sx, sy);
				}
				else
					cur = 100;
			}
			if (cur < 25) cur = 25;
			if (cur > 1600) cur = 1600;
		}
		int next = cur + (sign > 0 ? 25 : -25);
		if (next < 25) next = 25;
		if (next > 1600) next = 1600;
		if (next != m_player_zoom_pct)
		{
			// Zoom-to-anchor: keep the world point currently under `anchor`
			// fixed across the zoom step. Default anchor (sentinel -1,-1)
			// picks the canvas center, matching keyboard zoom semantics.
			//
			// Image origin x_d = (avail_w - cx_d)/2 + pan_x, where cur or
			// next sets cx_d via m_player_cx * pct / 100. We solve for the
			// new pan that puts the same source-pixel position back under
			// the anchor canvas pixel after the zoom.
			if (anchor.x < 0 || anchor.x >= avail_w) anchor.x = avail_w / 2;
			if (anchor.y < 0 || anchor.y >= avail_h) anchor.y = avail_h / 2;
			if (m_player_cx > 0 && m_player_cy > 0 && cur > 0)
			{
				const int cx_d_old = m_player_cx * cur  / 100;
				const int cy_d_old = m_player_cy * cur  / 100;
				const int cx_d_new = m_player_cx * next / 100;
				const int cy_d_new = m_player_cy * next / 100;
				const int x_d_old = (avail_w - cx_d_old) / 2 +
					((cx_d_old > avail_w) ? m_player_pan_x : 0);
				const int y_d_old = (avail_h - cy_d_old) / 2 +
					((cy_d_old > avail_h) ? m_player_pan_y : 0);
				// world coords of the pixel under the anchor (float for
				// rounding stability, but int math would round to the same
				// pixel in the common case).
				const double wx = (anchor.x - x_d_old) * (next / static_cast<double>(cur));
				const double wy = (anchor.y - y_d_old) * (next / static_cast<double>(cur));
				const int x_d_new_target = anchor.x - static_cast<int>(wx + 0.5);
				const int y_d_new_target = anchor.y - static_cast<int>(wy + 0.5);
				m_player_pan_x = (cx_d_new > avail_w) ? (x_d_new_target - (avail_w - cx_d_new) / 2) : 0;
				m_player_pan_y = (cy_d_new > avail_h) ? (y_d_new_target - (avail_h - cy_d_new) / 2) : 0;
			}
			else
			{
				m_player_pan_x = m_player_pan_y = 0;
			}
			m_player_zoom_pct = next;
			Invalidate(FALSE);
		}
		return;
	}
	if (m_zoomable_file)
	{
		int next = m_zoom_pct + (sign > 0 ? 25 : -25);
		if (next < 25) next = 25;
		if (next > 1600) next = 1600;
		if (next != m_zoom_pct)
		{
			m_zoom_pct = next;
			m_text_cache_valid = false;
			Invalidate();
		}
	}
}

int CXCCFileView::player_effective_zoom_pct() const
{
	if (!m_player_mode || m_player_cx <= 0 || m_player_cy <= 0)
		return 100;
	if (m_player_zoom_pct > 0)
		return m_player_zoom_pct;
	if (m_player_native_size)
		return 100;
	CRect rc; const_cast<CXCCFileView*>(this)->GetClientRect(&rc);
	int avail_w = rc.Width();
	int avail_h = rc.Height() - player_band_h();
	if (avail_w < 1 || avail_h < 1)
		return 100;
	int sx = avail_w * 100 / m_player_cx;
	int sy = avail_h * 100 / m_player_cy;
	int s_pct = std::min(sx, sy);
	if (s_pct < 1) s_pct = 1;
	if (s_pct > 1600) s_pct = 1600;
	return s_pct;
}

BOOL CXCCFileView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CPoint position = GetScrollPosition();
	bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
	bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

	// Wheel direction is encoded as a synthetic mouse button.
	BYTE btn = (zDelta > 0) ? keybinds::mb_wheel_up : keybinds::mb_wheel_down;
	UINT action = 0;
	if (keybinds::match_mouse(keybinds::scope_file_view, btn, ctrl, shift, alt, action))
	{
		// Wheel zoom anchors on the cursor so the image point under the
		// mouse stays fixed across the zoom step (image-viewer convention).
		// pt arrives in screen coords per Win32 WM_MOUSEWHEEL.
		CPoint anchor = pt;
		ScreenToClient(&anchor);
		if (action == keybinds::vact_zoom_in)  { do_zoom_step(+1, anchor); return TRUE; }
		if (action == keybinds::vact_zoom_out) { do_zoom_step(-1, anchor); return TRUE; }
		if (action == keybinds::vact_zoom_100)
		{
			if (m_player_mode)
				m_player_zoom_pct = 100;
			else if (m_zoomable_file)
				m_zoom_pct = 100;
			Invalidate();
			return TRUE;
		}
	}

	// No matching mouse binding: fall through to scroll. Shift scrolls
	// horizontally, otherwise vertically. In player mode the pane has no
	// scrollable content (image is fit/zoomed in place), so swallow the
	// wheel instead of moving the view origin.
	if (m_player_mode)
		return TRUE;
	if (shift)
	{
		ScrollToPosition(CPoint(position.x - zDelta, position.y));
		return FALSE;
	}
	ScrollToPosition(CPoint(position.x, position.y - zDelta));
	return FALSE;
}

void CXCCFileView::OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (m_player_mode)
		return;
	CPoint position = GetScrollPosition();
	SHORT shiftState = GetAsyncKeyState(VK_SHIFT);

	if (shiftState)
	{
		ScrollToPosition(CPoint(position.x, position.y + zDelta));
		return;
	}
	ScrollToPosition(CPoint(position.x + zDelta, position.y));
	return;
}

void CXCCFileView::OnInitialUpdate()
{
	CScrollView::OnInitialUpdate();
	// Seed the scroll size before any WM_PAINT reaches CScrollView::OnPrepareDC.
	// Without this, debug MFC asserts in viewscrl.cpp ("must call
	// SetScrollSizes() ... before painting scroll view") because the first
	// paint can arrive before OnDraw has assigned a per-format size.
	// (1,1) is a placeholder; OnDraw / player_enter overwrite it with the
	// real extent for the current file.
	SetScrollSizes(MM_TEXT, CSize(1, 1));
	test_brush.CreateSolidBrush(m_colour);
	//m_font.CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
	m_font.CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Lucida Console");
	//m_font.CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
	//m_font.CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, ""); //default font, but if it isn't monospace it sucks
	// The "Load PAL..." button now lives on the frame's status bar (see
	// CMainFrame::show_load_pal_button) so it never scrolls with this view.
	// Visibility is still driven from here via load_pal_btn_update_visibility().
}

bool CXCCFileView::is_paletted_file() const
{
	switch (m_ft)
	{
	case ft_shp:
	case ft_shp_ts:
	case ft_shp_dune2:
	case ft_wsa:
	case ft_wsa_dune2:
	case ft_tmp_ra:
	case ft_tmp_ts:
	case ft_pcx:
	case ft_cps:
	case ft_vxl:
		return true;
	default:
		return false;
	}
}

void CXCCFileView::load_pal_btn_update_visibility()
{
	// The button is hosted on the frame's status bar now; just tell the frame
	// whether the current file warrants it. The view still owns this decision
	// because only it knows the open file's type.
	const bool show = m_is_open && is_paletted_file();
	if (CMainFrame* mf = GetMainFrame())
		mf->show_load_pal_button(show);
}

void CXCCFileView::draw_image8(const byte* s, int cx_s, int cy_s, CDC* pDC, int x_d)
{
	int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
	int cx_d_pre = cx_s * zoom_pre / 100;
	int cy_d_pre = cy_s * zoom_pre / 100;
	// Skip the entire decode + DIB + StretchBlt path when this frame is
	// outside the current clip box. Cheap RectVisible test saves the per-
	// pixel palette walk for stacked SHP grids with many frames offscreen.
	CRect r_test(x_d, m_y, x_d + cx_d_pre, m_y + cy_d_pre);
	if (!pDC->RectVisible(&r_test))
	{
		m_x = max(m_x, x_d + cx_d_pre);
		if (zoom_pre != 100)
			m_y += (cy_d_pre - cy_s);
		return;
	}
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	const bool tr = theme::shp_transparency();
	const COLORREF ck_a = theme::checker_a();
	const COLORREF ck_b = theme::checker_b();
	// COLORREF is 0x00BBGGRR; the DIB stores 0x00RRGGBB BGRA. Swap.
	const DWORD ck_a_d = (GetRValue(ck_a) << 16) | (GetGValue(ck_a) << 8) | GetBValue(ck_a);
	const DWORD ck_b_d = (GetRValue(ck_b) << 16) | (GetGValue(ck_b) << 8) | GetBValue(ck_b);
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			byte idx = s[x + cx_s * y];
			if (tr && idx == 0)
				mp_dib[x + cx_s * y] = (((x >> 3) ^ (y >> 3)) & 1) ? ck_b_d : ck_a_d;
			else
				mp_dib[x + cx_s * y] = m_color_table[idx];
		}
	}
	int zoom = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
	int cx_d = cx_s * zoom / 100;
	int cy_d = cy_s * zoom / 100;
	theme::stretch_image(pDC, x_d, m_y, cx_d, cy_d, &mem_dc, mh_dib, mp_dib, cx_s, cy_s);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, x_d + cx_d);
	if (zoom != 100)
		m_y += (cy_d - cy_s);
}

void CXCCFileView::draw_image24(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
	int cx_d_pre = cx_s * zoom_pre / 100;
	int cy_d_pre = cy_s * zoom_pre / 100;
	CRect r_test(offset, m_y, offset + cx_d_pre, m_y + cy_d_pre);
	if (!pDC->RectVisible(&r_test))
	{
		m_x = max(m_x, offset + cx_d_pre);
		if (zoom_pre != 100)
			m_y += (cy_d_pre - cy_s);
		return;
	}
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = *s++;
			v.g = *s++;
			v.b = *s++;
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	int zoom = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
	int cx_d = cx_s * zoom / 100;
	int cy_d = cy_s * zoom / 100;
	theme::stretch_image(pDC, offset, m_y, cx_d, cy_d, &mem_dc, mh_dib, mp_dib, cx_s, cy_s);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_d);
	if (zoom != 100)
		m_y += (cy_d - cy_s);
}

void CXCCFileView::draw_image32(const byte* s, int cx_s, int cy_s, CDC* pDC, bool bgra)
{
	int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
	int cx_d_pre = cx_s * zoom_pre / 100;
	int cy_d_pre = cy_s * zoom_pre / 100;
	CRect r_test(offset, m_y, offset + cx_d_pre, m_y + cy_d_pre);
	if (!pDC->RectVisible(&r_test))
	{
		m_x = max(m_x, offset + cx_d_pre);
		if (zoom_pre != 100)
			m_y += (cy_d_pre - cy_s);
		return;
	}
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);

	// Composite RGBA over an 8x8 checkerboard so transparency is visible.
	// When m_show_alpha_only is on, ignore RGB and render alpha as grayscale.
	const COLORREF ck_a = theme::checker_a();
	const COLORREF ck_b = theme::checker_b();
	const byte ck_a_r = GetRValue(ck_a), ck_a_g = GetGValue(ck_a), ck_a_b = GetBValue(ck_a);
	const byte ck_b_r = GetRValue(ck_b), ck_b_g = GetGValue(ck_b), ck_b_b = GetBValue(ck_b);
	const bool alpha_only = m_show_alpha_only;

	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			byte b0 = *s++;
			byte b1 = *s++;
			byte b2 = *s++;
			byte sa = *s++;
			if (alpha_only)
			{
				v.r = sa;
				v.g = sa;
				v.b = sa;
				v.a = 0;
				mp_dib[x + cx_s * y] = v.v;
				continue;
			}
			byte sr = bgra ? b2 : b0;
			byte sg = b1;
			byte sb = bgra ? b0 : b2;
			bool ck = ((x >> 3) ^ (y >> 3)) & 1;
			byte br = ck ? ck_b_r : ck_a_r;
			byte bg = ck ? ck_b_g : ck_a_g;
			byte bb = ck ? ck_b_b : ck_a_b;
			// Standard alpha-over: out = src*a + bg*(1-a). +127 for rounding.
			v.r = (sr * sa + br * (255 - sa) + 127) / 255;
			v.g = (sg * sa + bg * (255 - sa) + 127) / 255;
			v.b = (sb * sa + bb * (255 - sa) + 127) / 255;
			v.a = 0;
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	int zoom = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
	int cx_d = cx_s * zoom / 100;
	int cy_d = cy_s * zoom / 100;
	theme::stretch_image(pDC, offset, m_y, cx_d, cy_d, &mem_dc, mh_dib, mp_dib, cx_s, cy_s);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_d);
	if (zoom != 100)
		m_y += (cy_d - cy_s);
}

void CXCCFileView::draw_image48(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	CRect r_test(offset, m_y, offset + cx_s, m_y + cy_s);
	if (!pDC->RectVisible(&r_test))
	{
		m_x = max(m_x, offset + cx_s);
		return;
	}
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	auto r = reinterpret_cast<const unsigned short*>(s);
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = linear2sRGB(*r++);
			v.g = linear2sRGB(*r++);
			v.b = linear2sRGB(*r++);
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	pDC->BitBlt(offset, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_s);
}

void CXCCFileView::draw_image64(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	CRect r_test(offset, m_y, offset + cx_s, m_y + cy_s);
	if (!pDC->RectVisible(&r_test))
	{
		m_x = max(m_x, offset + cx_s);
		return;
	}
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	auto r = reinterpret_cast<const unsigned short*>(s);
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = linear2sRGB(*r++);
			v.g = linear2sRGB(*r++);
			v.b = linear2sRGB(*r++);
			v.a = linear2sRGB(*r++);
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	pDC->BitBlt(offset, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_s);
}

static CMainFrame* GetMainFrame()
{
	return reinterpret_cast<CMainFrame*>(AfxGetMainWnd());
}

const t_palette_entry* CXCCFileView::get_default_palette()
{
	const t_palette_entry* p = GetMainFrame()->get_pal_data();
	if (p)
		return p;
	if (m_palette)
		return m_palette;
	return GetMainFrame()->get_game_palette(m_game);
}

void CXCCFileView::load_color_table(const t_palette palette, bool convert_palette)
{
	t_palette p;
	if (!palette)
	{
		convert_palette = true;
		palette = get_default_palette();
	}
	memcpy(p, palette, sizeof(t_palette));
	if (convert_palette)
		convert_palette_18_to_24(p);
	t_palette32bgr_entry* color_table = reinterpret_cast<t_palette32bgr_entry*>(m_color_table);
	for (unsigned short i = 0; i < 256; i++)
	{
		color_table[i].r = p[i].r;
		color_table[i].g = p[i].g;
		color_table[i].b = p[i].b;
	}
	// Palette change invalidates the player BGRA cache: every cached frame
	// was built with the previous color table. Re-prefill if currently in
	// SHP/WSA player mode so the next animation tick is a cache hit (this
	// path is also called during non-player file opens, where the prefill
	// is a no-op because m_player_mode is false).
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
}

static string t2s(const string& v)
{
	string r;
	for (int i = 0; i < v.length(); i++)
	{
		char c = v[i];
		if (c == '\t')
		{
			do
				r += ' ';
			while (r.length() & 3);
		}
		else
			r += c;
	}
	return r;
}

void CXCCFileView::draw_info(string n, string d)
{
	if (!m_text_cache_valid)
	{
		n = t2s(n);
		d = t2s(d);
		t_text_cache_entry e;

		CSize size;
		size.SetSize(1, 1);

		e.text_extent = CRect(CPoint(offset, m_y + offset), size);
		e.t = n;
		m_text_cache.push_back(e);
		if (!d.empty())
		{
			e.text_extent = CRect(CPoint(offset * 32, m_y + offset), size);
			e.t = d;
			m_text_cache.push_back(e);
			m_x = max<int>(m_x, (offset * 32) + e.text_extent.right);
		}
		else
			m_x = max<int>(m_x, e.text_extent.right);
	}
	m_y += m_y_inc;
}


void CXCCFileView::OnDisable(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(false);
}

void fnt_decode(const byte* r, byte* w, int cx, int cy)
{
	while (cy--)
	{
		for (int x = 1; x < cx; x += 2)
		{
			*w++ = *r & 0xf;
			*w++ = *r++ >> 4;
		}
		if (cx & 1)
			*w++ = *r++ & 0xf;
	}
}

void fnt_adjust(byte* d, int size)
{
	while (size--)
	{
		byte& v = *d++;
		v = v ? 0 : 0xff;
	}
}

// Side-color presets (matches the Advanced SHP Editor preview palette).
// Order: Red, Gold, Green, Blue, Light Red, Orange, Teal, Pink.
static const COLORREF k_side_colors[8] = {
	RGB(0xbf, 0x00, 0x00),
	RGB(0xe3, 0xa5, 0x02),
	RGB(0x48, 0xbb, 0x78),
	RGB(0x22, 0x55, 0xff),
	RGB(0xf5, 0x65, 0x65),
	RGB(0xed, 0x89, 0x36),
	RGB(0x4f, 0xd1, 0xc5),
	RGB(0xd5, 0x3f, 0x8c),
};

// --------------------------------------------------------------------------
// HVA keyframe interpolation
//
// HVA stores N keyframes. Vengi's scene graph spaces them at frame indices
// 0, 6, 12, ... and interpolates linearly between consecutive keyframes
// (slerp on rotation, lerp on translation). c_HVA_INTER is the per-gap step
// count we expose so the timeline slider gives meaningful sub-keyframe
// scrubbing — the value isn't observable elsewhere so 12 (matches Vengi's
// implicit ~6fps * one-second feel scaled up) is reasonable.
// --------------------------------------------------------------------------
static const int c_HVA_INTER = 12;

// Decompose a 3x3 rotation matrix into a unit quaternion (Shoemake's
// trace-based method). Tolerates mild non-orthogonality from HVA mats by
// renormalizing rows beforehand.
static void mat3_to_quat(const float m[3][3], float q[4])
{
	const float tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0.0f) {
		float s = std::sqrt(tr + 1.0f) * 2.0f;
		q[3] = 0.25f * s;
		q[0] = (m[2][1] - m[1][2]) / s;
		q[1] = (m[0][2] - m[2][0]) / s;
		q[2] = (m[1][0] - m[0][1]) / s;
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
		q[3] = (m[2][1] - m[1][2]) / s;
		q[0] = 0.25f * s;
		q[1] = (m[0][1] + m[1][0]) / s;
		q[2] = (m[0][2] + m[2][0]) / s;
	} else if (m[1][1] > m[2][2]) {
		float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
		q[3] = (m[0][2] - m[2][0]) / s;
		q[0] = (m[0][1] + m[1][0]) / s;
		q[1] = 0.25f * s;
		q[2] = (m[1][2] + m[2][1]) / s;
	} else {
		float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
		q[3] = (m[1][0] - m[0][1]) / s;
		q[0] = (m[0][2] + m[2][0]) / s;
		q[1] = (m[1][2] + m[2][1]) / s;
		q[2] = 0.25f * s;
	}
}

static void quat_to_mat3(const float q[4], float m[3][3])
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	m[0][0] = 1 - 2*(y*y + z*z); m[0][1] = 2*(x*y - z*w);     m[0][2] = 2*(x*z + y*w);
	m[1][0] = 2*(x*y + z*w);     m[1][1] = 1 - 2*(x*x + z*z); m[1][2] = 2*(y*z - x*w);
	m[2][0] = 2*(x*z - y*w);     m[2][1] = 2*(y*z + x*w);     m[2][2] = 1 - 2*(x*x + y*y);
}

static void quat_slerp(const float a[4], const float b_in[4], float t, float out[4])
{
	float b[4] = { b_in[0], b_in[1], b_in[2], b_in[3] };
	float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
	if (dot < 0.0f) {
		b[0] = -b[0]; b[1] = -b[1]; b[2] = -b[2]; b[3] = -b[3];
		dot = -dot;
	}
	float s0, s1;
	if (dot > 0.9995f) {
		// Nearly parallel: fall back to lerp + normalize.
		s0 = 1.0f - t;
		s1 = t;
	} else {
		const float th = std::acos(dot);
		const float sth = std::sin(th);
		s0 = std::sin((1.0f - t) * th) / sth;
		s1 = std::sin(t * th) / sth;
	}
	out[0] = s0*a[0] + s1*b[0];
	out[1] = s0*a[1] + s1*b[1];
	out[2] = s0*a[2] + s1*b[2];
	out[3] = s0*a[3] + s1*b[3];
	const float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
	if (len > 1e-6f) { out[0]/=len; out[1]/=len; out[2]/=len; out[3]/=len; }
}

// Interpolate between two HVA keyframe matrices into a 3x4 row-major output.
// Rotation goes through quaternion slerp so spin animations sweep smoothly
// between keyframes (linear matrix lerp would produce non-rigid shearing).
static void hva_interp(const float* a, const float* b, float t, float out[3][4])
{
	// Pull 3x3 rotations out of each row-major 3x4.
	float ra[3][3] = {
		{ a[0], a[1], a[2]  },
		{ a[4], a[5], a[6]  },
		{ a[8], a[9], a[10] }
	};
	float rb[3][3] = {
		{ b[0], b[1], b[2]  },
		{ b[4], b[5], b[6]  },
		{ b[8], b[9], b[10] }
	};
	float qa[4], qb[4], qr[4], rr[3][3];
	mat3_to_quat(ra, qa);
	mat3_to_quat(rb, qb);
	quat_slerp(qa, qb, t, qr);
	quat_to_mat3(qr, rr);
	// Translation: simple lerp.
	const float tx = a[3]  * (1.0f - t) + b[3]  * t;
	const float ty = a[7]  * (1.0f - t) + b[7]  * t;
	const float tz = a[11] * (1.0f - t) + b[11] * t;
	out[0][0] = rr[0][0]; out[0][1] = rr[0][1]; out[0][2] = rr[0][2]; out[0][3] = tx;
	out[1][0] = rr[1][0]; out[1][1] = rr[1][1]; out[1][2] = rr[1][2]; out[1][3] = ty;
	out[2][0] = rr[2][0]; out[2][1] = rr[2][1]; out[2][2] = rr[2][2]; out[2][3] = tz;
}


struct t_vector
{
	double x;
	double y;
	double z;
};

t_vector rotate_x(t_vector v, double a)
{
	double l = sqrt(v.y * v.y + v.z * v.z);
	double d_a = atan2(v.y, v.z) + a;
	t_vector r;
	r.x = v.x;
	r.y = l * sin(d_a);
	r.z = l * cos(d_a);
	return r;
}

t_vector rotate_y(t_vector v, double a)
{
	double l = sqrt(v.x * v.x + v.z * v.z);
	double d_a = atan2(v.x, v.z) + a;
	t_vector r;
	r.x = l * sin(d_a);
	r.y = v.y;
	r.z = l * cos(d_a);
	return r;
}

const char* dump_four_cc(DWORD four_cc)
{
	static char r[5];
	r[0] = four_cc & 0xff;
	r[1] = four_cc >> 8 & 0xff;
	r[2] = four_cc >> 16 & 0xff;
	r[3] = four_cc >> 24 & 0xff;
	r[4] = 0;
	return r;
}

int get_size(unsigned int v)
{
	int r = 0;
	while (v)
	{
		if (v & 1)
			r++;
		v >>= 1;
	}
	return r;
}

void CXCCFileView::OnDraw(CDC* pDC)
{
	//pDC->SetTextColor(RGB(249, 245, 215));

	const char* b2a[] = {"no", "yes"};
	pDC->SelectObject(&m_font);

	pDC->SetTextColor(theme::text());
	pDC->SetBkColor(theme::bg());
	pDC->SetBkMode(OPAQUE);

	//pDC->SetBkColor(m_colour);

	if (m_is_open && m_player_mode)
	{
		player_draw(pDC);
		return;
	}
	if (m_is_open)
	{
		TEXTMETRIC tm;
		pDC->GetTextMetrics(&tm);
		m_dc = pDC;
		m_x = 0;
		m_y = 0;
		m_y_inc = tm.tmHeight;

		draw_info("ID:", nh(8, m_id));
		draw_info("Size:", n(m_size));
		draw_info("Type:", ft_name[m_ft]);
		bool show_binary = false;
		switch (m_ft)
		{
		case ft_aud:
			{
				Caud_file f;
				f.load(m_data);
				draw_info("Audio:", n(f.get_samplerate()) + " hz, " + n(f.get_cb_sample() << 3) + " bit, " + (f.get_c_channels() == 1 ? "Mono" : "Stereo"));
				draw_info("Samples:", n(f.get_c_samples()));
				draw_info("Compression:", nh(2, f.header().compression));
				break;
			}
		case ft_big:
			{
				Cbig_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				draw_info("Files:", n(c_files));
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					string name = f.get_name(i);
					draw_info(nwzl(4, i) + " - " + swsl(11, theme::format_size(f.get_size(name))) + ' ' + name, "");
				}
				break;
			}
		case ft_csf:
		{
			Ccsf_file_rd f;
			f.load(m_data, m_size);
			const int c_strs = f.header().count1;
			auto& c_strmaps = f.get_map();
			draw_info("Strings:", n(c_strs));
			m_y += m_y_inc;
			draw_info("Name", "Value\t\tExtra Value");
			for (auto i : c_strmaps)
			{
				draw_info(i.first, Ccsf_file::convert2string(i.second.value) + "\t\t" + i.second.extra_value);
			}
			break;
		}
		case ft_cps:
			{
				Ccps_file f;
				f.load(m_data);
				draw_info("Paletted:", f.palette() ? "Yes" : "No");
				m_y += m_y_inc;
				load_color_table(f.palette(), true);
				Cvirtual_image image = f.vimage();
				draw_image8(image.image(), image.cx(), image.cy(), pDC, offset);
				m_y += 200 + m_y_inc;
				break;
			}
		case ft_dds:
			{
				Cdds_file f;
				f.load(m_data);
				const DDSURFACEDESC2& ddsd = f.ddsd();
				if (ddsd.dwFlags & (DDSD_WIDTH | DDSD_HEIGHT) == DDSD_WIDTH | DDSD_HEIGHT)
					draw_info("Size: ",  n(ddsd.dwWidth) + " x " + n(ddsd.dwHeight));
				if (ddsd.dwFlags & DDSD_PITCH)
					draw_info("Pitch: ", n(ddsd.lPitch));
				if (ddsd.dwFlags & DDSD_LINEARSIZE)
					draw_info("Linear size: ", n(ddsd.dwLinearSize));
				if (ddsd.dwFlags & DDSD_DEPTH)
					draw_info("Depth: ", n(ddsd.dwDepth));
				if (ddsd.dwFlags & DDSD_MIPMAPCOUNT)
					draw_info("Mipmaps: ", n(ddsd.dwMipMapCount));
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
					draw_info("Pixel Format: ", dump_four_cc(ddsd.ddpfPixelFormat.dwFourCC));
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_RGB)
					draw_info("Pixel Format: ", n(ddsd.ddpfPixelFormat.dwRGBBitCount) +
						" bits (" + nwzl(4, 1000 * get_size(ddsd.ddpfPixelFormat.dwRGBAlphaBitMask) + 100 * get_size(ddsd.ddpfPixelFormat.dwRBitMask) + 10
							* get_size(ddsd.ddpfPixelFormat.dwGBitMask) + get_size(ddsd.ddpfPixelFormat.dwBBitMask)) + ')');
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
				{
					Cvirtual_image image = f.vimage();
					if (image.image())
					{
						m_y += m_y_inc;
						// Decoded DDS data is 32-bit RGBA; draw_image32 will composite
						// alpha against a checkerboard so transparency is visible.
						draw_image32(image.image(), f.cx(), f.cy(), pDC, /*bgra=*/false);
						m_y += f.cy() + m_y_inc;
					}
				}
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_RGB && ddsd.ddpfPixelFormat.dwRGBBitCount == 24)
				{
					m_y += m_y_inc;
					draw_image24(f.image(), f.cx(), f.cy(), pDC);
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		case ft_fnt:
			{
				Cfnt_file f;
				f.load(m_data);
				const int c_chars = f.get_c_chars();
				const int cy_test = f.get_cy();
				const t_fnt_header& header = f.header();
				draw_info("Characters:", n(c_chars));
				draw_info("Size:", n(f.get_cmax_x()) + " x " + n(cy_test));
				m_y += m_y_inc;
				byte* d = new byte[f.get_cmax_x() * f.get_cy()];
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < c_chars; i++)
				{
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					if (!cx || !cy)
						continue;
					fnt_decode(f.get_image(i), d, cx, cy);
					fnt_adjust(d, cx * cy);
					draw_image8(d, cx, cy, pDC, offset);
					draw_info("", nwzl(3, i) + " - " + n(cx) + " x " + n(cy) + " at " + n(f.get_image(i) - f.get_data()));
					m_y += cy;	//add y height to make any character always be visible no matter text height
				}
				delete[] d;
				break;
			}
		case ft_hva:
			{
				Chva_file f;
				f.load(m_data, m_size);
				draw_info("Frames:", n(f.get_c_frames()));
				draw_info("Sections:", n(f.get_c_sections()));
				break;
			}
		case ft_bmp:
		case ft_jpeg:
		case ft_png:
			{
				Cvirtual_image image;
				if (!image.load(m_data))
				{
					const int cx = image.cx();
					const int cy = image.cy();
					draw_info("Bits/Pixel:", n(8 * image.cb_pixel()));
					draw_info("Size:", n(cx) + " x " + n(cy));
					m_y += m_y_inc;
					switch (image.cb_pixel())
					{
					case 1:
						load_color_table(image.palette(), false);
						draw_image8(image.image(), cx, cy, pDC, offset);
						break;
					case 3:
						draw_image24(image.image(), cx, cy, pDC);
						break;
					case 4:
						draw_image32(image.image(), cx, cy, pDC);
						break;
					case 6:
						draw_image48(image.image(), cx, cy, pDC);
						break;
					case 8:
						draw_image64(image.image(), cx, cy, pDC);
						break;
					default:
						break;
					}
					m_y += cy + m_y_inc;
				}
				break;
			}
		case ft_map_td:
			{
				Cvirtual_tfile tf;
				tf.load_data(m_data);
				Cmap_td_ini_reader ir;
				while (!tf.eof())
				{
					ir.process_line(tf.read_line());
				}
				const Cmap_td_ini_reader::t_map_data& md = ir.get_map_data();
				draw_info("Name:", ir.get_basic_data().name);
				draw_info("Size:", n(md.cx) + " x " + n(md.cy));
				draw_info("Theater:", ir.get_map_data().theater);
				m_y += m_y_inc;
				tf.load_data(m_data);
				while (!tf.eof())
					draw_info(tf.read_line(), "");
				break;
			}
		case ft_map_ra:
			{
				Cvirtual_tfile tf;
				tf.load_data(m_data);
				Cmap_ra_ini_reader ir;
				while (!tf.eof())
				{
					ir.process_line(tf.read_line());
				}
				const Cmap_ra_ini_reader::t_map_data& md = ir.get_map_data();
				draw_info("Name:", ir.get_basic_data().name);
				draw_info("Size:", n(md.cx) + " x " + n(md.cy));
				draw_info("Theater:", ir.get_map_data().theater);
				m_y += m_y_inc;
				tf.load_data(m_data);
				while (!tf.eof())
					draw_info(tf.read_line(), "");
				break;
			}
		case ft_map_ts:
			{
				Cvirtual_tfile tf;
				tf.load_data(m_data);
				Cmap_ts_ini_reader ir;
				while (!tf.eof())
				{
					ir.process_line(tf.read_line());
				}
				const Cmap_ts_ini_reader::t_map_data& md = ir.get_map_data();
				const Cmap_ts_ini_reader::t_preview_data& pd = ir.get_preview_data();
				const string& ppd = ir.get_preview_pack_data();
				draw_info("Name:", ir.get_basic_data().name);
				draw_info("Size:", n(md.size_right) + " x " + n(md.size_bottom));
				draw_info("Theater:", ir.get_map_data().theater);
				draw_info("Max Players:", n(ir.max_players()));

				if (pd.cx && pd.cy && ppd != "BIACcgAEwBtAMnRABAAaQCSANMAVQASAAnIABMAbQDJ0QAQAGkAkgDTAFUAEgAJyAATAG0yAsAIAXQ5PDQ5PDQ6JQATAEE6PDQ4PDI4JgBTAFEAkgAJyAATAG0AydEAEABpAJIA0wBVA")
				{
					m_y += m_y_inc;
					Cvirtual_binary s = decode64(ppd);
					Cvirtual_binary image;

					if ((pd.cx * pd.cy) / 33 > ppd.size())	//test to not try to render weirdly small (corrupted) preview images
					{
						m_y += m_y_inc;
						tf.load_data(m_data);
						while (!tf.eof())
							draw_info(tf.read_line(), "");
						break;
					}
					decode5(s.data(), image.write_start(pd.cx * pd.cy * 3), s.size(), 5);
					draw_image24(image.data(), pd.cx, pd.cy, pDC);
					m_y += m_y_inc + pd.cy;
					tf.load_data(m_data);
					while (!tf.eof())
						draw_info(tf.read_line(), "");
				}
				else
				{
					m_y += m_y_inc;
					tf.load_data(m_data);
					while (!tf.eof())
						draw_info(tf.read_line(), "");
				}
				break;
			}
		case ft_mix:
			{
				Cmix_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				const t_game game = f.get_game();
				draw_info("Files:", n(c_files));
				draw_info("Checksum:", f.has_checksum() ? "Yes" : "No");
				draw_info("Encrypted:", f.is_encrypted() ? "Yes" : "No");
				draw_info("Game:", (game >= 0 && game < game_unknown) ? game_name[game] : "unknown");
				if (game > game_td)
				{
					draw_info("Raw Flags:", nh(8, f.rawflags()));
				}
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					int id = f.get_id(i);
					draw_info(nwzl(4, i) + " - " + nh(8, id) + swsl(11, theme::format_size(f.get_size(id))) + ' ' + mix_database::get_name(game, id), "");
				}
				break;
			}
		case ft_mix_rg:
			{
				Cmix_rg_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				draw_info("Files:", n(c_files));
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					const string name = f.get_name(i);
					draw_info(nwzl(4, i) + " - " + swsl(11, theme::format_size(f.get_size(name))) + ' ' + name, "");
				}
				break;
			}
		case ft_pak:
			{
				Cpak_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				draw_info("Files:", n(c_files));
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					draw_info(nwzl(4, i) + " - " + swsl(11, theme::format_size(f.get_size(f.get_name(i)))) + ' ' + f.get_name(i), "");
				}
				break;
			}
		case ft_mp3:
			{
				Cmp3_file f;
				f.load(m_data, m_size);
				const Cmp3_frame_header& header = f.header();
				draw_info("Bitrate:", n(header.bitrate()));
				draw_info("Channel Mode:", mpcm_name[header.channel_mode()]);
				draw_info("Copyright:", header.copyright() ? "Yes" : "No");
				draw_info("CRC:", header.crc() ? "Yes" : "no");
				draw_info("Emphasis:", n(header.emphasis()));
				draw_info("Layer:", n(header.layer()));
				draw_info("Mode Extension:", n(header.mode_extension()));
				draw_info("Original:", header.original() ? "Yes" : "No");
				draw_info("Padding:", header.padding() ? "Yes" : "No");
				draw_info("Sample Rate:", n(header.samplerate()));
				draw_info("Version:", mpv_name[header.version()]);
				break;
			}
		case ft_pal:
			{
				Cpal_file f;
				f.load(m_data);
				int y = m_y;
				const t_palette_entry* palette = f.get_palette();
				for (int i = 0; i < 256; i++)
				{
					CBrush box;
					CBrush brush;
					box.CreateSolidBrush(RGB(0, 0, 0));
					brush.CreateSolidBrush(RGB(palette[i].r * 255 / 63, palette[i].g * 255 / 63, palette[i].b * 255 / 63));
					y += m_y_inc;
					pDC->FillRect(CRect(CPoint(99, y + 4), CSize(26, m_y_inc * 2 / 3 + 2)), &box);
					pDC->FillRect(CRect(CPoint(100, y + 5), CSize(24, m_y_inc * 2 / 3)), &brush);
				}
				break;
			}
		case ft_pcx:
			{
				Cpcx_file f;
				f.load(m_data);
				const int c_planes = f.cb_pixel();
				const int cx = f.cx();
				const int cy = f.cy();
				draw_info("Bits/Pixel:", n(8 * c_planes));
				draw_info("Size:", n(cx) + " x " + n(cy));
				m_y += m_y_inc;
				Cvirtual_binary image;
				f.decode(image.write_start(c_planes * cx * cy));
				if (c_planes == 1)
				{
					load_color_table(*f.get_palette(), false);
					draw_image8(image.data(), cx, cy, pDC, offset);
				}
				else
					draw_image24(image.data(), cx, cy, pDC);
				m_y += cy + m_y_inc;
				break;
			}
		case ft_shp_dune2:
			{
				Cshp_dune2_file f;
				f.load(m_data);
				const int c_images = f.get_c_images();
				draw_info("Images:", n(c_images));
				draw_info("Offset Size:", n(f.get_cb_ofs()));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				// Cumulative-Y + decoded-frame cache. Per-frame LCWDecompress
				// + decode2 is expensive and was previously re-run on every
				// paint; cache makes scroll repaints just BitBlt.
				if (m_shp_grid.token != m_open_token)
				{
					m_shp_grid.token = m_open_token;
					m_shp_grid.cum_y.assign(c_images + 1, 0);
					m_shp_grid.decoded.assign(c_images, Cvirtual_binary());
					int y_acc = 0;
					for (int i = 0; i < c_images; i++)
					{
						m_shp_grid.cum_y[i] = y_acc;
						y_acc += f.get_cy(i) + m_y_inc;
					}
					m_shp_grid.cum_y[c_images] = y_acc;
				}
				const int grid_y0 = m_y;
				CRect clip;
				pDC->GetClipBox(&clip);
				const int clip_top_rel = clip.top - grid_y0;
				const int clip_bot_rel = clip.bottom - grid_y0;
				auto& cum = m_shp_grid.cum_y;
				int first = static_cast<int>(std::upper_bound(cum.begin(), cum.end(), clip_top_rel) - cum.begin()) - 1;
				if (first < 0) first = 0;
				if (first > c_images) first = c_images;
				m_y = grid_y0 + cum[first];
				for (int i = first; i < c_images; i++)
				{
					if (cum[i] >= clip_bot_rel)
						break;
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
					CRect r_test(offset, m_y, offset + cx * zoom_pre / 100, m_y + cy * zoom_pre / 100);
					if (pDC->RectVisible(&r_test))
					{
						if (m_shp_grid.decoded[i].size() != cx * cy)
						{
							byte* image = m_shp_grid.decoded[i].write_start(cx * cy);
							if (f.is_compressed(i))
							{
								byte* d = new byte[f.get_image_header(i)->size_out];
								decode2(d, image, LCWDecompress(f.get_image(i), d), f.get_reference_palette(i));
								delete[] d;
							}
							else
								decode2(f.get_image(i), image, f.get_image_header(i)->size_out, f.get_reference_palette(i));
						}
						draw_image8(m_shp_grid.decoded[i].data(), cx, cy, pDC, offset);
					}
					m_y += cy + m_y_inc;
				}
				m_y = grid_y0 + cum[c_images];
				break;
			}
		case ft_shp:
			{
				Cshp_file f;
				f.load(m_data);
				draw_info("Frames:", n(f.cf()));
				draw_info("Size:", n(f.cx()) + " x " + n(f.cy()));
				draw_info("X:", n(f.header().xpos));
				draw_info("Y:", n(f.header().ypos));
				draw_info("Delta Size:", n(f.header().delta));
				draw_info("Flags:", n(f.header().flags));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				Cvirtual_image image = f.vimage();
				const byte* r0 = image.image();
				const int cf = f.cf();
				const int fcx = f.cx();
				const int fcy = f.cy();
				const int fstride = f.cb_image();
				const int row_h = fcy + m_y_inc;
				const int grid_y0 = m_y;
				// Fixed-height frames: jump to first visible by division
				// instead of walking every frame on each scroll repaint.
				CRect clip;
				pDC->GetClipBox(&clip);
				int first = (clip.top - grid_y0) / row_h;
				if (first < 0) first = 0;
				if (first > cf) first = cf;
				m_y = grid_y0 + first * row_h;
				for (int i = first; i < cf; i++)
				{
					if (m_y >= clip.bottom)
						break;
					draw_image8(r0 + i * fstride, fcx, fcy, pDC, offset);
					m_y += row_h;
				}
				m_y = grid_y0 + cf * row_h;
				break;
			}
		case ft_shp_ts:
			{
				Cshp_ts_file f;
				f.load(m_data);
				const int c_images = f.cf();
				const int cx = m_cx = f.cx();
				const int cy = m_cy = f.cy();
				const int zero = f.zero();
				draw_info("Images:", n(c_images));
				draw_info("Size:", n(cx) + " x " + n(cy));
				draw_info("Unknown:", nh(8, zero));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
#ifdef NDEBUG
				// Release: build cumulative-Y cache once per file, then binary-
				// search to the first visible frame and stop at the last. Skips
				// the per-frame walk on every scroll repaint and caches the RLE
				// decode per-frame so subsequent paints don't re-decompress.
				// Debug build keeps the original serial walk because the
				// per-frame radar-color band advances m_y in ways the cache
				// geometry doesn't model — that's a debug-only blemish.
				if (m_shp_grid.token != m_open_token)
				{
					m_shp_grid.token = m_open_token;
					m_shp_grid.cum_y.assign(c_images + 1, 0);
					m_shp_grid.decoded.assign(c_images, Cvirtual_binary());
					int y_acc = 0;
					for (int i = 0; i < c_images; i++)
					{
						m_shp_grid.cum_y[i] = y_acc;
						const int fcy = f.get_cy(i);
						const int fcx = f.get_cx(i);
						if (fcx && fcy)
							y_acc += fcy + m_y_inc;
					}
					m_shp_grid.cum_y[c_images] = y_acc;
				}
				const int grid_y0 = m_y;
				CRect clip;
				pDC->GetClipBox(&clip);
				const int clip_top_rel = clip.top - grid_y0;
				const int clip_bot_rel = clip.bottom - grid_y0;
				auto& cum = m_shp_grid.cum_y;
				int first = static_cast<int>(std::upper_bound(cum.begin(), cum.end(), clip_top_rel) - cum.begin()) - 1;
				if (first < 0) first = 0;
				if (first > c_images) first = c_images;
				m_y = grid_y0 + cum[first];
				for (int i = first; i < c_images; i++)
				{
					if (cum[i] >= clip_bot_rel)
						break;
					const int fcx = f.get_cx(i);
					const int fcy = f.get_cy(i);
					if (fcx && fcy)
					{
						int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
						CRect r_test(offset, m_y, offset + fcx * zoom_pre / 100, m_y + fcy * zoom_pre / 100);
						if (pDC->RectVisible(&r_test))
						{
							if (f.is_compressed(i))
							{
								if (m_shp_grid.decoded[i].size() != fcx * fcy)
									RLEZeroTSDecompress(f.get_image(i), m_shp_grid.decoded[i].write_start(fcx * fcy), fcx, fcy);
								draw_image8(m_shp_grid.decoded[i].data(), fcx, fcy, pDC, offset);
							}
							else
								draw_image8(f.get_image(i), fcx, fcy, pDC, offset);
						}
						m_y += fcy + m_y_inc;
					}
				}
				m_y = grid_y0 + cum[c_images];
#else
				for (int i = 0; i < c_images; i++)
				{
					draw_info("Radar Color:", "R:" + nwzl(3, f.get_image_header(i)->red) + " G:" + nwzl(3, f.get_image_header(i)->green) + " B:" + nwzl(3, f.get_image_header(i)->blue) + " A:" + nwzl(3, f.get_image_header(i)->alpha));
					CBrush box;
					CBrush color;
					box.CreateSolidBrush(RGB(0, 0, 0));
					color.CreateSolidBrush(RGB(f.get_image_header(i)->red, f.get_image_header(i)->green, f.get_image_header(i)->blue));
					pDC->FillRect(CRect(CPoint(94, m_y - 12), CSize(26, m_y_inc * 2 / 3 + 2)), &box);
					pDC->FillRect(CRect(CPoint(95, m_y - 11), CSize(24, m_y_inc * 2 / 3)), &color);
					draw_info("Frame Flags:", nh(8, f.get_image_header(i)->flags));
					draw_info("Unknown:", nh(8, f.get_image_header(i)->zero));
					const int fcx = f.get_cx(i);
					const int fcy = f.get_cy(i);
					if (fcx && fcy)
					{
						int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
						CRect r_test(offset, m_y, offset + fcx * zoom_pre / 100, m_y + fcy * zoom_pre / 100);
						if (pDC->RectVisible(&r_test))
						{
							if (f.is_compressed(i))
							{
								Cvirtual_binary image;
								RLEZeroTSDecompress(f.get_image(i), image.write_start(fcx * fcy), fcx, fcy);
								draw_image8(image.data(), fcx, fcy, pDC, offset);
							}
							else
								draw_image8(f.get_image(i), fcx, fcy, pDC, offset);
						}
						m_y += fcy + m_y_inc;
					}
				}
#endif
				break;
			}
		case ft_tga:
			{
				Ctga_file f;
				f.load(m_data);
				if (f.cb_pixel() == 4)
				{
					// 32-bit BGRA: keep alpha and composite against checkerboard.
					const int cx = f.cx();
					const int cy = f.cy();
					draw_info("Bits/Pixel:", n(32));
					draw_info("Size:", n(cx) + " x " + n(cy));
					m_y += m_y_inc;

					// TGA stores bottom-up unless the "vertical" flag is set; flip rows
					// into a temporary buffer when needed so draw_image32 sees top-down.
					Cvirtual_binary buf;
					byte* dst = buf.write_start(cx * cy * 4);
					const byte* src = f.image();
					if (f.header().vertical)
					{
						memcpy(dst, src, cx * cy * 4);
					}
					else
					{
						for (int y = 0; y < cy; y++)
							memcpy(dst + y * cx * 4, src + (cy - 1 - y) * cx * 4, cx * 4);
					}
					draw_image32(dst, cx, cy, pDC, /*bgra=*/true);
					m_y += cy + m_y_inc;
				}
				else
				{
					Cvirtual_image image;
					if (!f.decode(image))
					{
						const int cx = image.cx();
						const int cy = image.cy();
						draw_info("Bits/Pixel:", n(8 * image.cb_pixel()));
						draw_info("Size:", n(cx) + " x " + n(cy));
						m_y += m_y_inc;
						if (image.cb_pixel() == 1)
						{
							load_color_table(image.palette(), false);
							draw_image8(image.image(), cx, cy, pDC, offset);
						}
						else if (image.cb_pixel() == 3)
							draw_image24(image.image(), cx, cy, pDC);
						m_y += cy + m_y_inc;
					}
				}
				break;
			}
		case ft_tmp:
			{
				Ctmp_file f;
				f.load(m_data);
				const int c_tiles = f.get_c_tiles();
				draw_info("Icons:", n(c_tiles));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < c_tiles; i++)
				{
					if (f.get_index1()[i] != 0xff)
					{
						draw_image8(f.get_image(i), 24, 24, pDC, offset);
						m_y += 24 + m_y_inc;
					}
				}
				break;
			}
		case ft_tmp_ra:
			{
				Ctmp_ra_file f;
				f.load(m_data);
				const int c_tiles = f.get_c_tiles();
				static const int size = 24;
				int sx = f.cx();
				int sy = f.cy();
				int cx = f.get_cblocks_x();
				int cy = f.get_cblocks_y();
				if (cx == -1 && cy == -1)
				{
					cx = 1;
					cy = c_tiles;
				}
				draw_info("Icons:", n(c_tiles));
				draw_info("Size:", n(cx) + " x " + n(cy));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				if (cx == 1 && cy == 1)
				{
					for (int i = 0; i < c_tiles; i++)
					{
						if (f.get_index1()[i] != 0xff)
						{
							draw_image8(f.get_image(i), size, size, pDC, offset);
							m_y += size + m_y_inc;
						}
					}
				}
				else
				{
					int i = 0;
					for (int y = 0; y < cy; y++)
					{
						for (int x = 0; x < cx; x++)
						{
							if (f.get_index1()[i] != 0xff)
							{
								draw_image8(f.get_image(i), size, size, pDC, (size * x) + offset);
							}
							i++;
						}
						m_y += size;
					}
					m_y += m_y_inc;
				}
				break;
			}
		case ft_tmp_ts:
			{
				Ctmp_ts_file f;
				f.load(m_data);
				const int c_tiles = f.get_c_tiles();
				m_cx = f.get_cx();
				m_cy = f.get_cy();
				draw_info("Tiles:", n(c_tiles));
				draw_info("Size:", n(f.get_cblocks_x()) + " x " + n(f.get_cblocks_y()));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				int x, y, cx, cy;
				f.get_rect(x, y, cx, cy);
				byte* d = new byte[cx * cy];
				f.draw(d);
				draw_image8(d, cx, cy, pDC, 4);
				m_y += cy + m_y_inc;
#ifndef NDEBUG
				for (int i = 0; i < f.get_c_tiles(); i++)
				{
					if (!f.get_index()[i])
						continue;
					const t_tmp_image_header& header = *f.get_image_header(i);
					/*
					draw_info("Tile:", n(i));
					draw_info("x:", n(header.x));
					draw_info("y:", n(header.y));
					for (int j = 0; j < 3; j++)
						draw_info("unknown1[" + n(j) +"]:", n(header.unknown1[j]));
					draw_info("x_extra:", n(header.x_extra));
					draw_info("y_extra:", n(header.y_extra));
					draw_info("cx_extra:", n(header.cx_extra));
					draw_info("cy_extra:", n(header.cy_extra));
					*/
					/*
					draw_info("flags:", n(header.flags & 7));
					draw_info("height:", n(header.height));
					draw_info("terrain type:", n(header.terraintype));
					draw_info("direction:", n(header.direction));
					// draw_info("unknown2[0]:", nh(6, header.unknown2[0] & 0xffffff));
					/*
					for (j = 0; j < 3; j++)
						draw_info("unknown2[" + n(j) +"]:", nh(8, header.unknown2[j]));
					if (f.has_extra_graphics(i))
					{
						draw_image8(f.get_image(i) + 2 * 576, header.cx_extra, header.cy_extra, pDC, 0, m_y);
						m_y += header.cy_extra;
					}
					*/
					m_y += m_y_inc;
				}
#endif
				delete[] d;
				break;
			}
		case ft_voc:
			{
				Cvoc_file f;
				f.load(m_data);
				draw_info("Audio:", n(f.get_samplerate()) + " hz, 8 bit, Mono");
				draw_info("Samples:", n(f.get_c_samples()));
				break;
			}
		case ft_vqa:
			{
				Cvqa_file f;
				f.load(m_data);
				draw_info("Version:", n(f.header().version));
				draw_info("Video Flags:", nh(4, f.header().video_flags));
				draw_info("Frames:", n(f.get_c_frames()));
				draw_info("Size:", n(f.get_cx()) + " x " + n(f.get_cy()));
				draw_info("Block Size:", n(f.get_cx_block()) + " x " + n(f.get_cy_block()));
				draw_info("Audio:", n(f.get_samplerate()) + " hz, " + n(f.get_cbits_sample()) + " bit, " + (f.get_c_channels() == 1 ? "Mono" : "Stereo"));
				break;
			}
		case ft_vxl:
			{
				m_y += m_y_inc;
				Cvxl_file f;
				f.load(m_data);
				int vxl_mode = GetMainFrame()->get_vxl_mode();
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < f.get_c_section_headers(); i++)
				{
					const t_vxl_section_tailer& section_tailer = *f.get_section_tailer(i);
					const int cx = section_tailer.cx;
					const int cy = section_tailer.cy;
					const int cz = section_tailer.cz;
					const int l = ceil(sqrt((cx * cx + cy * cy + cz * cz) / 4.0));
					const int cl = 2 * l;
					const double center_x = cx / 2;
					const double center_y = cy / 2;
					const double center_z = cz / 2;
					const int c_pixels = cl * cl;
					draw_info("Section " + n(i) + ':', n(cx) + " x " + n(cy) + " x " + n(cz));
					char fb[32];
					for (int ty = 0; ty < 3; ty++)
					{
						string s;
						for (int tx = 0; tx < 4; tx++)
						{
							s += _gcvt(section_tailer.transform[ty][tx], 10, fb);
							s += ' ';
						}
						draw_info(n(ty), s);
					}
					draw_info("Scale:", _gcvt(section_tailer.scale, 10, fb));
					draw_info("X min:", _gcvt(section_tailer.x_min_scale, 10, fb));
					draw_info("Y min:", _gcvt(section_tailer.y_min_scale, 10, fb));
					draw_info("Z min:", _gcvt(section_tailer.z_min_scale, 10, fb));
					draw_info("X max:", _gcvt(section_tailer.x_max_scale, 10, fb));
					draw_info("Y max:", _gcvt(section_tailer.y_max_scale, 10, fb));
					draw_info("Z max:", _gcvt(section_tailer.z_max_scale, 10, fb));
					draw_info("Normal Type:", n(section_tailer.unknown));
					m_y += m_y_inc;
					// VXL grid: 64 independent voxel rasterizations (8 yaw × 8 pitch).
					// Each cell does a full per-section voxel splat into its own
					// image / image_s / image_z buffers — no shared state across
					// cells, so this is the dominant cost and a clean parallel-for
					// candidate. We split it into:
					//   Phase 1 (parallel): rasterize all 64 cells. Mode 2 also
					//     normalizes z and builds its per-cell gray palette here.
					//   Phase 2 (serial UI thread): walk the cells and call
					//     draw_image8 + load_color_table. GDI work must stay on
					//     the UI thread because pDC and the member DIB scratch
					//     (mh_dib / mp_dib in draw_image8) aren't thread-safe.
					struct vxl_grid_cell {
						std::vector<byte> image;
						std::vector<byte> image_s;
						std::vector<char> image_z;
						t_palette mode2_palette{};	// only populated when vxl_mode == 2
					};
					std::vector<vxl_grid_cell> cells(64);
					for (auto& c : cells)
					{
						c.image.assign(c_pixels, 0);
						c.image_s.assign(c_pixels, 0);
						c.image_z.assign(c_pixels, CHAR_MIN);
					}
					#pragma omp parallel for schedule(static)
					for (int cell_idx = 0; cell_idx < 64; cell_idx++)
					{
						const int yr = cell_idx / 8;
						const int xr = cell_idx % 8;
						vxl_grid_cell& cc = cells[cell_idx];
						byte* image = cc.image.data();
						byte* image_s = cc.image_s.data();
						char* image_z = cc.image_z.data();
						int j = 0;
						for (int y = 0; y < cy; y++)
						{
							for (int x = 0; x < cx; x++)
							{
								const byte* r = f.get_span_data(i, j);
								if (r)
								{
									int z = 0;
									while (z < cz)
									{
										z += *r++;
										int c = *r++;
										while (c--)
										{
											t_vector s_pixel;
											s_pixel.x = x - center_x;
											s_pixel.y = y - center_y;
											s_pixel.z = z - center_z;
											t_vector d_pixel = rotate_y(rotate_x(s_pixel, xr * std::numbers::pi / 4), yr * std::numbers::pi / 4);
											d_pixel.x += l;
											d_pixel.y += l;
											d_pixel.z += center_z;
											int ofs = static_cast<int>(d_pixel.x) + cl * static_cast<int>(d_pixel.y);
											if (d_pixel.z > image_z[ofs])
											{
												image[ofs] = *r++;
												image_s[ofs] = *r++;
												image_z[ofs] = d_pixel.z;
											}
											else
												r += 2;
											z++;
										}
										r++;
									}
								}
								j++;
							}
						}
						if (vxl_mode == 2)
						{
							// Per-cell z-range normalization + gray palette.
							int min_z = INT_MAX;
							int max_z = INT_MIN;
							for (int o = 1; o < c_pixels; o++)
							{
								int v = image_z[o];
								if (v == CHAR_MIN) continue;
								if (v < min_z) min_z = v;
								if (v > max_z) max_z = v;
							}
							for (int o = 0; o < c_pixels; o++)
							{
								if (image_z[o] == CHAR_MIN)
									image_z[o] = -1;
								else
									image_z[o] -= min_z;
							}
							max_z -= min_z;
							for (int p = 0; p < max_z; p++)
								cc.mode2_palette[p].r = cc.mode2_palette[p].g = cc.mode2_palette[p].b = p * 255 / max_z;
							cc.mode2_palette[0xff].r = 0;
							cc.mode2_palette[0xff].g = 0;
							cc.mode2_palette[0xff].b = 0xff;
						}
					}
					// Mode 1 uses one section-wide gray palette; load it once.
					if (vxl_mode == 1)
					{
						t_palette gray_palette;
						if (section_tailer.unknown == 2)
						{
							for (int gi = 0; gi < 256; gi++)
								gray_palette[gi].r = gray_palette[gi].g = gray_palette[gi].b = gi * 255 / 35;
						}
						else
						{
							for (int gi = 0; gi < 256; gi++)
								gray_palette[gi].r = gray_palette[gi].g = gray_palette[gi].b = gi;
						}
						load_color_table(gray_palette, false);
					}
					// Phase 2: serial draw on the UI thread.
					for (int yr = 0; yr < 8; yr++)
					{
						for (int xr = 0; xr < 8; xr++)
						{
							vxl_grid_cell& cc = cells[yr * 8 + xr];
							switch (vxl_mode)
							{
							case 0:
								draw_image8(cc.image.data(), cl, cl, pDC, xr * (cl + m_y_inc) + offset);
								break;
							case 1:
								draw_image8(cc.image_s.data(), cl, cl, pDC, xr * (cl + m_y_inc) + offset);
								break;
							case 2:
								load_color_table(cc.mode2_palette, false);
								draw_image8(reinterpret_cast<const byte*>(cc.image_z.data()), cl, cl, pDC, xr * (cl + m_y_inc) + offset);
								break;
							}
						}
						m_y += cl + m_y_inc;
					}
				}
				break;
			}
		case ft_wav:
			{
				Cwav_file f;
				f.load(m_data);
				if (!f.process())
				{
					const t_riff_wave_format_chunk& format_chunk = f.get_format_chunk();
					draw_info("Audio:", n(format_chunk.samplerate) + " hz, " + n(format_chunk.cbits_sample) + " bit, " + (format_chunk.c_channels == 1 ? "mono" : "stereo"));
					draw_info("Samples:", n(format_chunk.tag == 1 ? f.get_data_header().size * 8 / (format_chunk.cbits_sample * format_chunk.c_channels) : f.get_fact_chunk().c_samples));
					draw_info("Format:", nh(4, format_chunk.tag));
				}
				break;
			}
		case ft_wsa_dune2:
			{
				Cwsa_dune2_file f;
				f.load(m_data);
				draw_info("Frames:", n(f.cf()));
				draw_info("Size:", n(f.cx()) + " x " + n(f.cy()));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				Cvirtual_image image = f.vimage();
				const byte* r = image.image();
				for (int i = 0; i < f.cf(); i++)
				{
					draw_image8(r, f.cx(), f.cy(), pDC, offset);
					r += f.cb_image();
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		case ft_wsa:
			{
				Cwsa_file f;
				f.load(m_data);
				draw_info("Frames:", n(f.cf()));
				draw_info("Paletted:", f.palette() ? "Yes" : "No");
				draw_info("Position:", n(f.get_x()) + "," + n(f.get_y()));
				draw_info("Size:", n(f.cx()) + " x " + n(f.cy()));
				m_y += m_y_inc;
				load_color_table(f.palette(), true);
				Cvirtual_image image = f.vimage();
				const byte* r = image.image();
				for (int i = 0; i < f.cf(); i++)
				{
					draw_image8(r, f.cx(), f.cy(), pDC, offset);
					r += f.cb_image();
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		default:
			show_binary = true;
		}
		if (!m_text_cache_valid)
		{
			switch (m_ft)
			{
			case ft_pal:
				{
					Cpal_file f;
					f.load(m_data);
					m_y += m_y_inc;
					const t_palette_entry* palette = f.get_palette();
					for (int i = 0; i < 256; i++)
						draw_info((nh(2, i) + " - " + nwzl(2, palette[i].r) + ' '+ nwzl(2, palette[i].g) + ' ' + nwzl(2, palette[i].b)), "");
					break;
				}
			case ft_pkt_ts:
				{
					Cpkt_ts_ini_reader ir;
					ir.process(m_data);
					const Cpkt_ts_ini_reader::t_map_list& ml = ir.get_map_list();
					draw_info("Maps:", n(ml.size()));
					m_y += m_y_inc;
					for (auto& i : ml)
						draw_info(i.first, i.second.m_description + ", " + i.second.m_gamemode);
					break;
				}
			case ft_st:
				{
					Cst_file f;
					f.load(m_data);
					const int c_strings = f.get_c_strings();
					draw_info("Strings", n(c_strings));
					m_y += m_y_inc;
					for (int i = 0; i < c_strings; i++)
						draw_info(nwzl(5, i) + ' ' + f.get_string(i), "");
					break;
				}
			case ft_theme_ini_ts:
			case ft_sound_ini_ts:
			case ft_ini:
			case ft_rules_ini_ts:
			case ft_text:
				{
					m_y += m_y_inc;
					Cvirtual_tfile tf;
					tf.load_data(m_data);
					while (!tf.eof())
						draw_info(tf.read_line(), "");
					break;
				}
			case ft_xif:
				{
					m_y += m_y_inc;
					Cxif_key key;
					if (!key.load_key(m_data))
					{
						stringstream s;
						key.dump(s, m_data.size() < 1 << 20);
						string line;
						while (getline(s, line))
						{
							draw_info(line, "");
						}
					}
					break;
				}
			default:
				Cfname fname = to_lower(m_fname);
				if (fname.get_fext() == ".mix" && m_ft != ft_mix && m_ft != ft_mix_rg)
				{
					m_ft = ft_mix;
					Cmix_file_rd f;
					f.load(m_data, m_size);
					const int c_files = f.get_c_files();
					const t_game game = f.get_game();
					draw_info("Files:", n(c_files));
					draw_info("Checksum:", f.has_checksum() ? "Yes" : "No");
					draw_info("Encrypted:", f.is_encrypted() ? "Yes" : "No");
					draw_info("Game:", (game >= 0 && game < game_unknown) ? game_name[game] : "unknown");
					if (game > game_td)
					{
						draw_info("Raw Flags:", nh(8, f.rawflags()));
					}
					for (int i = 0; i < c_files; i++)
					{
						int id = f.get_id(i);
						draw_info(nwzl(4, i) + " - " + nh(8, id) + swsl(11, theme::format_size(f.get_size(id))) + ' ' + mix_database::get_name(game, id), "");
					}
					m_y += m_y_inc;
					break;
					}
				if (!show_binary)
					break;
				m_y += m_y_inc;
				if (m_data.size() > 32  << 10)
					m_data.set_size(32 << 10);
				for (int r = 0; r < m_data.size(); )
				{
					string line = nwzl(5, r) + ' ';
					int line_data[16];
					for (int c = 0; c < 16; c++)
					{
						line_data[c] = r < m_data.size() ? m_data.data()[r] : -1;
						r++;
					}
					for (int c = 0; c < 16; c++)
					{
						if (!(c & 7))
							line += "- ";
						line += line_data[c] == -1 ? "   " : nh(2, line_data[c]) + ' ';
					}
					line += "- ";
					for (int c = 0; c < 16; c++)
						line += line_data[c] < 0x20 ? ' ' : line_data[c];
					draw_info(line, "");
				}
			}
			SetScrollSizes(MM_TEXT, CSize(m_x, m_y + 4));
			m_text_cache_valid = true;
		}
		for (auto& i : m_text_cache)
		{
				pDC->TextOut(i.text_extent.TopLeft().x, i.text_extent.TopLeft().y, i.t.c_str());
		}
	}
}

void CXCCFileView::open_f(int id, Cmix_file& mix_f, t_game game, t_palette palette)
{
	close_f();
	Ccc_file f(false);
	if (!f.open(id, mix_f))
	{
		m_fname = mix_f.get_name(id);
		m_game = game;
		m_id = id;
		m_palette = palette;
	}
	m_source_mix = &mix_f;
	m_disk_dir.clear();
	post_open(f);
}

void CXCCFileView::open_f(const string& name)
{
	close_f();
	Ccc_file f(false);
	if (!f.open(name))
	{
		m_fname = Cfname(name).get_fname();
		m_game = GetMainFrame()->get_game();
		m_id = Cmix_file::get_id(m_game, Cfname(name).get_ftitle());
		m_palette = NULL;
	}
	m_source_mix = nullptr;
	m_disk_dir = Cfname(name).get_path();
	post_open(f);
}

void CXCCFileView::reload_current()
{
	if (!m_is_open)
		return;
	if (m_source_mix)
	{
		Cmix_file* src = m_source_mix;
		int id = m_id;
		t_game game = m_game;
		open_f(id, *src, game, m_palette);
	}
	else if (!m_disk_dir.empty())
	{
		open_f(m_disk_dir + m_fname);
	}
}

void CXCCFileView::post_open(Ccc_file& f)
{
	if (f.is_open())
	{
		m_can_pick = false;
		m_cx = 0;
		m_cy = 0;
		m_ft = f.get_file_type(false);
		m_size = f.get_size();
		int cb_max_data = (m_ft == ft_bmp || m_ft == ft_dds || m_ft == ft_jpeg || m_ft == ft_map_td || m_ft == ft_map_ra
			|| m_ft == ft_map_ts || m_ft == ft_mix_rg || m_ft == ft_pcx || m_ft == ft_png || m_ft == ft_shp
			|| m_ft == ft_shp_ts || m_ft == ft_tga || m_ft == ft_vxl || m_ft == ft_wsa_dune2
			|| m_ft == ft_wsa || m_ft == ft_xif) ? m_size :
			(m_ft == ft_csf ? 64 << 8 : 256 << 10);
		int cb_data = m_size > cb_max_data ? cb_max_data : m_size;
		f.read(m_data.write_start(cb_data), cb_data);
		f.close();
		m_text_cache_valid = false;
		// Invalidate per-file grid caches. The shp_grid cache compares its
		// token against m_open_token; bumping here forces a rebuild on the
		// next paint. Don't clear vectors — they'll be reassigned on rebuild
		// and the old allocations get reused if same/larger size.
		m_open_token++;
		// HVA pairs with one VXL; drop any previous load so the next file
		// starts clean. The button can re-load on demand.
		m_hva_loaded = false;
		m_hva_data.clear();
		m_hva_vxl_half = 0;
		// Drop any auto-loaded sibling parts from a previous VXL; the new file's
		// parts are populated below if it's a VXL and the toggle is on.
		m_vxl_parts.clear();
		m_is_open = true;
		m_zoom_pct = 100;
		m_zoomable_file =
			m_ft == ft_dds || m_ft == ft_cps ||
			m_ft == ft_jpeg || m_ft == ft_jpeg_single ||
			m_ft == ft_png || m_ft == ft_png_single ||
			m_ft == ft_pcx || m_ft == ft_pcx_single ||
			m_ft == ft_tga || m_ft == ft_tga_single ||
			m_ft == ft_bmp;
		// VXL full-hierarchy auto-load: when opening a body VXL, look for the
		// turret/barrel siblings (and their HVAs) in the same MIX, the opposite
		// pane's MIX, or — for disk-loaded files — the same folder. Mirrors
		// Vengi's VXLFormat (basename + "tur.vxl"/"barl.vxl"). Each part with
		// its exact-name <part>.hva auto-paired.
		if (m_ft == ft_vxl && theme::vxl_full_hierarchy())
			vxl_load_parts();
		// Auto-load voxels.vpl alongside the VXL — engine-faithful shading.
		// Drops the previous load first; succeeds silently or leaves the
		// viewer on the synthetic-shading fallback.
		if (m_ft == ft_vxl)
		{
			m_vpl_loaded = false;
			m_vpl_data.clear();
			m_vpl_name.clear();
			try_auto_load_vpl();
		}
	}
	if (m_player_mode)
		player_exit();
	ScrollToPosition(CPoint(0, 0));
	load_pal_btn_update_visibility();
	try_auto_load_paired_pal();
	Invalidate();
}

void CXCCFileView::close_f()
{
	m_is_open = false;
	m_text_cache.clear();
	// Hide the status-bar Load PAL button — no file open means nothing to load
	// a palette for.
	if (CMainFrame* mf = GetMainFrame())
		mf->show_load_pal_button(false);
}

bool CXCCFileView::is_playable_file() const
{
	if (!m_is_open)
		return false;
	return m_ft == ft_shp || m_ft == ft_shp_dune2 || m_ft == ft_shp_ts ||
		m_ft == ft_wsa || m_ft == ft_wsa_dune2 || m_ft == ft_vxl;
}

int CXCCFileView::player_total_frames() const
{
	return m_player_cf;
}

void CXCCFileView::player_decode_frames()
{
	m_player_frames.clear();
	m_player_bgra.clear();
	m_player_bgra_version++;
	m_player_cx = 0;
	m_player_cy = 0;
	m_player_cf = 0;
	if (!is_playable_file())
		return;
	if (m_ft == ft_shp)
	{
		Cshp_file f;
		f.load(m_data);
		load_color_table(get_default_palette(), true);
		m_player_cx = f.cx();
		m_player_cy = f.cy();
		m_player_cf = f.cf();
		Cvirtual_image image = f.vimage();
		const byte* r = image.image();
		int cb = f.cb_image();
		for (int i = 0; i < m_player_cf; i++)
		{
			Cvirtual_binary v;
			memcpy(v.write_start(cb), r, cb);
			m_player_frames.push_back(v);
			r += cb;
		}
	}
	else if (m_ft == ft_shp_dune2)
	{
		Cshp_dune2_file f;
		f.load(m_data);
		load_color_table(get_default_palette(), true);
		m_player_cf = f.get_c_images();
		// Frames have different sizes; find a bounding box.
		int max_cx = 0, max_cy = 0;
		for (int i = 0; i < m_player_cf; i++)
		{
			max_cx = std::max(max_cx, f.get_cx(i));
			max_cy = std::max(max_cy, f.get_cy(i));
		}
		m_player_cx = max_cx > 0 ? max_cx : 1;
		m_player_cy = max_cy > 0 ? max_cy : 1;
		for (int i = 0; i < m_player_cf; i++)
		{
			int cx = f.get_cx(i);
			int cy = f.get_cy(i);
			Cvirtual_binary v;
			byte* w = v.write_start(m_player_cx * m_player_cy);
			memset(w, 0, m_player_cx * m_player_cy);
			if (cx > 0 && cy > 0)
			{
				byte* image = new byte[cx * cy];
				if (f.is_compressed(i))
				{
					byte* d = new byte[f.get_image_header(i)->size_out];
					decode2(d, image, LCWDecompress(f.get_image(i), d), f.get_reference_palette(i));
					delete[] d;
				}
				else
					decode2(f.get_image(i), image, f.get_image_header(i)->size_out, f.get_reference_palette(i));
				int x_off = (m_player_cx - cx) / 2;
				int y_off = (m_player_cy - cy) / 2;
				for (int y = 0; y < cy; y++)
					memcpy(w + (y + y_off) * m_player_cx + x_off, image + y * cx, cx);
				delete[] image;
			}
			m_player_frames.push_back(v);
		}
	}
	else if (m_ft == ft_shp_ts)
	{
		Cshp_ts_file f;
		f.load(m_data);
		load_color_table(get_default_palette(), true);
		m_player_cx = f.cx();
		m_player_cy = f.cy();
		m_player_cf = f.cf();
		// SHP(TS) frames vary in size and store their own (x, y) offsets inside
		// the global cx*cy canvas. Use those offsets, not centering — body and
		// shadow frames have different (x, y) and shadows must align to body
		// positions when composited.
		for (int i = 0; i < m_player_cf; i++)
		{
			int cx = f.get_cx(i);
			int cy = f.get_cy(i);
			Cvirtual_binary v;
			byte* w = v.write_start(m_player_cx * m_player_cy);
			memset(w, 0, m_player_cx * m_player_cy);
			if (cx > 0 && cy > 0)
			{
				Cvirtual_binary tmp;
				const byte* src = f.get_image(i);
				if (f.is_compressed(i))
				{
					RLEZeroTSDecompress(src, tmp.write_start(cx * cy), cx, cy);
					src = tmp.data();
				}
				int x_off = f.get_x(i);
				int y_off = f.get_y(i);
				int dst_x0 = std::max(0, x_off);
				int dst_y0 = std::max(0, y_off);
				int dst_x1 = std::min(m_player_cx, x_off + cx);
				int dst_y1 = std::min(m_player_cy, y_off + cy);
				for (int y = dst_y0; y < dst_y1; y++)
				{
					int sy = y - y_off;
					memcpy(w + y * m_player_cx + dst_x0,
						src + sy * cx + (dst_x0 - x_off),
						dst_x1 - dst_x0);
				}
			}
			m_player_frames.push_back(v);
		}
	}
	else if (m_ft == ft_wsa)
	{
		Cwsa_file f;
		f.load(m_data);
		load_color_table(f.palette(), true);
		m_player_cx = f.cx();
		m_player_cy = f.cy();
		m_player_cf = f.cf();
		Cvirtual_image image = f.vimage();
		const byte* r = image.image();
		int cb = f.cb_image();
		for (int i = 0; i < m_player_cf; i++)
		{
			Cvirtual_binary v;
			memcpy(v.write_start(cb), r, cb);
			m_player_frames.push_back(v);
			r += cb;
		}
	}
	else if (m_ft == ft_wsa_dune2)
	{
		Cwsa_dune2_file f;
		f.load(m_data);
		load_color_table(get_default_palette(), true);
		Cvirtual_image image = f.vimage();
		m_player_cx = image.cx();
		m_player_cy = image.cy();
		m_player_cf = f.cf();
		const byte* r = image.image();
		int cb = m_player_cx * m_player_cy;
		for (int i = 0; i < m_player_cf; i++)
		{
			Cvirtual_binary v;
			memcpy(v.write_start(cb), r, cb);
			m_player_frames.push_back(v);
			r += cb;
		}
	}
	else if (m_ft == ft_vxl)
	{
		// Source list: the body's VXL + bytes + optional HVA, followed by any
		// auto-loaded sibling parts (turret/barrel). Each source contributes
		// sections to a single merged m_vxl_cloud. Each source has its own
		// independent HVA timeline; lengths can differ, and shorter HVAs clamp
		// at their last keyframe while longer ones keep playing.
		struct vxl_source
		{
			Cvxl_file vxl;
			Cvirtual_binary hva_bytes;
			Chva_file hva;
			bool hva_ok = false;
		};
		std::vector<vxl_source> sources(1 + m_vxl_parts.size());
		sources[0].vxl.load(m_data);
		if (m_hva_loaded && m_hva_data.size() > 0)
		{
			sources[0].hva_bytes = m_hva_data;
			sources[0].hva.load(sources[0].hva_bytes);
			if (sources[0].hva.is_valid() && sources[0].hva.get_c_frames() > 0)
				sources[0].hva_ok = true;
		}
		for (size_t pi = 0; pi < m_vxl_parts.size(); pi++)
		{
			auto& s = sources[1 + pi];
			s.vxl.load(m_vxl_parts[pi].vxl_data);
			if (m_vxl_parts[pi].hva_loaded && m_vxl_parts[pi].hva_data.size() > 0)
			{
				s.hva_bytes = m_vxl_parts[pi].hva_data;
				s.hva.load(s.hva_bytes);
				if (s.hva.is_valid() && s.hva.get_c_frames() > 0)
					s.hva_ok = true;
			}
		}

		load_color_table(get_default_palette(), true);

		// Resolve the timeline position to (kf_start, kf_end, kf_t) for one
		// source. HVA = keyframed animation; the engine interpolates between
		// consecutive keyframes spaced c_HVA_INTER timeline steps apart
		// (rotation slerped, translation lerped). Timeline length per source
		// = (n_kf - 1) * c_HVA_INTER + 1. The pane-wide m_player_cf is the
		// **max** across sources (computed below); a source with fewer
		// keyframes clamps at its last keyframe while longer animations keep
		// playing.
		auto resolve_kf = [this](const Chva_file& hva, int& kf_start, int& kf_end, float& kf_t) {
			const int n_kf = hva.get_c_frames();
			if (n_kf <= 1)
			{
				kf_start = kf_end = 0;
				kf_t = 0.0f;
				return;
			}
			int tl = m_player_frame;
			const int span = c_HVA_INTER;
			const int total = (n_kf - 1) * span + 1;
			if (tl < 0) tl = 0;
			if (tl >= total) tl = total - 1;
			int k0 = tl / span;
			int rem = tl - k0 * span;
			if (k0 >= n_kf - 1) { k0 = n_kf - 1; rem = 0; }
			kf_start = k0;
			kf_end = (k0 + 1 < n_kf) ? (k0 + 1) : k0;
			kf_t = static_cast<float>(rem) / static_cast<float>(span);
		};

		// Build the object-space point cloud once. The viewer rasterizes it
		// per frame at the current m_vxl_yaw/m_vxl_pitch.
		m_vxl_cloud.clear();
		m_vxl_normal_type = 0;
		for (auto& src : sources)
		{
			Cvxl_file& f = src.vxl;
			Chva_file& hva = src.hva;
			const bool hva_ok = src.hva_ok;
			int kf_start = 0, kf_end = 0;
			float kf_t = 0.0f;
			if (hva_ok)
				resolve_kf(hva, kf_start, kf_end, kf_t);
		const int n_sections = f.get_c_section_headers();
		for (int i = 0; i < n_sections; i++)
		{
			const t_vxl_section_tailer& st = *f.get_section_tailer(i);
			const int cx = st.cx;
			const int cy = st.cy;
			const int cz = st.cz;
			double sx = (st.x_max_scale - st.x_min_scale) / std::max(1, cx);
			double sy = (st.y_max_scale - st.y_min_scale) / std::max(1, cy);
			double sz = (st.z_max_scale - st.z_min_scale) / std::max(1, cz);
			// Pick the section transform: HVA per-frame matrix when loaded,
			// otherwise the static VXL section transform. Match by 16-byte
			// section id first; if no match (common — TS/RA2 HVAs frequently
			// rename sections), fall back to positional index pairing so a
			// single-section turret HVA still drives a single-section VXL.
			//
			// HVA translation column is expressed in *voxels* (per the HVA
			// spec); convert to world units by multiplying by the section's
			// voxel scale. Rotation/shear columns are unitless multipliers
			// that act directly on world-unit local coords, so they pass
			// through unchanged. We also skip the file-Y flip used by the
			// static path when HVA is active — HVAs are authored in the
			// engine's native axes which already match yaw=0 facing camera.
			float tm[3][4];
			memcpy(tm, st.transform, sizeof(tm));
			if (hva_ok)
			{
				int hs_match = -1;
				const char* vid = f.get_section_header(i)->id;
				for (int hs = 0; hs < hva.get_c_sections(); hs++)
				{
					if (!strncmp(hva.get_section_id(hs), vid, 16))
					{
						hs_match = hs;
						break;
					}
				}
				if (hs_match < 0 && i < hva.get_c_sections())
					hs_match = i;
				if (hs_match >= 0)
				{
					// Interpolate the section transform between kf_start and
					// kf_end (rotation slerped, translation lerped). When the
					// two keyframes are the same (single-keyframe HVA, or the
					// timeline lands exactly on a key) hva_interp degenerates
					// to that keyframe's matrix.
					const float* ma = hva.get_transform_matrix(hs_match, kf_start);
					const float* mb = hva.get_transform_matrix(hs_match, kf_end);
					hva_interp(ma, mb, kf_t, tm);
					// HVA translation column is in leptons (cell-based units).
					// Scale by st.scale (the section's global scale, ~1/12) to
					// bring it into the same world-unit space as the voxel
					// positions, which are already sx/sy/sz-scaled when emitted.
					tm[0][3] *= st.scale;
					tm[1][3] *= st.scale;
					tm[2][3] *= st.scale;
				}
			}
			// Section-local occupancy grid for neighbor-based normal derivation.
			// Each voxel's normal points away from its empty neighbor cells (sum
			// of empty-side unit vectors). Store color sentinel 0 = empty so a
			// single byte per cell suffices; 1 = occupied.
			std::vector<unsigned char> occ(static_cast<size_t>(cx) * cy * cz, 0);
			auto occ_idx = [cx, cy](int x, int y, int z) { return x + cx * (y + cy * z); };
			// Pass 1: parse spans into a parallel scratch list of
			// (lx,ly,lz,color,normal_idx) and populate the occupancy grid.
			// normal_idx is the on-disk Westwood normal-table index — only
			// used when theme::vxl_normal_src() == vxl_normals_file (Pass 2
			// branches on that). Computed-normals path ignores normal_idx.
			struct local_voxel { int lx, ly, lz; unsigned char color; unsigned char normal_idx; };
			std::vector<local_voxel> locals;
			int j = 0;
			for (int y = 0; y < cy; y++)
			{
				for (int x = 0; x < cx; x++)
				{
					const byte* r = f.get_span_data(i, j++);
					if (!r)
						continue;
					int z = 0;
					while (z < cz)
					{
						z += *r++;
						int c = *r++;
						while (c--)
						{
							const unsigned char color_byte = *r++;
							const unsigned char normal_byte = *r++;
							locals.push_back({ x, y, z, color_byte, normal_byte });
							if (x >= 0 && x < cx && y >= 0 && y < cy && z >= 0 && z < cz)
								occ[occ_idx(x, y, z)] = 1;
							z++;
						}
						r++;
					}
				}
			}
			// Pass 2: emit world-space voxels with normals.
			// Two normal sources, and for the Computed source three algorithms:
			//   computed/basic    : 6-neighbor empty-side sum (legacy).
			//   computed/weighted : 26-neighbor filled-side sum with face=1,
			//                       edge=1/sqrt(2), corner=1/sqrt(3) weights.
			//                       Vengi-style.
			//   computed/gradient : central-difference gradient of a Gaussian-
			//                       blurred density field. Smooth, continuous
			//                       directions — best for curved hulls.
			//   file              : Westwood normal-table lookup.
			// Both paths then rotate by tm's 3x3 submatrix, flip Y to match
			// the position flip, and renormalize. Fallback +Z when local
			// normal length is below 1e-6 (fully surrounded voxel, etc.).
			const theme::vxl_normal_source norm_src = theme::vxl_normal_src();
			const theme::vxl_normal_method norm_method = theme::vxl_normals_method();
			const unsigned char normal_type = static_cast<unsigned char>(st.unknown);
			// Capture for the surface-normals view mode's gray ramp. Sections of
			// one model share a normal type, so last-wins is fine.
			m_vxl_normal_type = normal_type;
			// Smooth-gradient pre-pass: build a separable-Gaussian-blurred
			// float density field over the section's occupancy grid. Runs
			// once per section, only when method == gradient. Kernel size is
			// theme-driven; 3 (radius 1) keeps fine features, 5 (radius 2)
			// gives a smoother overall shape at the cost of thin details.
			std::vector<float> density_smooth;
			if (norm_src == theme::vxl_normals_computed && norm_method == theme::vxl_method_gradient)
			{
				const int kernel_size = (theme::vxl_normals_kernel() == theme::vxl_kernel_5) ? 5 : 3;
				const int radius = kernel_size / 2;
				// Gaussian weights (normalized) — match Pascal's triangle row
				// for radius=1 ([1,2,1]/4) and radius=2 ([1,4,6,4,1]/16). One
				// switch keeps both kernels tight.
				float w[5] = { 0 };
				int kw = 0;
				if (kernel_size == 3) { w[0] = 1.f/4; w[1] = 2.f/4; w[2] = 1.f/4; kw = 3; }
				else                  { w[0] = 1.f/16; w[1] = 4.f/16; w[2] = 6.f/16; w[3] = 4.f/16; w[4] = 1.f/16; kw = 5; }
				const size_t n_voxels = static_cast<size_t>(cx) * cy * cz;
				std::vector<float> a(n_voxels), b(n_voxels);
				// Seed `a` from occupancy.
				#pragma omp parallel for schedule(static)
				for (int idx = 0; idx < static_cast<int>(n_voxels); idx++)
					a[idx] = occ[idx] ? 1.0f : 0.0f;
				auto clamp_i = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
				// Blur X: a -> b
				#pragma omp parallel for schedule(static)
				for (int z = 0; z < cz; z++)
				{
					for (int y = 0; y < cy; y++)
					{
						const size_t row_base = static_cast<size_t>(z) * cx * cy + static_cast<size_t>(y) * cx;
						for (int x = 0; x < cx; x++)
						{
							float s = 0;
							for (int k = 0; k < kw; k++)
							{
								int xs = clamp_i(x + k - radius, 0, cx - 1);
								s += w[k] * a[row_base + xs];
							}
							b[row_base + x] = s;
						}
					}
				}
				// Blur Y: b -> a
				#pragma omp parallel for schedule(static)
				for (int z = 0; z < cz; z++)
				{
					const size_t plane_base = static_cast<size_t>(z) * cx * cy;
					for (int y = 0; y < cy; y++)
					{
						for (int x = 0; x < cx; x++)
						{
							float s = 0;
							for (int k = 0; k < kw; k++)
							{
								int ys = clamp_i(y + k - radius, 0, cy - 1);
								s += w[k] * b[plane_base + static_cast<size_t>(ys) * cx + x];
							}
							a[plane_base + static_cast<size_t>(y) * cx + x] = s;
						}
					}
				}
				// Blur Z: a -> density_smooth
				density_smooth.assign(n_voxels, 0.0f);
				#pragma omp parallel for schedule(static)
				for (int z = 0; z < cz; z++)
				{
					for (int y = 0; y < cy; y++)
					{
						const size_t row_base = static_cast<size_t>(y) * cx;
						for (int x = 0; x < cx; x++)
						{
							float s = 0;
							for (int k = 0; k < kw; k++)
							{
								int zs = clamp_i(z + k - radius, 0, cz - 1);
								s += w[k] * a[static_cast<size_t>(zs) * cx * cy + row_base + x];
							}
							density_smooth[static_cast<size_t>(z) * cx * cy + row_base + x] = s;
						}
					}
				}
			}
			m_vxl_cloud.reserve(m_vxl_cloud.size() + locals.size());
			for (const auto& lv : locals)
			{
				const int x = lv.lx, y = lv.ly, z = lv.lz;
				// Place voxels using the section's mins/maxs as the local
				// origin. Voxel index 0 maps to st.{x,y,z}_min_scale, voxel
				// index (size-1) maps near st.{x,y,z}_max_scale - sectionScale.
				// This mirrors Vengi's renderer applying translate(wm,
				// -pivot * dimensions) where pivot = -mins/(maxs-mins). It's
				// how per-section authored positions (rotor above body) are
				// expressed in the file, since the static transform is
				// typically identity for stock VXLs.
				double lx = st.x_min_scale + (x + 0.5) * sx;
				double ly = st.y_min_scale + (y + 0.5) * sy;
				double lz = st.z_min_scale + (z + 0.5) * sz;
				double wx = tm[0][0] * lx + tm[0][1] * ly + tm[0][2] * lz + tm[0][3];
				double wy = tm[1][0] * lx + tm[1][1] * ly + tm[1][2] * lz + tm[1][3];
				double wz = tm[2][0] * lx + tm[2][1] * ly + tm[2][2] * lz + tm[2][3];
				// Camera-facing convention: flip Y after the section transform
				// so the model presents its front at yaw=0. Applying the flip
				// post-transform keeps tm's rotation/translation columns in
				// their authored axes — important so HVA matrices compose
				// correctly with sibling sections.
				wy = -wy;
				// Local-space normal — branches on user's preference.
				float lnx = 0.0f, lny = 0.0f, lnz = 0.0f;
				if (norm_src == theme::vxl_normals_file)
				{
					// Westwood normal-table lookup. Pre-decoded direction
					// vectors already in voxel-local space.
					xcc_vxl_normals::vec3f n = xcc_vxl_normals::lookup(normal_type, lv.normal_idx);
					lnx = n.x; lny = n.y; lnz = n.z;
				}
				else if (norm_method == theme::vxl_method_gradient)
				{
					// Central-difference gradient of the smoothed density
					// field, computed in the pre-pass above. Reads outside
					// the volume clamp to the edge. The gradient points from
					// high density (inside) toward low (outside), which is
					// exactly the outward surface normal.
					auto sample = [&](int xx, int yy, int zz) {
						if (xx < 0) xx = 0; else if (xx >= cx) xx = cx - 1;
						if (yy < 0) yy = 0; else if (yy >= cy) yy = cy - 1;
						if (zz < 0) zz = 0; else if (zz >= cz) zz = cz - 1;
						return density_smooth[static_cast<size_t>(zz) * cx * cy
							+ static_cast<size_t>(yy) * cx + static_cast<size_t>(xx)];
					};
					lnx = sample(x - 1, y, z) - sample(x + 1, y, z);
					lny = sample(x, y - 1, z) - sample(x, y + 1, z);
					lnz = sample(x, y, z - 1) - sample(x, y, z + 1);
					if (lnx == 0.0f && lny == 0.0f && lnz == 0.0f)
						lnz = 1.0f;
				}
				else if (norm_method == theme::vxl_method_weighted)
				{
					// Vengi-style 26-neighbor: sum *filled* neighbor offsets
					// weighted by 1/distance (face=1, edge=1/sqrt(2),
					// corner=1/sqrt(3)), then sign-flip so the result points
					// outward. Resolves diagonals the 6-neighbor sum can't.
					auto filled = [&](int xx, int yy, int zz) {
						if (xx < 0 || xx >= cx || yy < 0 || yy >= cy || zz < 0 || zz >= cz)
							return false;
						return occ[occ_idx(xx, yy, zz)] != 0;
					};
					const float w_edge = 0.70710678f;	// 1/sqrt(2)
					const float w_corner = 0.57735027f;	// 1/sqrt(3)
					for (int dz = -1; dz <= 1; dz++)
					{
						for (int dy = -1; dy <= 1; dy++)
						{
							for (int dx = -1; dx <= 1; dx++)
							{
								if (dx == 0 && dy == 0 && dz == 0) continue;
								if (!filled(x + dx, y + dy, z + dz)) continue;
								const int abs_sum = std::abs(dx) + std::abs(dy) + std::abs(dz);
								const float w = (abs_sum == 1) ? 1.0f
									: (abs_sum == 2 ? w_edge : w_corner);
								lnx -= dx * w;	// sign-flip: filled neighbors push normal away
								lny -= dy * w;
								lnz -= dz * w;
							}
						}
					}
					if (lnx == 0.0f && lny == 0.0f && lnz == 0.0f)
						lnz = 1.0f;
				}
				else
				{
					// Basic: legacy 6-neighbor empty-side sum.
					auto empty = [&](int xx, int yy, int zz) {
						if (xx < 0 || xx >= cx || yy < 0 || yy >= cy || zz < 0 || zz >= cz)
							return true;
						return occ[occ_idx(xx, yy, zz)] == 0;
					};
					if (empty(x - 1, y, z)) lnx -= 1.0f;
					if (empty(x + 1, y, z)) lnx += 1.0f;
					if (empty(x, y - 1, z)) lny -= 1.0f;
					if (empty(x, y + 1, z)) lny += 1.0f;
					if (empty(x, y, z - 1)) lnz -= 1.0f;
					if (empty(x, y, z + 1)) lnz += 1.0f;
					if (lnx == 0.0f && lny == 0.0f && lnz == 0.0f)
						lnz = 1.0f;
				}
				float wnx = tm[0][0] * lnx + tm[0][1] * lny + tm[0][2] * lnz;
				float wny = tm[1][0] * lnx + tm[1][1] * lny + tm[1][2] * lnz;
				float wnz = tm[2][0] * lnx + tm[2][1] * lny + tm[2][2] * lnz;
				wny = -wny;
				float nlen = std::sqrt(wnx * wnx + wny * wny + wnz * wnz);
				if (nlen > 1e-6f) { wnx /= nlen; wny /= nlen; wnz /= nlen; }
				else              { wnx = 0; wny = 0; wnz = 1; }
				t_vxl_voxel v{ wx, wy, wz, lv.color, lv.normal_idx, wnx, wny, wnz, 0 };
				m_vxl_cloud.push_back(v);
			}
			// Pass 3 — Ambient occlusion bake.
			//
			// Two mutually-exclusive strategies write to the same per-voxel
			// `ao` byte. The splat / shade pass downstream doesn't know which
			// produced the value.
			//
			//   ao_method_contact     3×3×3 neighborhood walk per voxel.
			//                         Distance-weighted (Soft) or bit-count
			//                         (Hard) over face / edge / corner
			//                         neighbors. ~free at file load.
			//                         Captures only immediate-neighbor
			//                         occlusion (contact shadows under
			//                         edges + concave corners).
			//
			//   ao_method_hemisphere  Cosine-weighted Hammersley rays + 3D
			//                         DDA up to ~8 cells. Captures
			//                         medium-range cavities and undercuts
			//                         the 3×3×3 neighborhood can't see.
			//                         Slower file open (Quality combo tunes
			//                         ray count / range).
			//
			// Both paths read `occ` (populated above) and write to the
			// `locals.size()` cloud entries this section just appended. AO
			// uses its own 6-neighbor local normal for ray orientation —
			// independent of the shading normal source/method (orientation
			// only needs "outward enough", and decoupling avoids rebakes
			// when shading normals change).
			if (theme::vxl_ao_method_v() == theme::ao_method_contact)
			{
				// Contact AO. For each voxel, walk the 26 neighbors of the
				// 3×3×3 cube (excluding the center, which is the voxel
				// itself). Score by distance class:
				//   face   (6 cells, dist=1)          weight 1.000
				//   edge   (12 cells, dist=sqrt 2)    weight 1/sqrt(2) ≈ 0.707
				//   corner (8 cells, dist=sqrt 3)     weight 1/sqrt(3) ≈ 0.577
				// Soft = weighted sum / max possible. Hard = filled count / 26.
				// Result mapped to byte and clamped.
				const size_t cloud_start = m_vxl_cloud.size() - locals.size();
				const theme::vxl_ao_contact_falloff falloff = theme::vxl_ao_contact_falloff_v();
				// Pre-tabulated weight per (dx,dy,dz) offset (taxicab class).
				// Index by |dx|+|dy|+|dz| — 1=face, 2=edge, 3=corner.
				const float w_face   = 1.0f;
				const float w_edge   = 0.7071067812f;
				const float w_corner = 0.5773502692f;
				const float max_weighted = 6.0f * w_face + 12.0f * w_edge + 8.0f * w_corner;
				#pragma omp parallel for schedule(static)
				for (int li = 0; li < static_cast<int>(locals.size()); li++)
				{
					const local_voxel& lv = locals[li];
					int filled_count = 0;
					float filled_weight = 0.0f;
					for (int dz = -1; dz <= 1; dz++)
					for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++)
					{
						if (dx == 0 && dy == 0 && dz == 0) continue;
						const int nx2 = lv.lx + dx;
						const int ny2 = lv.ly + dy;
						const int nz2 = lv.lz + dz;
						// Treat out-of-bounds cells as empty (open air) so
						// voxels at the section boundary don't get falsely
						// darkened.
						if (nx2 < 0 || nx2 >= cx || ny2 < 0 || ny2 >= cy ||
						    nz2 < 0 || nz2 >= cz) continue;
						if (occ[occ_idx(nx2, ny2, nz2)] == 0) continue;
						filled_count++;
						const int taxicab = std::abs(dx) + std::abs(dy) + std::abs(dz);
						const float w = (taxicab == 1) ? w_face :
						                (taxicab == 2) ? w_edge : w_corner;
						filled_weight += w;
					}
					float ratio;
					if (falloff == theme::ao_contact_soft)
						ratio = filled_weight / max_weighted;
					else
						ratio = static_cast<float>(filled_count) / 26.0f;
					if (ratio < 0.0f) ratio = 0.0f;
					else if (ratio > 1.0f) ratio = 1.0f;
					int ao_byte = static_cast<int>(ratio * 255.0f + 0.5f);
					if (ao_byte < 0) ao_byte = 0;
					else if (ao_byte > 255) ao_byte = 255;
					m_vxl_cloud[cloud_start + li].ao = static_cast<unsigned char>(ao_byte);
				}
			}
			else
			{
			// Quality tiers (theme::vxl_ao_quality_v):
			//   Low    16 rays, 5-cell range  — ~current speed
			//   High   32 rays, 8-cell range  — default
			//   Ultra  64 rays, 8-cell range  — best
			{
				const size_t cloud_start = m_vxl_cloud.size() - locals.size();
				int ray_count = 32;
				int max_steps = 8;
				switch (theme::vxl_ao_quality_v())
				{
				case theme::ao_q_low:   ray_count = 16; max_steps = 5; break;
				case theme::ao_q_high:  ray_count = 32; max_steps = 8; break;
				case theme::ao_q_ultra: ray_count = 64; max_steps = 8; break;
				}
				// Cosine-weighted Hammersley directions in tangent space
				// (z-up hemisphere), generated once per section. The
				// Hammersley sequence is naturally stratified — no banding
				// from a small fixed direction table reused across voxels.
				// u1 = (i + 0.5) / N, u2 = van der Corput base-2 of i.
				// Malley's method: r = sqrt(u1), phi = 2*pi*u2,
				// dir = (r cos phi, r sin phi, sqrt(1 - u1)).
				std::vector<std::array<float, 3>> ao_dirs(ray_count);
				for (int i = 0; i < ray_count; i++)
				{
					const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(ray_count);
					// van der Corput radical inverse, base 2.
					unsigned int bits = static_cast<unsigned int>(i);
					bits = (bits << 16) | (bits >> 16);
					bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
					bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
					bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
					bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
					const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;
					const float r = std::sqrt(u1);
					const float phi = 6.28318530717958647692f * u2;
					ao_dirs[i][0] = r * std::cos(phi);
					ao_dirs[i][1] = r * std::sin(phi);
					ao_dirs[i][2] = std::sqrt(1.0f - u1);
				}
				const float inv_max_steps = 1.0f / static_cast<float>(max_steps);
				const float scale_to_byte = 255.0f / static_cast<float>(ray_count);
				#pragma omp parallel for schedule(static)
				for (int li = 0; li < static_cast<int>(locals.size()); li++)
				{
					const local_voxel& lv = locals[li];
					// AO orientation normal: cheap 6-neighbor empty-side sum.
					// Fall back to +Z if fully surrounded.
					float nx = 0, ny = 0, nz = 0;
					auto empty_local = [&](int xx, int yy, int zz) {
						if (xx < 0 || xx >= cx || yy < 0 || yy >= cy || zz < 0 || zz >= cz)
							return true;
						return occ[occ_idx(xx, yy, zz)] == 0;
					};
					if (empty_local(lv.lx - 1, lv.ly, lv.lz)) nx -= 1.0f;
					if (empty_local(lv.lx + 1, lv.ly, lv.lz)) nx += 1.0f;
					if (empty_local(lv.lx, lv.ly - 1, lv.lz)) ny -= 1.0f;
					if (empty_local(lv.lx, lv.ly + 1, lv.lz)) ny += 1.0f;
					if (empty_local(lv.lx, lv.ly, lv.lz - 1)) nz -= 1.0f;
					if (empty_local(lv.lx, lv.ly, lv.lz + 1)) nz += 1.0f;
					float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
					if (nlen > 1e-6f) { nx /= nlen; ny /= nlen; nz /= nlen; }
					else              { nx = 0; ny = 0; nz = 1; }
					// Tangent frame (T, B, N) — rotates the z-up hemisphere
					// directions to align with the voxel's outward normal.
					float tx, ty, tz;
					if (std::fabs(nz) < 0.999f)
					{
						tx = -ny; ty = nx; tz = 0.0f;	// cross(N, +Z)
					}
					else
					{
						tx = 1.0f; ty = 0.0f; tz = 0.0f;	// cross(N, +X) for poles
					}
					float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
					if (tlen > 1e-6f) { tx /= tlen; ty /= tlen; tz /= tlen; }
					else              { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
					// B = N x T
					float bx = ny * tz - nz * ty;
					float by = nz * tx - nx * tz;
					float bz = nx * ty - ny * tx;
					// IMPORTANT: skip the originating voxel cell during the DDA.
					// `occ[lv.lx, lv.ly, lv.lz] == 1` (the voxel itself is
					// occupied), and starting the ray at +0.5 along the
					// direction often leaves us still inside that cell for
					// grazing rays. Without the skip, every ray hits at step
					// 0 → occlusion = ray_count → every voxel reads as fully
					// occluded → uniform darkening with no AO contrast.
					const int origin_x = lv.lx;
					const int origin_y = lv.ly;
					const int origin_z = lv.lz;
					float occlusion = 0.0f;
					for (int r = 0; r < ray_count; r++)
					{
						const float dx = ao_dirs[r][0];
						const float dy = ao_dirs[r][1];
						const float dz = ao_dirs[r][2];
						const float rx = tx * dx + bx * dy + nx * dz;
						const float ry = ty * dx + by * dy + ny * dz;
						const float rz = tz * dx + bz * dy + nz * dz;
						// 3D DDA seeded at the voxel center. The first
						// occupied cell *other than the origin* is the hit.
						float px = origin_x + 0.5f;
						float py = origin_y + 0.5f;
						float pz = origin_z + 0.5f;
						const int step_x = rx > 0 ? 1 : (rx < 0 ? -1 : 0);
						const int step_y = ry > 0 ? 1 : (ry < 0 ? -1 : 0);
						const int step_z = rz > 0 ? 1 : (rz < 0 ? -1 : 0);
						const float inv_rx = (rx != 0) ? 1.0f / std::fabs(rx) : 1e30f;
						const float inv_ry = (ry != 0) ? 1.0f / std::fabs(ry) : 1e30f;
						const float inv_rz = (rz != 0) ? 1.0f / std::fabs(rz) : 1e30f;
						int ix = origin_x;
						int iy = origin_y;
						int iz = origin_z;
						auto frac_pos = [](float f, int step) {
							float fr = f - std::floor(f);
							return (step > 0) ? (1.0f - fr) : (step < 0 ? fr : 1e30f);
						};
						float t_x = frac_pos(px, step_x) * inv_rx;
						float t_y = frac_pos(py, step_y) * inv_ry;
						float t_z = frac_pos(pz, step_z) * inv_rz;
						int hit_step = -1;
						// hit_step counts non-origin cells visited along the
						// ray; the originating cell is implicitly step -1.
						for (int s = 0; s < max_steps; s++)
						{
							// Advance one cell along the dominant axis first.
							// That way we never test the origin cell — only
							// cells strictly off the source voxel get an
							// occupancy check.
							if (t_x <= t_y && t_x <= t_z)
							{
								ix += step_x;
								t_x += inv_rx;
							}
							else if (t_y <= t_z)
							{
								iy += step_y;
								t_y += inv_ry;
							}
							else
							{
								iz += step_z;
								t_z += inv_rz;
							}
							if (ix < 0 || ix >= cx || iy < 0 || iy >= cy || iz < 0 || iz >= cz)
								break;
							if (occ[occ_idx(ix, iy, iz)] != 0)
							{
								hit_step = s;
								break;
							}
						}
						if (hit_step >= 0)
						{
							// Linear falloff. hit_step=0 (first non-origin
							// cell occupied) contributes 1.0; hit_step at
							// max_steps-1 contributes ~1/max_steps.
							occlusion += 1.0f - static_cast<float>(hit_step) * inv_max_steps;
						}
					}
					// Linear ratio → byte. The bake produces a natural
					// distribution where open surfaces have low ratios
					// (rays escape the section bounds, no hit) and deep
					// crevices have high ratios (most rays hit nearby
					// occluders). A gamma curve here is tempting but works
					// against the desired feel — at strength=100 we want
					// open faces near full brightness and only enclosed
					// voxels truly dark; that mapping is `1 - ratio` in
					// the shade pass, which already does the right thing
					// with a linear ratio. If the spread feels too gentle
					// later, add `ratio = std::pow(ratio, gamma)` here
					// with gamma in (1.0, 2.0]; gamma > 1 darkens crevices
					// harder.
					float ratio = occlusion / static_cast<float>(ray_count);
					if (ratio < 0.0f) ratio = 0.0f; else if (ratio > 1.0f) ratio = 1.0f;
					int ao_byte = static_cast<int>(ratio * 255.0f + 0.5f);
					if (ao_byte < 0) ao_byte = 0; else if (ao_byte > 255) ao_byte = 255;
					m_vxl_cloud[cloud_start + li].ao = static_cast<unsigned char>(ao_byte);
				}
			}
			}
		}
		} // end for (auto& src : sources)

		if (m_vxl_cloud.empty())
			return;

		// Are any sources HVA-driven? If so the cloud rebuilds per frame and
		// we need the worst-case bound across all keyframes of every source,
		// otherwise the auto-fit scale would pulse as rotated bounding spheres
		// expand/contract. With no HVA anywhere the cloud is static — use the
		// quick max-radius walk over the merged cloud (still covers multi-part
		// turret-on-body geometry).
		bool any_hva = false;
		for (const auto& s : sources) if (s.hva_ok) { any_hva = true; break; }

		if (any_hva && m_hva_vxl_half == 0)
		{
			double worst_r2 = 0;
			for (auto& src : sources)
			{
				Cvxl_file& f = src.vxl;
				const int n_sections = f.get_c_section_headers();
				// For non-HVA sources in a mixed scene, the bound contribution
				// is just the static pose (single "keyframe" at identity tm).
				const int n_kf = src.hva_ok ? src.hva.get_c_frames() : 1;
				for (int kf = 0; kf < n_kf; kf++)
				{
					for (int i = 0; i < n_sections; i++)
					{
						const t_vxl_section_tailer& st = *f.get_section_tailer(i);
						const int cx = st.cx, cy = st.cy, cz = st.cz;
						double sx = (st.x_max_scale - st.x_min_scale) / std::max(1, cx);
						double sy = (st.y_max_scale - st.y_min_scale) / std::max(1, cy);
						double sz = (st.z_max_scale - st.z_min_scale) / std::max(1, cz);
						float tm[3][4];
						memcpy(tm, st.transform, sizeof(tm));
						if (src.hva_ok)
						{
							int hs_match = -1;
							const char* vid = f.get_section_header(i)->id;
							for (int hs = 0; hs < src.hva.get_c_sections(); hs++)
							{
								if (!strncmp(src.hva.get_section_id(hs), vid, 16))
								{
									hs_match = hs;
									break;
								}
							}
							if (hs_match < 0 && i < src.hva.get_c_sections())
								hs_match = i;
							if (hs_match >= 0)
							{
								const float* m = src.hva.get_transform_matrix(hs_match, kf);
								memcpy(tm, m, sizeof(tm));
								tm[0][3] *= st.scale;
								tm[1][3] *= st.scale;
								tm[2][3] *= st.scale;
							}
						}
						// Walk spans to find each occupied voxel's local coord
						// and project through tm. Skip empty cells.
						int j = 0;
						for (int y = 0; y < cy; y++)
						{
							for (int x = 0; x < cx; x++)
							{
								const byte* r = f.get_span_data(i, j++);
								if (!r) continue;
								int z = 0;
								while (z < cz)
								{
									z += *r++;
									int c = *r++;
									while (c--)
									{
										double lx = st.x_min_scale + (x + 0.5) * sx;
										double ly = st.y_min_scale + (y + 0.5) * sy;
										double lz = st.z_min_scale + (z + 0.5) * sz;
										double wx = tm[0][0] * lx + tm[0][1] * ly + tm[0][2] * lz + tm[0][3];
										double wy = tm[1][0] * lx + tm[1][1] * ly + tm[1][2] * lz + tm[1][3];
										double wz = tm[2][0] * lx + tm[2][1] * ly + tm[2][2] * lz + tm[2][3];
										wy = -wy;
										double r2 = wx * wx + wy * wy + wz * wz;
										if (r2 > worst_r2) worst_r2 = r2;
										r += 2;
										z++;
									}
									r++;
								}
							}
						}
					}
				}
			}
			const double bound = std::sqrt(worst_r2);
			m_hva_vxl_half = std::max(8, static_cast<int>(std::ceil(bound)) + 2);
		}

		if (any_hva)
		{
			m_vxl_half = m_hva_vxl_half;
		}
		else
		{
			double max_r2 = 0;
			for (const auto& v : m_vxl_cloud)
			{
				double r2 = v.x * v.x + v.y * v.y + v.z * v.z;
				if (r2 > max_r2) max_r2 = r2;
			}
			const double bound = std::sqrt(max_r2);
			m_vxl_half = std::max(8, static_cast<int>(std::ceil(bound)) + 2);
		}
		m_player_cx = 2 * m_vxl_half;
		m_player_cy = 2 * m_vxl_half;
		// Timeline length = max across all sources. A part with fewer keyframes
		// than the timeline clamps at its last keyframe (handled inside
		// resolve_kf above), so a static turret over an animated body works
		// (and vice versa).
		int n_kf_max = 0;
		for (const auto& s : sources)
			if (s.hva_ok) n_kf_max = std::max(n_kf_max, s.hva.get_c_frames());
		m_player_cf = (n_kf_max <= 1) ? 1 : (n_kf_max - 1) * c_HVA_INTER + 1;
	}
}

void CXCCFileView::notify_palette_changed()
{
	if (!m_player_mode || !m_is_open)
	{
		// Grid mode: OnDraw -> per-format load_color_table picks up the
		// new palette on the invalidate CMainFrame::set_palette already
		// issued. Nothing to do here.
		return;
	}
	// Player mode: rebuild the color table from the new default palette.
	// load_color_table bumps m_player_bgra_version (so VXL BGRA cache
	// misses) and, for SHP/WSA, re-prefills the per-frame BGRA cache.
	// VXL synthetic-shading path keeps its splat (buf holds the original
	// v.color and the BGRA composite picks up the new color table on
	// cache miss). VPL path REBUILDS the splat: buf holds vpl_baked
	// indices (redmean nearest-color against the *old* palette), so
	// stale baked indices look wrong under the new color table. The
	// splat cache key now includes vpl_pal_version (= m_player_bgra_version),
	// so the bgra_version bump above forces a fresh splat + re-bake.
	load_color_table(get_default_palette(), true);
	Invalidate();
}

void CXCCFileView::player_enter()
{
	if (!is_playable_file())
		return;
	player_decode_frames();
	if (m_player_cf <= 0)
		return;
	m_player_mode = true;
	m_player_frame = 0;
	// Don't auto-start playback. The animation begins when the user clicks
	// Play (or hits Space, which the Play button picks up via its mnemonic).
	// Avoids the "open a SHP, fans rev for the first-playthrough cache fill,
	// user didn't even ask for it" surprise.
	m_player_playing = false;
	m_player_zoom_pct = 0;
	// Open at native 100% for every player kind (VXL/SHP/WSA/PCX/...). The
	// default-fit path scales small sprites up to fill the pane ("zoom in"),
	// which the user doesn't want — pin Native on so the image opens 1:1.
	// The Native button / Ctrl+wheel still let the user deviate per view; it
	// re-defaults to 100% on the next open.
	m_player_native_size = true;
	m_player_pan_x = m_player_pan_y = 0;
	m_player_panning = false;
	if (m_ft == ft_vxl)
	{
		m_vxl_yaw = 0.0;
		m_vxl_pitch = 30.0 * 3.14159265358979323846 / 180.0;
		m_vxl_dragging = false;
	}
	if (!m_player_controls_created)
	{
		CRect r(0, 0, 1, 1);
		m_player_play.Create("Pause", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_PLAY);
		m_player_reverse.Create("<<", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, r, this, IDC_PLAYER_REVERSE);
		m_player_grid.Create("Back", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_GRID);
		m_player_native.Create("Native", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, r, this, IDC_PLAYER_NATIVE);
		m_player_screenshot.Create("Screenshot", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_SCREENSHOT);
		m_player_turntable.Create("Record", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_TURNTABLE);
		m_player_slider.Create(WS_CHILD | TBS_HORZ | TBS_NOTICKS, r, this, IDC_PLAYER_SLIDER);
		m_player_label.Create("", WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, r, this, IDC_PLAYER_FRAME_LABEL);
		m_player_fps_label.Create("FPS", WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, r, this, IDC_PLAYER_FPS_LABEL);
		m_player_fps_edit.Create(WS_CHILD | ES_NUMBER | ES_RIGHT | WS_BORDER, r, this, IDC_PLAYER_FPS_EDIT);
		m_player_fps_spin.Create(WS_CHILD | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS, r, this, IDC_PLAYER_FPS_SPIN);
		m_player_fps_spin.SetBuddy(&m_player_fps_edit);
		m_player_fps_spin.SetRange(1, 120);
		m_player_play.SetFont(&m_font);
		m_player_reverse.SetFont(&m_font);
		m_player_grid.SetFont(&m_font);
		m_player_native.SetFont(&m_font);
		m_player_screenshot.SetFont(&m_font);
		m_player_turntable.SetFont(&m_font);
		m_player_label.SetFont(&m_font);
		m_player_fps_label.SetFont(&m_font);
		m_player_fps_edit.SetFont(&m_font);
		theme::apply_window(m_player_play.GetSafeHwnd());
		theme::apply_window(m_player_reverse.GetSafeHwnd());
		theme::apply_window(m_player_grid.GetSafeHwnd());
		theme::apply_window(m_player_native.GetSafeHwnd());
		theme::apply_window(m_player_screenshot.GetSafeHwnd());
		theme::apply_window(m_player_turntable.GetSafeHwnd());
		theme::apply_window(m_player_slider.GetSafeHwnd());
		theme::apply_window(m_player_label.GetSafeHwnd());
		theme::apply_window(m_player_fps_label.GetSafeHwnd());
		theme::apply_window(m_player_fps_edit.GetSafeHwnd());
		theme::apply_window(m_player_fps_spin.GetSafeHwnd());
		// Wire dark trackbar custom-draw for the player slider. The slider
		// is parented to the view (not a dialog), so apply_dialog doesn't
		// reach it — install the parent-side WM_NOTIFY interceptor on the
		// view directly. Idempotent so repeated player_enter calls are fine.
		theme::install_trackbar_parent_subclass(GetSafeHwnd());

		m_player_shadows.Create("Shadows", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, r, this, IDC_PLAYER_SHADOWS);
		// 3-state pushbutton: click cycles Color → Checker → Pane bg.
		// Label is updated to reflect current state in player_show_for_kind.
		m_player_bg.Create("BG", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_BG);
		for (int i = 0; i < 8; i++)
		{
			m_player_side[i].Create("", WS_CHILD | BS_OWNERDRAW, r, this, IDC_PLAYER_SIDE0 + i);
		}
		m_player_side_custom.Create("", WS_CHILD | BS_OWNERDRAW, r, this, IDC_PLAYER_SIDE_CUSTOM);
		// Game Grid: 3-state pushbutton (was a combobox). Click cycles
		// off -> TS -> RA2; label reflects the current state, starting "Grid".
		// Pushbutton matches the BG button idiom and themes via apply_window —
		// no dropdown listbox to theme, so the old subclass_combobox plumbing
		// is gone.
		m_player_iso_grid.Create("Grid", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_GRID_SEL);
		m_player_shadows.SetFont(&m_font);
		m_player_bg.SetFont(&m_font);
		m_player_iso_grid.SetFont(&m_font);
		theme::apply_window(m_player_shadows.GetSafeHwnd());
		theme::apply_window(m_player_bg.GetSafeHwnd());
		theme::apply_window(m_player_iso_grid.GetSafeHwnd());
		player_update_grid_label();
		for (int i = 0; i < 8; i++)
			theme::apply_window(m_player_side[i].GetSafeHwnd());
		theme::apply_window(m_player_side_custom.GetSafeHwnd());

		// VXL parallel set of side-color swatches. Same look + behavior as the
		// SHP set; kept distinct so VXL state doesn't leak into SHP previews.
		for (int i = 0; i < 8; i++)
		{
			m_vxl_side[i].Create("", WS_CHILD | BS_OWNERDRAW, r, this, IDC_VXL_SIDE0 + i);
			theme::apply_window(m_vxl_side[i].GetSafeHwnd());
		}
		m_vxl_side_custom.Create("", WS_CHILD | BS_OWNERDRAW, r, this, IDC_VXL_SIDE_CUSTOM);
		theme::apply_window(m_vxl_side_custom.GetSafeHwnd());

		// HVA load button (VXL-only). Stays hidden for SHP/WSA.
		m_vxl_hva_load.Create("Load HVA...", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_VXL_HVA_LOAD);
		m_vxl_hva_load.SetFont(&m_font);
		theme::apply_window(m_vxl_hva_load.GetSafeHwnd());

		// Loop checkbox: only relevant when an HVA is driving multi-frame
		// playback. Default on (mirrors Vengi's animation panel).
		m_vxl_hva_loop.Create("Loop", WS_CHILD | BS_AUTOCHECKBOX, r, this, IDC_VXL_HVA_LOOP);
		m_vxl_hva_loop.SetFont(&m_font);
		theme::apply_window(m_vxl_hva_loop.GetSafeHwnd());

		// VPL load button (VXL-only). Lets the user pick voxels.vpl manually
		// when auto-detection didn't find one in the source MIX or folder.
		m_vxl_vpl_load.Create("Load VPL...", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_VXL_VPL_LOAD);
		m_vxl_vpl_load.SetFont(&m_font);
		theme::apply_window(m_vxl_vpl_load.GetSafeHwnd());

		m_player_controls_created = true;
	}
	const bool vxl = (m_ft == ft_vxl);
	// VXL playback is hidden by default, but a loaded HVA turns the VXL into
	// a multi-frame animation — show the transport row so the user can scrub.
	const bool vxl_hva = vxl && m_hva_loaded && m_player_cf > 1;
	m_player_slider.SetRange(0, std::max(0, m_player_cf - 1));
	m_player_slider.SetPos(0);
	m_player_fps_edit.SetWindowText(n(m_player_fps).c_str());
	m_player_fps_spin.SetPos(m_player_fps);
	const int playback_show = (!vxl || vxl_hva) ? SW_SHOW : SW_HIDE;
	m_player_play.ShowWindow(playback_show);
	m_player_reverse.ShowWindow(playback_show);
	m_player_reverse.SetCheck(m_player_reverse_dir ? BST_CHECKED : BST_UNCHECKED);
	m_player_grid.ShowWindow(SW_SHOW);
	m_player_native.ShowWindow(SW_SHOW);
	m_player_screenshot.ShowWindow(SW_SHOW);
	// Record lives on two different rows depending on file kind:
	//   - VXL: upper row next to Load VPL... (turntable + HVA + downscale).
	//   - SHP/WSA with cf > 1: bottom transport row after Native (frame
	//     capture into GIF / PNG sequence).
	// Single-frame SHPs hide it (nothing to record).
	const bool shp_animated = !vxl && m_player_cf > 1;
	m_player_turntable.ShowWindow((vxl || shp_animated) ? SW_SHOW : SW_HIDE);
	m_player_native.SetCheck(m_player_native_size ? BST_CHECKED : BST_UNCHECKED);
	m_player_slider.ShowWindow(playback_show);
	m_player_label.ShowWindow(playback_show);
	m_player_fps_label.ShowWindow(playback_show);
	m_player_fps_edit.ShowWindow(playback_show);
	m_player_fps_spin.ShowWindow(playback_show);
	// Shadow/BG/SideColor controls are SHP-family only (not VXL).
	const int shp_show = vxl ? SW_HIDE : SW_SHOW;
	// Pair-frame mode (state 1) needs even cf; RA1 mode (state 2) works on
	// any SHP (the 0xFF magic-marker pixels live inside the body frame).
	const bool can_pair_shadows = !vxl && m_player_cf >= 2 && (m_player_cf % 2) == 0;
	m_player_shadows.ShowWindow(shp_show);
	m_player_shadows.EnableWindow(!vxl ? TRUE : FALSE);
	if (vxl)
	{
		m_player_shadows_on = false;
		m_player_shadows_ra1 = false;
		m_player_shadows_state = 0;
		m_player_bg_idx = 0;
	}
	else if (m_player_shadows_state == 1 && !can_pair_shadows)
	{
		// User had pair-mode on but switched to a SHP that doesn't support
		// it. Drop to off rather than leaving stale state.
		m_player_shadows_on = false;
		m_player_shadows_state = 0;
	}
	m_player_shadows.SetCheck((m_player_shadows_state >= 1) ? BST_CHECKED : BST_UNCHECKED);
	m_player_shadows.SetWindowText(
		m_player_shadows_state == 0 ? "Shadows" :
		m_player_shadows_state == 1 ? "Shadows RA2/TS" :
		                              "Shadows RA1");
	// BG toggle applies to both SHP and VXL — palette index 0 = "no voxel" /
	// "transparent" pixel in either case, so the show-bg-vs-checker switch is
	// meaningful for both.
	m_player_bg.ShowWindow(SW_SHOW);
	player_update_bg_label();
	for (int i = 0; i < 8; i++)
		m_player_side[i].ShowWindow(shp_show);
	m_player_side_custom.ShowWindow(shp_show);
	// VXL parallel: opposite gating — only shown when viewing a VXL.
	const int vxl_show = vxl ? SW_SHOW : SW_HIDE;
	for (int i = 0; i < 8; i++)
		m_vxl_side[i].ShowWindow(vxl_show);
	m_vxl_side_custom.ShowWindow(vxl_show);
	m_vxl_hva_load.ShowWindow(vxl_show);
	m_vxl_vpl_load.ShowWindow(vxl_show);
	// Loop only meaningful while an HVA is supplying multiple frames.
	const int hva_loop_show = vxl_hva ? SW_SHOW : SW_HIDE;
	m_vxl_hva_loop.ShowWindow(hva_loop_show);
	m_vxl_hva_loop.SetCheck(m_hva_loop ? BST_CHECKED : BST_UNCHECKED);
	// Game Grid button shows for both SHP and VXL — the overlay applies in
	// either case (already drawn for VXL via the post-stretch path below).
	m_player_iso_grid.ShowWindow(SW_SHOW);
	player_update_grid_label();
	player_layout_controls();
	player_update_label();
	// Reflect the not-playing default on the button label so the user sees
	// "Play" (and clicking it starts playback).
	if (!vxl && m_player_controls_created)
		m_player_play.SetWindowText("Play");
	// Pre-fill the BGRA cache for every SHP/WSA frame up-front. The cost is
	// the same as the first playthrough used to be, but it's compressed into
	// one moment (under the user's intent of "I'm entering the player") and
	// after this point the timer-driven repaints are pure memcpy + StretchBlt
	// — no fan-rev on first play. VXL is skipped: its source `s` depends on
	// camera, not frame index, and is cached separately at the splat level.
	if (!vxl)
		player_prefill_bgra_cache();
	SetScrollSizes(MM_TEXT, CSize(1, 1));
	load_pal_btn_update_visibility();
	Invalidate();
}

void CXCCFileView::player_exit()
{
	if (m_player_mode)
	{
		KillTimer(1);
		m_player_mode = false;
		m_player_playing = false;
	}
	if (m_player_controls_created)
	{
		m_player_play.ShowWindow(SW_HIDE);
		m_player_reverse.ShowWindow(SW_HIDE);
		m_player_grid.ShowWindow(SW_HIDE);
		m_player_native.ShowWindow(SW_HIDE);
		m_player_screenshot.ShowWindow(SW_HIDE);
		m_player_turntable.ShowWindow(SW_HIDE);
		m_player_slider.ShowWindow(SW_HIDE);
		m_player_label.ShowWindow(SW_HIDE);
		m_player_fps_label.ShowWindow(SW_HIDE);
		m_player_fps_edit.ShowWindow(SW_HIDE);
		m_player_fps_spin.ShowWindow(SW_HIDE);
		m_player_shadows.ShowWindow(SW_HIDE);
		m_player_bg.ShowWindow(SW_HIDE);
		for (int i = 0; i < 8; i++)
			m_player_side[i].ShowWindow(SW_HIDE);
		m_player_side_custom.ShowWindow(SW_HIDE);
		for (int i = 0; i < 8; i++)
			m_vxl_side[i].ShowWindow(SW_HIDE);
		m_vxl_side_custom.ShowWindow(SW_HIDE);
		m_vxl_hva_load.ShowWindow(SW_HIDE);
		m_vxl_hva_loop.ShowWindow(SW_HIDE);
		m_vxl_vpl_load.ShowWindow(SW_HIDE);
		m_player_iso_grid.ShowWindow(SW_HIDE);
	}
	m_player_frames.clear();
	m_vxl_cloud.clear();
	m_vxl_dragging = false;
	if (GetCapture() == this)
		ReleaseCapture();
	m_text_cache_valid = false;
	load_pal_btn_update_visibility();
	Invalidate();
}

void CXCCFileView::player_toggle_play()
{
	m_player_playing = !m_player_playing;
	if (m_player_controls_created)
		m_player_play.SetWindowText(m_player_playing ? "Pause" : "Play");
	if (m_player_playing)
		SetTimer(1, 1000 / std::max(1, m_player_fps), NULL);
	else
		KillTimer(1);
}

void CXCCFileView::player_set_frame(int f)
{
	if (m_player_cf <= 0)
		return;
	int range = m_player_cf;
	if (m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0)
		range = m_player_cf / 2;
	if (f < 0)
		f = range - 1;
	if (f >= range)
		f = 0;
	m_player_frame = f;
	if (m_player_controls_created)
		m_player_slider.SetPos(f);
	player_update_label();
	// VXL+HVA scrubbing: rebuild the point cloud for the new frame and bump
	// m_open_token so the splat cache (keyed on token) rebuilds on next paint.
	if (m_ft == ft_vxl && m_hva_loaded)
	{
		player_decode_frames();
		m_open_token++;
	}
	// Only invalidate the image area above the controls band; the controls paint themselves.
	CRect cr;
	GetClientRect(&cr);
	cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::player_layout_controls()
{
	if (!m_player_controls_created)
		return;
	CRect cr;
	GetClientRect(&cr);
	const int H = 24;
	const int pad = 4;
	const bool vxl = is_vxl_view();
	// Transport sub-row (Play, <<, frame label, FPS, slider) is live for
	// SHP/WSA and for VXL only once an HVA supplies multiple frames. When it's
	// hidden (VXL without HVA) its controls must NOT consume layout width —
	// otherwise Grid/Native and the Game Grid combo float out across the empty
	// slots the hidden controls would have occupied (the reported bug).
	const bool vxl_hva = m_hva_loaded && m_player_cf > 1;
	const bool transport = (!vxl || vxl_hva);
	// Bottom row: transport controls (Play, <<, Grid, Native, slider, FPS).
	int y = cr.bottom - H - pad;
	int x = pad;
	m_player_screenshot.MoveWindow(x, y, 80, H);      x += 80 + pad;
	if (transport)
	{
		m_player_play.MoveWindow(x, y, 60, H);        x += 60 + pad;
		m_player_reverse.MoveWindow(x, y, 30, H);     x += 30 + pad;
	}
	m_player_grid.MoveWindow(x, y, 50, H);            x += 50 + pad;
	m_player_native.MoveWindow(x, y, 96, H);          x += 96 + pad;
	// SHP/WSA Record button slot — only consumes layout space when this is
	// SHP territory. VXL Record lives on the upper VXL row and is sized
	// inside the vxl branch below.
	if (!vxl && m_player_cf > 1)
	{
		m_player_turntable.MoveWindow(x, y, 70, H);   x += 70 + pad;
	}
	if (transport)
	{
		int label_w = 110;
		m_player_label.MoveWindow(x, y, label_w, H); x += label_w + pad;
		int fps_label_w = 26;
		m_player_fps_label.MoveWindow(x, y, fps_label_w, H); x += fps_label_w + 2;
		int fps_w = 44;
		m_player_fps_edit.MoveWindow(x, y, fps_w, H);     x += fps_w + pad;
		int slider_x = x;
		int slider_w = cr.right - slider_x - pad;
		if (slider_w < 60) slider_w = 60;
		m_player_slider.MoveWindow(slider_x, y, slider_w, H);
	}
	if (vxl)
	{
		// VXL without HVA: transport row is hidden, so the iso-grid combo
		// gets parked right after the Native button on the single visible
		// row. With HVA loaded the transport is live and the combo stays on
		// the upper row (placed below) next to the swatches.
		if (!vxl_hva)
		{
			// `x` is the running edge right after Native — the transport
			// advances above were gated out — so the button packs flush.
			m_player_iso_grid.MoveWindow(x, y, 70, H);
		}
		// Upper row: BG toggle + 9 VXL side-color swatches + HVA button
		// (+ iso-grid when HVA is loaded).
		int y2 = y - H - pad;
		int x2 = pad;
		m_player_bg.MoveWindow(x2, y2, 70, H); x2 += 70 + pad;
		const int swatch = H;
		for (int i = 0; i < 8; i++)
		{
			m_vxl_side[i].MoveWindow(x2, y2, swatch, H);
			x2 += swatch + 2;
		}
		m_vxl_side_custom.MoveWindow(x2, y2, swatch, H); x2 += swatch + pad;
		// HVA load button — sits at the right end of the VXL upper row.
		m_vxl_hva_load.MoveWindow(x2, y2, 90, H); x2 += 90 + pad;
		// VPL load button — alongside HVA. Auto-detection covers most cases;
		// the button is the manual override.
		m_vxl_vpl_load.MoveWindow(x2, y2, 90, H); x2 += 90 + pad;
		// Record (turntable) — sits on the VXL upper row right after Load VPL
		// so it's discoverable next to the other VXL-specific controls.
		m_player_turntable.MoveWindow(x2, y2, 70, H); x2 += 70 + pad;
		// Loop checkbox — only visible while an HVA is loaded.
		m_vxl_hva_loop.MoveWindow(x2, y2, 56, H); x2 += 56 + pad;
		if (vxl_hva)
			m_player_iso_grid.MoveWindow(x2, y2, 70, H);
		return;
	}
	// Upper row (SHP family only): Shadows, BG, 8 side-color swatches.
	int y2 = y - H - pad;
	int x2 = pad;
	// Width = 110 to fit the longest cycle-state label ("Shadows TD/RA1")
	// without truncation. Shorter states leave a bit of empty space; that's
	// the trade-off for the longer name being readable.
	m_player_shadows.MoveWindow(x2, y2, 110, H);      x2 += 110 + pad;
	m_player_bg.MoveWindow(x2, y2, 70, H);            x2 += 70 + pad;
	const int swatch = H;
	for (int i = 0; i < 8; i++)
	{
		m_player_side[i].MoveWindow(x2, y2, swatch, H);
		x2 += swatch + 2;
	}
	// 9th swatch — custom (opens color picker).
	m_player_side_custom.MoveWindow(x2, y2, swatch, H); x2 += swatch + pad;
	// Game Grid cycle button.
	m_player_iso_grid.MoveWindow(x2, y2, 70, H);
}

void CXCCFileView::player_update_label()
{
	if (!m_player_controls_created)
		return;
	int total = m_player_cf;
	if (m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0)
		total = m_player_cf / 2;
	string s = "Frame " + n(m_player_frame + 1) + " / " + n(total);
	m_player_label.SetWindowText(s.c_str());
}

void CXCCFileView::player_convert_frame_to_bgra(int frame_idx, DWORD* dst) const
{
	const int n = m_player_cx * m_player_cy;
	if (n <= 0 || frame_idx < 0 ||
		frame_idx >= static_cast<int>(m_player_frames.size()))
		return;
	const byte* s = m_player_frames[frame_idx].data();
	const int cx_s = m_player_cx;
	const int bg_mode = m_player_bg_mode;
	const int side = m_player_side_idx;
	const COLORREF custom_color = m_player_side_custom_color;
	const COLORREF ck_a = theme::checker_a();
	const COLORREF ck_b = theme::checker_b();
	const DWORD ck_a_d = (GetRValue(ck_a) << 16) | (GetGValue(ck_a) << 8) | GetBValue(ck_a);
	const DWORD ck_b_d = (GetRValue(ck_b) << 16) | (GetGValue(ck_b) << 8) | GetBValue(ck_b);
	// Pane bg (mode 2): use theme background so transparent pixels blend
	// into the file-info pane. Shadows still darken these pixels.
	const COLORREF pane_c = theme::bg();
	const DWORD pane_d = (GetRValue(pane_c) << 16) | (GetGValue(pane_c) << 8) | GetBValue(pane_c);
	float remap_r = 0, remap_g = 0, remap_b = 0;
	if (side >= 0 && side < 8)
	{
		remap_r = GetRValue(k_side_colors[side]);
		remap_g = GetGValue(k_side_colors[side]);
		remap_b = GetBValue(k_side_colors[side]);
	}
	else if (side == 8)
	{
		remap_r = GetRValue(custom_color);
		remap_g = GetGValue(custom_color);
		remap_b = GetBValue(custom_color);
	}
	const bool shadow_on =
		m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0;
	const byte* sshad = nullptr;
	if (shadow_on)
	{
		int shadow_idx = frame_idx + m_player_cf / 2;
		if (shadow_idx >= 0 && shadow_idx < static_cast<int>(m_player_frames.size()))
			sshad = m_player_frames[shadow_idx].data();
	}
	// Capture bg_idx outside the loop to avoid a member-read per pixel.
	const byte bg_idx = static_cast<byte>(m_player_bg_idx);
	// RA1 mode (state 2): in RA1 SHP files, shadow pixels are stored as
	// palette index LTGREEN (=4) per WWSTD.H. The runtime engine routes
	// them through a per-unit ColorTable (UnitShadow, built by
	// Conquer_Build_Translucent_Table in JSHELL.CPP:409-433) that maps
	// palette[LTGREEN] -> the post-table sentinel SHADOW_COL (0xFF, from
	// SHAPE.INC:84); DS_DS.ASM:269-276 then darkens the destination
	// pixel through ShadowingTable. Authority: UShadowCols =
	// {LTGREEN, BLACK, 130, 0} in DISPLAY.CPP:357-365.
	// We collapse this to a direct raw-index test on the source pixel
	// (idx == 4) and darken the would-be background BGRA toward black
	// by 130/256 — same ratio UShadowCols produces. Mutually exclusive
	// with shadow_on (pair-frame).
	const bool ra1_shadow = m_player_shadows_ra1;
	const byte ra1_shadow_idx = 4; // LTGREEN
	for (int i = 0; i < n; i++)
	{
		byte idx = s[i];
		DWORD bgra;
		if (ra1_shadow && idx == ra1_shadow_idx)
		{
			// Resolve the background BGRA the same way the bg_idx branch
			// below does, then darken it.
			DWORD bg_bgra;
			if (bg_mode == 0)
				bg_bgra = m_color_table[bg_idx];
			else if (bg_mode == 2)
				bg_bgra = pane_d;
			else
			{
				int x = i % cx_s, y = i / cx_s;
				bg_bgra = (((x >> 3) ^ (y >> 3)) & 1) ? ck_b_d : ck_a_d;
			}
			const float fa = 200.0f / 256.0f;
			float rr = static_cast<float>((bg_bgra >> 16) & 0xff) * (1.0f - fa);
			float gg = static_cast<float>((bg_bgra >>  8) & 0xff) * (1.0f - fa);
			float bb = static_cast<float>( bg_bgra        & 0xff) * (1.0f - fa);
			int irr = static_cast<int>(rr + 0.5f);
			int igg = static_cast<int>(gg + 0.5f);
			int ibb = static_cast<int>(bb + 0.5f);
			if (irr > 255) irr = 255; if (igg > 255) igg = 255; if (ibb > 255) ibb = 255;
			dst[i] = static_cast<DWORD>(ibb)
			       | (static_cast<DWORD>(igg) <<  8)
			       | (static_cast<DWORD>(irr) << 16);
			continue;
		}
		if (idx == bg_idx)
		{
			if (bg_mode == 0)
				bgra = m_color_table[bg_idx];
			else if (bg_mode == 2)
				bgra = pane_d;
			else
			{
				int x = i % cx_s, y = i / cx_s;
				bgra = (((x >> 3) ^ (y >> 3)) & 1) ? ck_b_d : ck_a_d;
			}
		}
		else
		{
			bgra = m_color_table[idx];
			if (side >= 0 && idx >= 16 && idx <= 31)
			{
				float b = static_cast<float>(bgra & 0xff);
				float g = static_cast<float>((bgra >> 8) & 0xff);
				float r = static_cast<float>((bgra >> 16) & 0xff);
				float bright = std::max(r, std::max(g, b)) / 255.0f * 1.25f;
				int rr = static_cast<int>(remap_r * bright + 0.5f);
				int gg = static_cast<int>(remap_g * bright + 0.5f);
				int bb = static_cast<int>(remap_b * bright + 0.5f);
				if (rr > 255) rr = 255; if (gg > 255) gg = 255; if (bb > 255) bb = 255;
				bgra = static_cast<DWORD>(bb) | (static_cast<DWORD>(gg) << 8) | (static_cast<DWORD>(rr) << 16);
			}
		}
		if (sshad && sshad[i] != bg_idx)
		{
			float fa = 120.0f / 255.0f;
			float r = static_cast<float>((bgra >> 16) & 0xff) * (1.0f - fa);
			float g = static_cast<float>((bgra >> 8) & 0xff) * (1.0f - fa);
			float b = static_cast<float>(bgra & 0xff) * (1.0f - fa);
			int rr = static_cast<int>(r + 0.5f), gg = static_cast<int>(g + 0.5f), bb = static_cast<int>(b + 0.5f);
			bgra = static_cast<DWORD>(bb) | (static_cast<DWORD>(gg) << 8) | (static_cast<DWORD>(rr) << 16);
		}
		dst[i] = bgra;
	}
	// Grid overlay baked into the cached buffer so per-paint cost stays at
	// memcpy. Painted over background pixels only (idx == 0) — sprite
	// pixels stay intact.
	if (m_player_grid_mode > 0)
	{
		const int cy_s = m_player_cy;
		const int tileW = (m_player_grid_mode == 1) ? 48 : 60;
		const int gcx = cx_s / 2;
		const int gcy = cy_s;
		const DWORD line = 0x00FFFFFF;
		for (int py = 0; py < cy_s; py++)
		{
			for (int px = 0; px < cx_s; px++)
			{
				int i = px + cx_s * py;
				// Grid lines paint over BG pixels only (s[i] equals the
				// configured background index); sprite pixels stay intact.
				if (s[i] != bg_idx) continue;
				int dx = px - gcx;
				int dy = py - gcy;
				int u = dx + 2 * dy;
				int v = 2 * dy - dx;
				int au = u % tileW; if (au < 0) au += tileW;
				int av = v % tileW; if (av < 0) av += tileW;
				if (au < 2 || av < 2 || au > tileW - 2 || av > tileW - 2)
					dst[i] = line;
			}
		}
	}
}

void CXCCFileView::invalidate_vxl_cloud()
{
	// Drop the cached point cloud; next paint will rebuild it via
	// player_decode_frames(). Bump the token so the splat cache (keyed on
	// it) also rebuilds. Only meaningful in VXL player mode; harmless to
	// call otherwise.
	if (m_ft == ft_vxl && m_player_mode)
	{
		m_vxl_cloud.clear();
		player_decode_frames();
		m_open_token++;
		CRect cr;
		GetClientRect(&cr);
		cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
	}
}

int CXCCFileView::shp_effective_upscale_factor() const
{
	const int sw = m_player_cx;
	const int sh = m_player_cy;
	int factor = theme::interp_upscale_factor();
	if (factor <= 1 || sw <= 0 || sh <= 0)
		return 1;
	// Memory guard. Win32 process has ~2GB heap; a single upscaled cache
	// entry of >32 MB (8M DWORDs) on a many-frame WSA blows OOM very fast
	// under the prefill's OpenMP fan-out. Crash report 2026-05-21 caught
	// a large SHP at xBR-4x trying to allocate 70 MB per frame, multiplied
	// across worker threads. Fall back to factor=1 (source-res) for the
	// whole file when even one frame would exceed the cap. 8M DWORDs
	// covers comfortably: 1414x1414 at factor=2 or any standard SHP/WSA
	// at factor=4. Per-frame fallback was rejected because it'd leave the
	// stretch_image source dims out of sync with the cache contents.
	const size_t k_max_upscaled_pixels = 8u * 1024u * 1024u;
	const size_t target = static_cast<size_t>(sw) * sh * factor * factor;
	if (target > k_max_upscaled_pixels)
		return 1;

	// Aggregate-cost guard. NNEDI3 is ~10x slower per output pixel than
	// xBR-4x; on a large multi-frame SHP/WSA the prefill can stall the UI
	// for tens of seconds even with OMP parallelism — looks like a hang.
	// Fall back to factor=1 when the total work (frame count * upscaled
	// pixel count) would exceed a budget. The cheap rule-based upscalers
	// don't need this — the memory cap above is the only limit they hit.
	const theme::interpolation mode = theme::interp();
	const bool is_nnedi = (mode == theme::interp_nnedi2x || mode == theme::interp_nnedi4x);
	if (is_nnedi) {
		const int nf = static_cast<int>(m_player_frames.size());
		// Budget: 64 megapixels of total upscaled output, summed across all
		// frames. That's ~2 seconds of single-core NNEDI3-4x bake on
		// typical hardware; with 8-core OMP, sub-second. Above the budget
		// we silently downgrade to factor=1. Examples:
		// - 100 frames @ 64x64 src, 4x = 100 * 65536 = 6.5 Mpx -> fine.
		// - 100 frames @ 300x300 src, 4x = 100 * 1.44 Mpx = 144 Mpx -> skip.
		// - 1 frame @ 1000x1000 src, 4x = 16 Mpx -> fine.
		const long long total_upscaled = static_cast<long long>(nf) * target;
		const long long k_nnedi_budget = 64LL * 1024 * 1024;
		if (total_upscaled > k_nnedi_budget)
			return 1;
	}
	return factor;
}

void CXCCFileView::player_fill_bgra_cache_entry(int frame_idx, shp_bgra_cache_entry& ce) const
{
	const int sw = m_player_cx;
	const int sh = m_player_cy;
	const int factor = shp_effective_upscale_factor();
	const int dw = sw * factor;
	const int dh = sh * factor;
	if (factor == 1)
	{
		// Regular interpolation OR upscale-skipped (oversize source): cache
		// stores source-resolution BGRA.
		ce.bgra.assign(static_cast<size_t>(sw) * sh, 0);
		player_convert_frame_to_bgra(frame_idx, ce.bgra.data());
	}
	else
	{
		// Pixel-art upscaler: render source first, then expand into the
		// cache buffer at the upscaled resolution. Scratch goes on the
		// heap to keep us off the thread stack at large frame sizes.
		std::vector<DWORD> src(static_cast<size_t>(sw) * sh, 0);
		player_convert_frame_to_bgra(frame_idx, src.data());
		ce.bgra.assign(static_cast<size_t>(dw) * dh, 0);
		const unsigned int* sp = reinterpret_cast<const unsigned int*>(src.data());
		unsigned int* dp = reinterpret_cast<unsigned int*>(ce.bgra.data());
		switch (theme::interp())
		{
		case theme::interp_scale2x: pixel_upscale::scale2x(sp, sw, sh, dp); break;
		case theme::interp_scale3x: pixel_upscale::scale3x(sp, sw, sh, dp); break;
		case theme::interp_hq2x:    pixel_upscale::hq2x   (sp, sw, sh, dp); break;
		case theme::interp_hq4x:    pixel_upscale::hq4x   (sp, sw, sh, dp); break;
		case theme::interp_xbr2x:   pixel_upscale::xbr2x  (sp, sw, sh, dp); break;
		case theme::interp_xbr4x:   pixel_upscale::xbr4x  (sp, sw, sh, dp); break;
		case theme::interp_nnedi2x: pixel_upscale::nnedi2x(sp, sw, sh, dp); break;
		case theme::interp_nnedi4x: pixel_upscale::nnedi4x(sp, sw, sh, dp); break;
		default: break;	// can't happen — factor==1 path handled above.
		}
	}
	ce.cx_upscaled = dw;
	ce.cy_upscaled = dh;
	ce.version = m_player_bgra_version;
}

void CXCCFileView::player_prefill_bgra_cache()
{
	const int nf = static_cast<int>(m_player_frames.size());
	const int n = m_player_cx * m_player_cy;
	if (nf <= 0 || n <= 0)
		return;
	// Lazy prefill: previously we eagerly baked every frame's BGRA cache
	// upfront here, parallelized across frames via OpenMP. On a 9 MB SHP
	// (many large frames) that meant tens of seconds of frozen UI before
	// the file was visible — and with NNEDI3 active, minutes. Replaced
	// with a "bake the current frame only, defer the rest" strategy: the
	// cache-miss path inside player_draw already handles per-frame on-
	// demand bake correctly, so scrubbing a fresh frame triggers a single
	// frame's worth of work (small hitch, not a freeze). After one
	// playthrough the cache is warm and animation runs fluidly.
	//
	// Allocate the slot vector but leave entries empty (version=-1) so the
	// cache-hit check in player_draw misses cleanly until the slot is
	// filled by the miss-path helper.
	m_player_bgra.assign(nf, shp_bgra_cache_entry{});
	// Bake the currently-visible frame synchronously so the very first
	// paint is a cache hit (avoids one frame of post-open stutter on
	// single-frame SHPs / cameos / the title-screen WSA frame).
	if (m_player_frame >= 0 && m_player_frame < nf) {
		player_fill_bgra_cache_entry(m_player_frame, m_player_bgra[m_player_frame]);
	}
}

void CXCCFileView::player_draw(CDC* pDC)
{
	CRect cr;
	GetClientRect(&cr);
	const int controls_h = player_band_h();
	int avail_w = cr.Width();
	int avail_h = cr.Height() - controls_h;
	if (avail_w < 1) avail_w = 1;
	if (avail_h < 1) avail_h = 1;
	if (m_player_cx <= 0 || m_player_cy <= 0)
		return;

	// VXL: point-splat the cached point cloud into an 8bpp framebuffer at SSx
	// canvas resolution. The chosen interpolation mode (theme::interp()) then
	// downsamples that supersample buffer to the destination viewport via the
	// shared theme::stretch_image path — same way SHP/WSA frames are scaled.
	// This makes Bilinear/Bicubic/Lanczos actually do real silhouette AA on
	// VXL, rather than the previous Gaussian-splat path that ignored the user
	// choice. SHP/WSA path below stays unchanged.
	//
	// The splat itself is cached in m_vxl_splat across paints because it
	// depends only on (file, yaw, pitch, ss, shading). Idle viewing or any
	// repaint that doesn't change the camera reuses the cached buffer; the
	// OpenMP region only fires on cache miss (drag, ss/shading toggle, file
	// change). Side color / BG / palette / zoom / pan are applied downstream
	// per paint and don't invalidate the cache.
	const byte* s = nullptr;
	int vxl_ss_cx = 0, vxl_ss_cy = 0;
	// Shading factor per pixel (0..255 = 0..2.0 at half=128). Only populated
	// for VXL when theme::vxl_shading() is on; SHP/WSA leaves it empty.
	const std::vector<unsigned char>* vxl_shade_p = nullptr;
	if (is_vxl_view())
	{
		if (m_vxl_cloud.empty())
			return;
		// Effective SS = user's chosen value. The previous interactive low-SS
		// override (force SS=1 while dragging) was removed: it prevented the
		// zoom-aware bump and seam pad from applying mid-drag, which is exactly
		// where the user notices their effect. Drag perf stays acceptable
		// because (a) the splat itself is cheap (<1ms for ~5-10k voxels) and
		// (b) the blit still forces nearest while dragging (see stretch_image
		// branch below), so the heavy resample doesn't fire per frame.
		const int ss_user = static_cast<int>(theme::vxl_supersample());
		// Zoom-aware effective SS. SS resolution is logical * ss; at high zoom
		// the splat is then stretched up by stretch_image, so a small voxel at
		// 500% zoom + SS=4 ends up as a ~120 px source filling a ~150 px target
		// — only a 1.25x stretch of a low-res image, which reads as blocky.
		// When zoom > 100%, raise the effective SS toward the on-screen pixel
		// coverage (cap 16, snap to the {1,2,4,8,16} ladder used by the combo).
		// Gated on ss_user > 1 so the explicit Supersampling=Off choice is
		// honored at every zoom level. Applies during orbit too (user wants to
		// see the model at full quality even while dragging).
		int ss = ss_user;
		if (theme::vxl_zoom_aware_ss()
			&& ss_user > 1 && m_player_zoom_pct > 100)
		{
			int ss_zoom = (m_player_zoom_pct + 99) / 100;	// ceil(zoom/100)
			if (ss_zoom > 16) ss_zoom = 16;
			if (ss_zoom > ss) ss = ss_zoom;
			static const int k_ss_ladder[] = { 1, 2, 4, 8, 16 };
			for (int v : k_ss_ladder) { if (v >= ss) { ss = v; break; } }
		}
		const bool shading = theme::vxl_shading();
		const int lighting_version = theme::vxl_lighting_version();
		vxl_ss_cx = m_player_cx * ss;
		vxl_ss_cy = m_player_cy * ss;
		// The splat cache no longer keys on lighting_version: lighting only
		// affects shade[], which is rebuilt by a cheap separate pass below
		// (consuming the per-pixel cam_normal[] buffer). So slider drags in
		// the VXL Lighting dialog skip the splat rebuild entirely.
		// VPL bakes lighting directly into buf, so its key must include both
		// "vpl is on" and the current lighting version (light direction).
		const int splat_lighting_version_now = theme::vxl_lighting_version();
		// View > Voxel mode (0=colors, 1=surface normals, 2=depth). Drives which
		// auxiliary buffer the splat populates and how the composite paints it.
		const int view_mode = GetMainFrame()->get_vxl_mode();
		const bool cache_hit =
			m_vxl_splat.token == m_open_token &&
			m_vxl_splat.yaw == m_vxl_yaw &&
			m_vxl_splat.pitch == m_vxl_pitch &&
			m_vxl_splat.ss == ss &&
			m_vxl_splat.shading == shading &&
			m_vxl_splat.view_mode == view_mode &&
			m_vxl_splat.vpl_active == m_vpl_loaded &&
			(!m_vpl_loaded || m_vxl_splat.vpl_lighting_version == splat_lighting_version_now) &&
			(!m_vpl_loaded || m_vxl_splat.vpl_pal_version == m_player_bgra_version) &&
			m_vxl_splat.cx_s == vxl_ss_cx &&
			m_vxl_splat.cy_s == vxl_ss_cy &&
			m_vxl_splat.buf.size() == static_cast<size_t>(vxl_ss_cx) * vxl_ss_cy;
		if (!cache_hit)
		{
			const int half_ss = m_vxl_half * ss;
			const int c_pixels = vxl_ss_cx * vxl_ss_cy;
			const double cosY = std::cos(m_vxl_yaw);
			const double sinY = std::sin(m_vxl_yaw);
			const double cosP = std::cos(m_vxl_pitch);
			const double sinP = std::sin(m_vxl_pitch);
			// Light vector in camera space. Two frames supported:
			//   world_fixed: vxl_light_direction() is a world-space vector;
			//     rotate it into camera space by the SAME yaw+pitch+Y-flip
			//     the voxel normals get (cam_normal = (nrx, -nry_p, nrz_p),
			//     see ~line 4455). Since dot(R*n, R*L) = dot(n, L), dotting
			//     the camera-space light against the camera-space normal
			//     reproduces a model-fixed light regardless of orbit. Matches
			//     the TS/RA2 engine's VoxelLightSource.
			//   camera_fixed (default): vxl_light_direction() is already
			//     interpreted in camera space; skip the rotation. The lit
			//     side stays put on screen as the camera orbits (XCC's
			//     traditional "studio lamp" behavior).
			// Computed once per splat (same for every voxel) and shared by
			// both the VPL section pick and the synthetic shade pass.
			float cam_lx, cam_ly, cam_lz;
			{
				float lwx, lwy, lwz;
				theme::vxl_light_direction(lwx, lwy, lwz);
				if (theme::vxl_light_frame_v() == theme::vlf_world_fixed)
				{
					float lrx = static_cast<float>(lwx * cosY - lwy * sinY);
					float lry = static_cast<float>(lwx * sinY + lwy * cosY);
					float lrz = lwz;
					float lry_p = static_cast<float>(lry * cosP - lrz * sinP);
					float lrz_p = static_cast<float>(lry * sinP + lrz * cosP);
					cam_lx = lrx;
					cam_ly = -lry_p;	// match cam_normal's Y flip
					cam_lz = lrz_p;
				}
				else
				{
					cam_lx = lwx;
					cam_ly = lwy;
					cam_lz = lwz;
				}
			}
			// Each voxel projects to a parallelogram (the rotated unit cube),
			// not an axis-aligned square. Fixed ss x ss splats leave diagonal
			// gaps under arbitrary rotation. Compute the screen-space bounding
			// box of a projected unit cube under this (yaw, pitch) and use
			// that as the splat footprint. Adjacent voxels along any local
			// axis then overlap by construction. The +1 ceil + extra pixel
			// guards against int truncation drift between neighbors.
			auto absd = [](double v) { return v < 0 ? -v : v; };
			const double ex_x = absd(cosY);                    // x-edge -> rx
			const double ey_x = absd(-sinY);                   // y-edge -> rx
			// z-edge contributes 0 to rx.
			const double ex_y = absd(sinY * sinP);             // x-edge -> py (via ry*cosP - rz*sinP; ry = x*sinY)
			const double ey_y = absd(cosY * sinP);             // y-edge -> py (ry = y*cosY)
			const double ez_y = absd(-cosP);                   // z-edge -> py (rz=z, so -z*sinP... wait sign: py = ry*cosP - rz*sinP; z-edge: rz=1 -> py contribution = -sinP). Use abs.
			const double fpx_d = (ex_x + ey_x) * ss;
			const double fpy_d = (ex_y + ey_y + ez_y) * ss;
			// Use ceil (not truncate) on the projected-cube extent, and grow the
			// safety pad with ss. At yaw=45°, fpx_d=√2·ss; truncate+2 was fine at
			// ss=4 (gap≤1 ss-pixel = visually negligible after downscale) but at
			// ss=8/16 (now reachable via zoom-aware SS) integer rounding on adjacent
			// voxel centers can open 1-2 ss-pixel seams that survive the stretch.
			// pad = max(2, ss/2) keeps the original behavior at ss≤4 and adds just
			// enough overlap at ss=8/16 to close those seams. The user can shrink
			// the pad above/below this baseline from the VXL Lighting dialog
			// (Seam pad -64..+64, default 0) for debugging — negative exposes
			// seams (useful to confirm what's causing them), positive grows
			// the footprint. A pad ≤ 0 may yield a 0-or-negative footprint,
			// which the dy/dx splat loops handle as "don't write" (harmless).
			// The user delta is ignored at ss=1 (Supersampling=Off): there's
			// no SS to debug, and a negative delta would blank the view.
			const int pad = std::max(2, ss / 2)
				+ (ss > 1 ? theme::vxl_splat_pad_extra() : 0);
			const int fp_x = static_cast<int>(std::ceil(fpx_d)) + pad;
			const int fp_y = static_cast<int>(std::ceil(fpy_d)) + pad;
			// Center the footprint on the projected voxel so the splat is
			// symmetric instead of biased toward +x/+y.
			const int fp_x_off = fp_x / 2;
			const int fp_y_off = fp_y / 2;
			byte* d = m_vxl_splat.buf.write_start(c_pixels);
			memset(d, 0, c_pixels);
			vector<short> z_buf(c_pixels, SHRT_MIN);
			// Surface-normals view: per-pixel grayscale of the normal index,
			// written alongside color in the z-test win below. Occupancy is read
			// from buf (d[ofs] != 0) at composite time, so this only needs the
			// gray value. nt_div mirrors the static grid ramp (TS 36-set -> /35,
			// RA2/YR 244-set -> direct 0..255).
			unsigned char* nidx_buf = nullptr;
			const int nt_div = (m_vxl_normal_type == 2) ? 35 : 255;
			if (view_mode == 1)
			{
				m_vxl_splat.nidx.assign(c_pixels, 0);
				nidx_buf = m_vxl_splat.nidx.data();
			}
			else
				m_vxl_splat.nidx.clear();
			if (view_mode != 2)
				m_vxl_splat.depth.clear();
			// Splat writes the rotated camera-space normal per pixel into
			// cam_normal[], not a pre-shaded byte. The lighting pass below
			// converts cam_normal -> shade in a cheap second pass keyed on
			// lighting_version. That way slider drags in the VXL Lighting
			// dialog only redo the lighting pass, not the splat.
			//
			// VPL path: lighting is baked into buf via vpl[section][color]
			// at splat time, so cam_normal/shade are unused (and would
			// double-shade if the post-splat lighting pass ran).
			const bool synth_shading = shading && !m_vpl_loaded;
			const bool ao_active = theme::vxl_ao_enabled();
			if (synth_shading)
			{
				m_vxl_splat.cam_normal.assign(static_cast<size_t>(c_pixels) * 3, 0);
			}
			else
			{
				m_vxl_splat.cam_normal.clear();
				m_vxl_splat.shade.clear();
			}
			// AO buffer: written whenever the cloud has AO baked, regardless
			// of whether `ao_active` is true — that gate lives in the shade
			// pass so toggling AO on/off doesn't invalidate the splat.
			m_vxl_splat.ao.assign(static_cast<size_t>(c_pixels), 0);
			// Light direction for VPL section selection. Fetched once per
			// rebuild — same for every voxel in this splat.
			float vpl_lx = 0, vpl_ly = 0, vpl_lz = 1;
			// Secondary fresnel-like light: l2 = normalize(l + (0,0,1)) per
			// vxl-renderer's shaders.hlsl:131. Adds a vertical bias so
			// top-facing surfaces pick up extra brightness (a cheap stand-in
			// for view-dependent fresnel — view direction is implicit since the
			// camera looks down +z in our cam space). Same for every voxel.
			float vpl_l2x = 0, vpl_l2y = 0, vpl_l2z = 0;
			if (m_vpl_loaded)
			{
				// Camera-space (world-fixed) light, computed once above.
				vpl_lx = cam_lx; vpl_ly = cam_ly; vpl_lz = cam_lz;
				// l + (0,0,1), normalized. Degenerate when l == (0,0,-1) (light
				// pointing straight down through the model from above-camera);
				// fall back to zero so f2 contributes nothing in that case.
				float sx = vpl_lx, sy = vpl_ly, sz = vpl_lz + 1.0f;
				float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
				if (slen > 1e-4f)
				{
					vpl_l2x = sx / slen;
					vpl_l2y = sy / slen;
					vpl_l2z = sz / slen;
				}
				// Re-bake the section table if the lighting (amb/dif/spec) or
				// the display palette changed since the last bake. Cheap
				// (32*255 nearest-color searches) and only runs on actual
				// change, not every splat.
				if (m_vpl_baked.empty()
					|| m_vpl_baked_lighting_version != theme::vxl_lighting_version()
					|| m_vpl_baked_pal_version != m_player_bgra_version)
					rebuild_baked_vpl();
			}
			// Parallelize the splat by partitioning *output rows*: each thread
			// owns a contiguous row band [y_lo, y_hi) of the supersample
			// framebuffer and iterates the entire voxel cloud, writing only when
			// the projected voxel footprint falls in its band. No write hazard on
			// d / z_buf / shade because bands don't overlap. Cost: rotation
			// math runs T times per voxel (T = thread count), but most voxels
			// reject early via the band-bounds check before hitting the per-pixel
			// inner loop, and the inner ss*ss pixel writes dominate at SS=4..16
			// anyway. Voxel cloud is read-only, so no copies needed.
			const int n_voxels = static_cast<int>(m_vxl_cloud.size());
			const t_vxl_voxel* cloud = m_vxl_cloud.data();
			signed char* cam_n_buf = synth_shading ? m_vxl_splat.cam_normal.data() : nullptr;
			unsigned char* ao_buf = m_vxl_splat.ao.data();
			// Splat is intentionally serial. The previous output-row-banded
			// OpenMP version made every thread iterate the whole voxel cloud
			// and run the rotation math for each voxel, then reject ones
			// outside its band. Since the rotation math (8 muls + 6 adds) is
			// the hot path — not the ss*ss inner write — that multiplied CPU
			// cost by the thread count for no useful work, pegging all cores
			// while orbiting a small voxel model. Serial is cheap enough:
			// ~5-10k voxels per VXL, so a rebuild is well under a millisecond
			// and easily keeps up with high-polling-rate orbit drags.
			for (int vi = 0; vi < n_voxels; vi++)
			{
				const t_vxl_voxel& v = cloud[vi];
				double rx = v.x * cosY - v.y * sinY;
				double ry = v.x * sinY + v.y * cosY;
				double rz = v.z;
				double py = ry * cosP - rz * sinP;
				double pz = ry * sinP + rz * cosP;
				int sx0 = static_cast<int>(rx * ss) + half_ss;
				int sy0 = -static_cast<int>(py * ss) + half_ss;
				short depth = static_cast<short>(pz);
				// Rotate the voxel's local-space normal into camera space.
				// Stored quantized in cam_normal[] as i8 per channel so the
				// shading pass can re-apply lighting without rebuilding the
				// splat. Y is flipped to match the same post-transform Y flip
				// applied to positions during decode.
				//
				// The same rotated normal is also used to pick the VPL
				// section (when VPL is loaded), so we compute it whenever
				// either path needs it.
				signed char ni8x = 0, ni8y = 0, ni8z = 0;
				byte voxel_pal = v.color;
				// Surface-normals view: gray value for this voxel's normal index.
				// Per-voxel (not per-pixel), so compute it once here.
				byte ngray = 0;
				if (nidx_buf)
				{
					int g = (nt_div == 35) ? (v.normal_idx * 255 / 35) : v.normal_idx;
					ngray = static_cast<byte>(g > 255 ? 255 : g);
				}
				const bool need_normal = synth_shading || m_vpl_loaded;
				if (need_normal)
				{
					float nrx = static_cast<float>(v.nx * cosY - v.ny * sinY);
					float nry = static_cast<float>(v.nx * sinY + v.ny * cosY);
					float nrz = static_cast<float>(v.nz);
					float nry_p = static_cast<float>(nry * cosP - nrz * sinP);
					float nrz_p = static_cast<float>(nry * sinP + nrz * cosP);
					// Stored as: (nrx, -nry_p, nrz_p) so the lighting pass can
					// just do dot(stored, light) without per-pixel sign-flips.
					auto q = [](float v) -> signed char {
						int i = static_cast<int>(v * 127.0f + (v >= 0 ? 0.5f : -0.5f));
						if (i < -127) i = -127; else if (i > 127) i = 127;
						return static_cast<signed char>(i);
					};
					ni8x = q(nrx);
					ni8y = q(-nry_p);
					ni8z = q(nrz_p);
					if (m_vpl_loaded && voxel_pal != 0)
					{
						// Two-light shading model ported verbatim from vxl-renderer
						// (shaders.hlsl:127-135 / gdi.cpp:286-307):
						//   f1 = clamp0(n . L)             primary directional light
						//   f2 = clamp0((n . L2)/(d-(d-1)(n . L2)))  fresnel-like
						//   f  = f1 + f2                   range [0..2]
						// The section index is then f scaled by 16 and truncated,
						// indexing the (re-baked) VPL table directly -- no brightness
						// curve here, no normalize, no inversion. Convention: more
						// light => higher section (sec0 = darkest). Ambient/Diffuse/
						// Specular are baked into m_vpl_baked by rebuild_baked_vpl()
						// (vxl-renderer's two-stage model: bake amb/dif/spec into the
						// table, then index it), so the knobs are live in VPL mode.
						const float d2 = 3.0f;
						float nL  = (ni8x * vpl_lx  + ni8y * vpl_ly  + ni8z * vpl_lz)  / 127.0f;
						float nL2 = (ni8x * vpl_l2x + ni8y * vpl_l2y + ni8z * vpl_l2z) / 127.0f;
						float f1 = nL  > 0.0f ? nL  : 0.0f;
						// Guard: denominator d - (d-1)*nL2 = 3 - 2*nL2. nL2 <= 1
						// makes denom >= 1 — never zero in practice, but clamp
						// defensively.
						float denom = d2 - (d2 - 1.0f) * nL2;
						float f2 = nL2 > 0.0f && denom > 1e-4f ? nL2 / denom : 0.0f;
						if (f2 < 0.0f) f2 = 0.0f;
						float f = f1 + f2;
						if (f > 2.0f) f = 2.0f;
						// vxl-renderer's exact VPL section index
						// (shaders.hlsl:134 / gdi.cpp:305):
						//   i = 16 * (clamp0(f1) + clamp0(f2))  integer truncation.
						// f spans [0..2] so i spans [0..32]; the renderer over-reads
						// vpl_data[32] at f==2.0 exactly (a latent OOB in its own
						// 32-section array). We clamp to [0,31]: in-bounds and faithful
						// everywhere except that degenerate saturation corner.
						int sec = static_cast<int>(16.0f * f);
						if (sec < 0) sec = 0; else if (sec > 31) sec = 31;
						// Index the re-baked table; fall back to the on-disk VPL if
						// the bake is somehow unavailable (shouldn't happen -- the
						// rebuild ran above when m_vpl_loaded).
						voxel_pal = m_vpl_baked.empty()
							? m_vpl_file.get_section(sec)[v.color]
							: m_vpl_baked[static_cast<size_t>(sec) * 256 + v.color];
					}
				}
				// Splat the rotation-aware footprint (computed above) centered
				// on the projected voxel position. Guarantees overlap with the
				// projected neighbor along any local axis, eliminating diagonal
				// gaps that an axis-aligned ss x ss square leaves under
				// arbitrary yaw/pitch. Z-buffer keeps real occlusions correct.
				for (int dy = 0; dy < fp_y; dy++)
				{
					int sy_pix = sy0 + dy - fp_y_off;
					if (sy_pix < 0 || sy_pix >= vxl_ss_cy) continue;
					for (int dx = 0; dx < fp_x; dx++)
					{
						int sx_pix = sx0 + dx - fp_x_off;
						if (sx_pix < 0 || sx_pix >= vxl_ss_cx) continue;
						int ofs = sx_pix + vxl_ss_cx * sy_pix;
						if (depth > z_buf[ofs])
						{
							z_buf[ofs] = depth;
							d[ofs] = voxel_pal;
							if (nidx_buf)
								nidx_buf[ofs] = ngray;
							if (cam_n_buf)
							{
								signed char* p = cam_n_buf + 3 * ofs;
								p[0] = ni8x; p[1] = ni8y; p[2] = ni8z;
							}
							ao_buf[ofs] = v.ao;
						}
					}
				}
			}
			// Depth view: keep the finished z-buffer and bound the occupied
			// range for the grayscale ramp. Occupancy = d[o] != 0 (same
			// convention the color composite uses to detect background).
			if (view_mode == 2)
			{
				m_vxl_splat.depth.assign(z_buf.begin(), z_buf.end());
				short dmin = SHRT_MAX, dmax = SHRT_MIN;
				for (int o = 0; o < c_pixels; o++)
				{
					if (d[o] == 0) continue;
					short zv = z_buf[o];
					if (zv < dmin) dmin = zv;
					if (zv > dmax) dmax = zv;
				}
				if (dmax < dmin) { dmin = 0; dmax = 0; }
				m_vxl_splat.depth_min = dmin;
				m_vxl_splat.depth_max = dmax;
			}
			m_vxl_splat.view_mode = view_mode;
			m_vxl_splat.token = m_open_token;
			m_vxl_splat.yaw = m_vxl_yaw;
			m_vxl_splat.pitch = m_vxl_pitch;
			m_vxl_splat.ss = ss;
			m_vxl_splat.shading = shading;
			m_vxl_splat.vpl_active = m_vpl_loaded;
			m_vxl_splat.vpl_lighting_version = m_vpl_loaded ? splat_lighting_version_now : -1;
			m_vxl_splat.vpl_pal_version = m_vpl_loaded ? m_player_bgra_version : -1;
			m_vxl_splat.cx_s = vxl_ss_cx;
			m_vxl_splat.cy_s = vxl_ss_cy;
			// Force the shading pass to run on the fresh cam_normal buffer.
			m_vxl_splat.shade_lighting_version = -1;
		}
		// Lighting pass: cheap. Runs whenever the splat just rebuilt OR
		// theme::vxl_lighting_version() has bumped since shade was last built.
		// Converts cam_normal[] -> shade[] via the same ambient + diffuse*max(0,
		// dot(n, light)) formula the old in-splat shading used. Output range
		// 0..255 where 128 = neutral 1.0 (matches downstream BGRA composite).
		//
		// AO multiplies in here: final = base_shade * (1 - (ao/255)*strength).
		// When AO is on but synth shading is off (VPL or no-shading), base_shade
		// stays at 128 (neutral) and only AO modulates the output. When both
		// are off the pass is skipped entirely.
		const bool ao_pass_active = theme::vxl_ao_enabled() && theme::vxl_ao_strength() > 0;
		const bool synth_pass_active = shading && !m_vpl_loaded;
		if (!synth_pass_active && !ao_pass_active)
		{
			// Neither pass wants to contribute — make sure stale shade values
			// from a previous on->off toggle don't leak into the composite.
			if (!m_vxl_splat.shade.empty() && m_vxl_splat.shade_lighting_version != lighting_version)
			{
				m_vxl_splat.shade.clear();
				m_vxl_splat.shade_lighting_version = lighting_version;
			}
		}
		if ((synth_pass_active || ao_pass_active) && m_vxl_splat.shade_lighting_version != lighting_version)
		{
			const int c_pixels_pass = vxl_ss_cx * vxl_ss_cy;
			m_vxl_splat.shade.assign(c_pixels_pass, 128);
			float light_x_pass = 0, light_y_pass = 0, light_z_pass = 1;
			float ambient_pass = 0, diffuse_pass = 0;
			if (synth_pass_active)
			{
				// Two frames (see ~line 4347 for the matching block in splat).
				// world_fixed: rotate the world-space light into camera space
				//   by the splat's yaw+pitch+Y-flip so the lit side stays
				//   fixed to the model as the camera orbits. This pass runs
				//   without a splat rebuild (keyed on shade_lighting_version),
				//   so recompute from the splat's stored angles.
				// camera_fixed: skip the rotation -- light vector is already
				//   in camera space.
				float lwx, lwy, lwz;
				theme::vxl_light_direction(lwx, lwy, lwz);
				if (theme::vxl_light_frame_v() == theme::vlf_world_fixed)
				{
					const double cY = std::cos(m_vxl_splat.yaw);
					const double sY = std::sin(m_vxl_splat.yaw);
					const double cP = std::cos(m_vxl_splat.pitch);
					const double sP = std::sin(m_vxl_splat.pitch);
					float lrx = static_cast<float>(lwx * cY - lwy * sY);
					float lry = static_cast<float>(lwx * sY + lwy * cY);
					float lry_p = static_cast<float>(lry * cP - lwz * sP);
					float lrz_p = static_cast<float>(lry * sP + lwz * cP);
					light_x_pass = lrx;
					light_y_pass = -lry_p;	// match cam_normal's Y flip
					light_z_pass = lrz_p;
				}
				else
				{
					light_x_pass = lwx;
					light_y_pass = lwy;
					light_z_pass = lwz;
				}
				ambient_pass = theme::vxl_light_ambient();
				diffuse_pass = theme::vxl_light_diffuse();
			}
			const float ao_strength_f = theme::vxl_ao_strength() / 100.0f;
			const signed char* nbuf = synth_pass_active ? m_vxl_splat.cam_normal.data() : nullptr;
			const unsigned char* aobuf = ao_pass_active && !m_vxl_splat.ao.empty() ? m_vxl_splat.ao.data() : nullptr;
			const byte* dbuf = m_vxl_splat.buf.data();
			unsigned char* sbuf = m_vxl_splat.shade.data();
			#pragma omp parallel for schedule(static) if(c_pixels_pass >= 65536)
			for (int i = 0; i < c_pixels_pass; i++)
			{
				if (dbuf[i] == 0) continue;	// empty pixel; leave at neutral
				float shade;
				if (nbuf)
				{
					const signed char* p = nbuf + 3 * i;
					float nx = p[0] / 127.0f;
					float ny = p[1] / 127.0f;
					float nz = p[2] / 127.0f;
					float ndotl = nx * light_x_pass + ny * light_y_pass + nz * light_z_pass;
					if (ndotl < 0.0f) ndotl = 0.0f;
					shade = ambient_pass + diffuse_pass * ndotl;
				}
				else
				{
					shade = 1.0f;	// neutral when only AO is active
				}
				if (aobuf)
				{
					float ao_f = aobuf[i] / 255.0f;
					shade *= (1.0f - ao_f * ao_strength_f);
				}
				int sb = static_cast<int>(shade * 128.0f + 0.5f);
				if (sb < 0) sb = 0; else if (sb > 255) sb = 255;
				sbuf[i] = static_cast<unsigned char>(sb);
			}
			m_vxl_splat.shade_lighting_version = lighting_version;
		}
		s = m_vxl_splat.buf.data();
		vxl_shade_p = &m_vxl_splat.shade;
	}
	else
	{
		if (m_player_frames.empty())
			return;
		s = m_player_frames[m_player_frame].data();
	}
	// Source buffer size (what gets passed to theme::stretch_image as the
	// source DIB). VXL renders at supersample resolution; SHP/WSA at native
	// times any active pixel-art upscale factor (interp_scale2x/3x,
	// interp_hq2x/4x, interp_xbr2x/4x). The upscale is baked into the BGRA
	// cache during fill, so per-paint cost stays at memcpy + StretchBlt.
	// shp_effective_upscale_factor() bakes in the memory cap (huge SHP/
	// WSA + xBR-4x falls back to factor=1 to avoid Win32 OOM).
	const int shp_upscale = is_vxl_view() ? 1 : shp_effective_upscale_factor();
	int cx_s = is_vxl_view() ? vxl_ss_cx : (m_player_cx * shp_upscale);
	int cy_s = is_vxl_view() ? vxl_ss_cy : (m_player_cy * shp_upscale);
	// Logical canvas size for layout / zoom / fit math. For VXL this stays at
	// m_player_cx/cy regardless of supersample factor — otherwise enabling a
	// higher SS would shrink the auto-fit and shift the Ctrl+wheel zoom
	// baseline. The supersample buffer is always downscaled by the same
	// logical ratio at the blit, so the on-screen size only depends on s_pct.
	int cx_logical = m_player_cx;
	int cy_logical = m_player_cy;
	int s_pct;
	if (m_player_zoom_pct > 0)
	{
		// Ctrl+wheel override (set in OnMouseWheel during player mode).
		s_pct = m_player_zoom_pct;
	}
	else if (m_player_native_size)
	{
		s_pct = 100;
	}
	else
	{
		int sx = avail_w * 100 / cx_logical;
		int sy = avail_h * 100 / cy_logical;
		s_pct = std::min(sx, sy);
		if (s_pct < 1) s_pct = 1;
		if (s_pct > 1600) s_pct = 1600;
	}
	// Reflect the live zoom in the Native button text so the user always sees
	// what the recorder will capture. Only refresh on change to avoid flicker
	// (SetWindowText is a no-op on identical text, but skipping the call also
	// skips the GDI churn). Native_size cases still show 100% because they
	// pin s_pct to 100 above.
	if (m_player_native.GetSafeHwnd())
	{
		char buf[32];
		_snprintf_s(buf, _TRUNCATE, "Native %d%%", s_pct);
		char cur[32] = {};
		m_player_native.GetWindowText(cur, sizeof(cur));
		if (strcmp(cur, buf) != 0)
			m_player_native.SetWindowText(buf);
	}
	int cx_d = cx_logical * s_pct / 100;
	int cy_d = cy_logical * s_pct / 100;
	int x_d = (avail_w - cx_d) / 2;
	int y_d = (avail_h - cy_d) / 2;
	// Apply right-drag pan when the image is larger than the viewport. Pan
	// is meaningless when the image fits, so suppress it there to avoid
	// confusing offsets after a zoom-out.
	if (cx_d > avail_w) x_d += m_player_pan_x;
	if (cy_d > avail_h) y_d += m_player_pan_y;

	// Paint only the four margins around the (possibly panned) image rect
	// with theme::bg. A full-canvas FillSolidRect every frame would strobe
	// under the image during playback, so we erase only what the image
	// itself doesn't cover. The pan offsets are already baked into x_d/y_d
	// above, so this works for both centered and right-dragged images.
	{
		const COLORREF bg = theme::bg();
		// Top: 0..min(y_d, avail_h)
		int top = std::max(0, std::min(y_d, avail_h));
		if (top > 0)
			pDC->FillSolidRect(0, 0, avail_w, top, bg);
		// Bottom: max(y_d+cy_d, 0)..avail_h
		int bottom = std::max(0, std::min(y_d + cy_d, avail_h));
		if (bottom < avail_h)
			pDC->FillSolidRect(0, bottom, avail_w, avail_h - bottom, bg);
		// Left/right margins are clipped to the band actually covered by the
		// image vertically — outside that band the top/bottom fills already
		// painted those columns.
		int band_top = std::max(0, y_d);
		int band_bot = std::max(band_top, std::min(y_d + cy_d, avail_h));
		int band_h = band_bot - band_top;
		if (band_h > 0)
		{
			int left = std::max(0, std::min(x_d, avail_w));
			if (left > 0)
				pDC->FillSolidRect(0, band_top, left, band_h, bg);
			int right = std::max(0, std::min(x_d + cx_d, avail_w));
			if (right < avail_w)
				pDC->FillSolidRect(right, band_top, avail_w - right, band_h, bg);
		}
	}

	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	HBITMAP h_dib;
	DWORD* p_dib;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(bmi));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = cx_s * cy_s * 4;
		h_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&p_dib), 0, 0);
	}
	HGDIOBJ old = mem_dc.SelectObject(h_dib);
	// Cache-hit flags — lifted out of the conversion scope so the grid
	// overlay + cache-snapshot blocks below can read them.
	bool shp_cache_hit = false;
	bool vxl_cache_hit = false;
	{
		// Paletted SHP/WSA/VXL path with three optional ASE-style modifiers:
		//   - BG off: index 0 paints as alpha-checker (transparent preview).
		//   - Side-color remap: indices 16..31 retinted via brightness * preset.
		//   - Shadow pair: when on and cf is even, blend frame[f + cf/2] black
		//     at 120/255 alpha over the body frame (engine convention).
		//
		// SHP/WSA fast path: the converted BGRA bytes for a given frame are
		// stable until something user-visible changes (palette, side color,
		// shadow toggle, BG toggle, alpha checker). So we cache per frame
		// keyed on m_player_bgra_version. Animation tick on a cached frame
		// drops to a memcpy + StretchBlt — the timer-driven fan-spin from
		// re-running the per-pixel loop 15-30 times per second goes away.
		// VXL still runs the loop every paint because shading scales the
		// VXL splat output (which already has its own splat-level cache).
		const int bg_mode = m_player_bg_mode;
		// SHP and VXL each have their own side-color state. Pick the active
		// one for the current view so the retint applies independently per
		// kind of asset.
		const bool vxl = is_vxl_view();
		const int side = vxl ? m_vxl_side_idx : m_player_side_idx;
		const COLORREF custom_color = vxl ? m_vxl_side_custom_color : m_player_side_custom_color;
		const COLORREF ck_a = theme::checker_a();
		const COLORREF ck_b = theme::checker_b();
		const DWORD ck_a_d = (GetRValue(ck_a) << 16) | (GetGValue(ck_a) << 8) | GetBValue(ck_a);
		const DWORD ck_b_d = (GetRValue(ck_b) << 16) | (GetGValue(ck_b) << 8) | GetBValue(ck_b);
		const COLORREF pane_c = theme::bg();
		const DWORD pane_d = (GetRValue(pane_c) << 16) | (GetGValue(pane_c) << 8) | GetBValue(pane_c);
		float remap_r = 0, remap_g = 0, remap_b = 0;
		if (side >= 0 && side < 8)
		{
			remap_r = GetRValue(k_side_colors[side]);
			remap_g = GetGValue(k_side_colors[side]);
			remap_b = GetBValue(k_side_colors[side]);
		}
		else if (side == 8)
		{
			remap_r = GetRValue(custom_color);
			remap_g = GetGValue(custom_color);
			remap_b = GetBValue(custom_color);
		}
		const bool shadow_on = m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0;
		const byte* sshad = nullptr;
		if (shadow_on)
		{
			int shadow_idx = m_player_frame + m_player_cf / 2;
			if (shadow_idx >= 0 && shadow_idx < static_cast<int>(m_player_frames.size()))
				sshad = m_player_frames[shadow_idx].data();
		}
		const unsigned char* shade_buf = (vxl_shade_p && !vxl_shade_p->empty()) ? vxl_shade_p->data() : nullptr;
		// View > Voxel mode for the VXL composite. Normals/depth paint a
		// grayscale derived from the splat's auxiliary buffers instead of the
		// palette color; nullptr buffers fall back to the color path.
		const int vxl_vmode = vxl ? m_vxl_splat.view_mode : 0;
		const unsigned char* vxl_nidx = (vxl_vmode == 1 && !m_vxl_splat.nidx.empty()) ? m_vxl_splat.nidx.data() : nullptr;
		const short* vxl_depth = (vxl_vmode == 2 && !m_vxl_splat.depth.empty()) ? m_vxl_splat.depth.data() : nullptr;
		const int vxl_dmin = m_vxl_splat.depth_min;
		const int vxl_drange = m_vxl_splat.depth_max - m_vxl_splat.depth_min;
		const int n = cx_s * cy_s;
		// Cache lookup for SHP/WSA. VXL skips this path; it has its own
		// single-buffer BGRA cache below keyed on splat identity + side state.
		if (!vxl)
		{
			const int nf = static_cast<int>(m_player_frames.size());
			if (static_cast<int>(m_player_bgra.size()) != nf)
				m_player_bgra.assign(nf, shp_bgra_cache_entry{});
			if (m_player_frame >= 0 && m_player_frame < nf)
			{
				auto& ce = m_player_bgra[m_player_frame];
				if (ce.version == m_player_bgra_version &&
					static_cast<int>(ce.bgra.size()) == n)
				{
					memcpy(p_dib, ce.bgra.data(), static_cast<size_t>(n) * 4);
					shp_cache_hit = true;
				}
			}
		}
		// VXL: single-buffer BGRA cache. Idle viewing of a still voxel model
		// drops to memcpy + StretchBlt; orbiting still rebuilds because the
		// underlying splat key changes. The cache key includes everything
		// that affects the composited bytes (splat identity, side color,
		// bg, grid, alpha checker).
		if (vxl)
		{
			auto& vc = m_vxl_bgra;
			if (vc.splat_token == m_vxl_splat.token &&
				vc.splat_yaw == m_vxl_splat.yaw &&
				vc.splat_pitch == m_vxl_splat.pitch &&
				vc.splat_ss == m_vxl_splat.ss &&
				vc.splat_shading == m_vxl_splat.shading &&
				vc.view_mode == m_vxl_splat.view_mode &&
				vc.splat_lighting_version == m_vxl_splat.shade_lighting_version &&
				vc.splat_vpl_active == m_vxl_splat.vpl_active &&
				vc.splat_vpl_lighting_version == m_vxl_splat.vpl_lighting_version &&
				vc.side == side &&
				vc.custom_color == custom_color &&
				vc.bg_mode == bg_mode &&
				vc.grid_mode == m_player_grid_mode &&
				vc.ck_a == ck_a &&
				vc.ck_b == ck_b &&
				vc.pane_c == pane_c &&
				vc.cx_s == cx_s &&
				vc.cy_s == cy_s &&
				vc.bgra_version == m_player_bgra_version &&
				static_cast<int>(vc.bgra.size()) == n)
			{
				memcpy(p_dib, vc.bgra.data(), static_cast<size_t>(n) * 4);
				vxl_cache_hit = true;
			}
		}
		// Cache-miss conversion. Two flavors:
		//   SHP/WSA → route through player_fill_bgra_cache_entry so the
		//             cached bytes (including any pixel-art upscale) match
		//             what the prefill produces. Then memcpy from the
		//             entry to p_dib.
		//   VXL     → inline loop here because it composites the shading
		//             buffer that the SHP helper doesn't know about.
		if (!shp_cache_hit && !vxl_cache_hit)
		{
		if (vxl)
		{
		// Parallel for SS>1 — at SS=4..16 the supersample buffer is ~250 K to
		// 4 M pixels and the composite (palette lookup + optional remap +
		// shade multiply) is sizable enough to amortize the fork/join. At
		// SS=1 (~57 K pixels) the gate falls back to serial like the SHP
		// case below. This is the loop that runs every time the lighting
		// pass invalidates the BGRA cache (slider drag), so its cost is
		// what the user feels mid-drag.
		#pragma omp parallel for schedule(static) if(n >= 65536)
		for (int i = 0; i < n; i++)
		{
			byte idx = s[i];
			DWORD bgra;
			if (idx == 0)
			{
				if (bg_mode == 0)
					bgra = m_color_table[0];
				else if (bg_mode == 2)
					bgra = pane_d;
				else
				{
					int x = i % cx_s, y = i / cx_s;
					bgra = (((x >> 3) ^ (y >> 3)) & 1) ? ck_b_d : ck_a_d;
				}
			}
			else if (vxl_nidx)
			{
				// Surface-normals view: grayscale of the stored normal index
				// (ramp baked into nidx by the splat). No palette, no side
				// remap, no relighting — a faithful diagnostic readout.
				int g = vxl_nidx[i];
				bgra = static_cast<DWORD>(g) | (static_cast<DWORD>(g) << 8) | (static_cast<DWORD>(g) << 16);
			}
			else if (vxl_depth)
			{
				// Depth view: normalize the winning z over the occupied range.
				// Brighter = nearer the camera, matching the static grid view.
				int g = (vxl_drange > 0) ? ((vxl_depth[i] - vxl_dmin) * 255 / vxl_drange) : 255;
				if (g < 0) g = 0; else if (g > 255) g = 255;
				bgra = static_cast<DWORD>(g) | (static_cast<DWORD>(g) << 8) | (static_cast<DWORD>(g) << 16);
			}
			else
			{
				bgra = m_color_table[idx];
				if (side >= 0 && idx >= 16 && idx <= 31)
				{
					float b = static_cast<float>(bgra & 0xff);
					float g = static_cast<float>((bgra >> 8) & 0xff);
					float r = static_cast<float>((bgra >> 16) & 0xff);
					float bright = std::max(r, std::max(g, b)) / 255.0f * 1.25f;
					int rr = static_cast<int>(remap_r * bright + 0.5f);
					int gg = static_cast<int>(remap_g * bright + 0.5f);
					int bb = static_cast<int>(remap_b * bright + 0.5f);
					if (rr > 255) rr = 255; if (gg > 255) gg = 255; if (bb > 255) bb = 255;
					bgra = static_cast<DWORD>(bb) | (static_cast<DWORD>(gg) << 8) | (static_cast<DWORD>(rr) << 16);
				}
			}
			// VXL directional shading: scale the voxel color by the per-pixel
			// shade factor that the splat wrote (128 = neutral 1.0). Skips
			// background pixels (idx == 0) so the bg color / alpha-checker stay
			// at full intensity. Color view only — normals/depth are grayscale
			// diagnostics that must not be relit.
			if (shade_buf && idx != 0 && vxl_vmode == 0)
			{
				int sb = shade_buf[i];
				int rr = static_cast<int>(((bgra >> 16) & 0xff) * sb / 128);
				int gg = static_cast<int>(((bgra >> 8)  & 0xff) * sb / 128);
				int bb = static_cast<int>((bgra         & 0xff) * sb / 128);
				if (rr > 255) rr = 255;
				if (gg > 255) gg = 255;
				if (bb > 255) bb = 255;
				bgra = static_cast<DWORD>(bb) | (static_cast<DWORD>(gg) << 8) | (static_cast<DWORD>(rr) << 16);
			}
			p_dib[i] = bgra;
		}
		}
		else
		{
			// SHP/WSA cache-miss path. Fill the cache entry (handles
			// optional upscaler), then blit into p_dib. The cache size
			// matches cx_s*cy_s (DIB allocation accounts for the upscale
			// factor), so the memcpy is straight.
			const int nf = static_cast<int>(m_player_bgra.size());
			if (m_player_frame >= 0 && m_player_frame < nf)
			{
				auto& ce = m_player_bgra[m_player_frame];
				player_fill_bgra_cache_entry(m_player_frame, ce);
				if (static_cast<int>(ce.bgra.size()) == n)
					memcpy(p_dib, ce.bgra.data(), static_cast<size_t>(n) * 4);
				else
				{
					// Shouldn't happen — the helper sizes the buffer to
					// cx_upscaled*cy_upscaled and cx_s/cy_s mirror that
					// math. Fall back to a source-res render so we paint
					// *something* rather than uninitialized DIB bytes.
					player_convert_frame_to_bgra(m_player_frame, p_dib);
				}
			}
			else
			{
				// No backing cache entry — fall back to the source-res
				// render. Only reachable if frame_idx is out of range,
				// which shouldn't happen in practice.
				player_convert_frame_to_bgra(m_player_frame, p_dib);
			}
		}
		}
	}
	// Game Grid overlay (isometric guide, ASE convention). Drawn into the
	// source DIB before scaling so the lines participate in the chosen
	// interpolation. SHP path gets the grid baked into its cached BGRA via
	// player_convert_frame_to_bgra; this per-paint pass is VXL-only and only
	// runs when the VXL cache missed (otherwise the cached bytes already
	// contain the grid).
	// Grid math runs in *logical* (non-supersampled) space — derive logical
	// coords by dividing the SS pixel position by ss. Without this the tile
	// width and line thickness shrink proportionally with SS, so at SS=4 the
	// 48/60-px tiles become 12/15 output pixels and the 2-px line becomes
	// 0.5 output pixels (blurred to invisibility by bilinear/lanczos).
	if (is_vxl_view() && !vxl_cache_hit && m_player_grid_mode > 0)
	{
		const int ss_grid = (m_player_cx > 0) ? (cx_s / m_player_cx) : 1;
		const int tileW = (m_player_grid_mode == 1) ? 48 : 60;
		const int gcx_l = m_player_cx / 2;
		const int gcy_l = m_player_cy;
		const DWORD line = 0x00FFFFFF;
		// Serial; same sizing reasoning as the BGRA loop above.
		for (int py = 0; py < cy_s; py++)
		{
			int ly = (ss_grid > 0) ? (py / ss_grid) : py;
			for (int px = 0; px < cx_s; px++)
			{
				int i = px + cx_s * py;
				if (s[i] != 0) continue; // sprite pixel — keep
				int lx = (ss_grid > 0) ? (px / ss_grid) : px;
				int dx = lx - gcx_l;
				int dy = ly - gcy_l;
				int u = dx + 2 * dy;
				int v = 2 * dy - dx;
				int au = u % tileW; if (au < 0) au += tileW;
				int av = v % tileW; if (av < 0) av += tileW;
				if (au < 2 || av < 2)
					p_dib[i] = line;
			}
		}
	}
	// Snapshot the per-paint output into the VXL BGRA cache. SHP's cache
	// is already populated by player_fill_bgra_cache_entry in the cache-
	// miss path above, so it doesn't need a separate snapshot here.
	if (m_is_open && m_player_mode)
	{
		const int n = cx_s * cy_s;
		const bool vxl_view = is_vxl_view();
		if (vxl_view && !vxl_cache_hit)
		{
			auto& vc = m_vxl_bgra;
			vc.splat_token = m_vxl_splat.token;
			vc.splat_yaw = m_vxl_splat.yaw;
			vc.splat_pitch = m_vxl_splat.pitch;
			vc.splat_ss = m_vxl_splat.ss;
			vc.splat_shading = m_vxl_splat.shading;
			vc.view_mode = m_vxl_splat.view_mode;
			vc.splat_lighting_version = m_vxl_splat.shade_lighting_version;
			vc.splat_vpl_active = m_vxl_splat.vpl_active;
			vc.splat_vpl_lighting_version = m_vxl_splat.vpl_lighting_version;
			vc.side = m_vxl_side_idx;
			vc.custom_color = m_vxl_side_custom_color;
			vc.bg_mode = m_player_bg_mode;
			vc.grid_mode = m_player_grid_mode;
			vc.ck_a = theme::checker_a();
			vc.ck_b = theme::checker_b();
			vc.pane_c = theme::bg();
			vc.cx_s = cx_s;
			vc.cy_s = cy_s;
			vc.bgra_version = m_player_bgra_version;
			vc.bgra.assign(p_dib, p_dib + n);
		}
	}
	// Clip the blit to the image area so a Ctrl+wheel-zoomed sprite that's
	// larger than the available canvas doesn't paint over the player control
	// band beneath it. Without this, an oversize SHP/WSA bleeds through and
	// the buttons end up textured by the sprite's pixels.
	pDC->SaveDC();
	pDC->IntersectClipRect(0, 0, avail_w, avail_h);
	// SHP/WSA: scale at the user's chosen interpolation. VXL: same — but the
	// source buffer is already supersampled (2x), so Bilinear/Bicubic/Lanczos
	// run as a real downscale and produce silhouette AA. Nearest gives sharp
	// blocky voxels (each voxel covers 2x2 in the supersample buffer, so the
	// downscale to 1x naturally lands on pixel boundaries).
	// Force nearest while orbiting a VXL or panning. Bilinear/Bicubic/Lanczos
	// parallel regions otherwise fire on every WM_MOUSEMOVE — at 1000Hz mouse
	// polling, that's 1000 fork/joins per second of drag, pegging cores.
	// Eyes can't resolve Lanczos quality mid-motion at 60fps anyway, and the
	// VXL splat already does silhouette AA via supersampling, so dropping to
	// GDI StretchBlt during the drag is visually nearly identical and orders
	// of magnitude cheaper. The user's chosen mode is restored on release.
	const bool dragging = m_vxl_dragging || m_player_panning;
	if (dragging)
		theme::stretch_image(pDC, x_d, y_d, cx_d, cy_d, &mem_dc, h_dib, p_dib, cx_s, cy_s, theme::interp_nearest);
	else if (!is_vxl_view() && theme::interp_is_pixel_art_upscaler())
		// SHP/WSA + pixel-art upscaler: the cache already holds the
		// algorithmically reconstructed image at 2x/3x/4x. Running
		// bicubic or Lanczos on top adds blur. Force bilinear for the
		// stretch step so we just smoothly resample to the viewport.
		theme::stretch_image(pDC, x_d, y_d, cx_d, cy_d, &mem_dc, h_dib, p_dib, cx_s, cy_s, theme::interp_bilinear);
	else
		theme::stretch_image(pDC, x_d, y_d, cx_d, cy_d, &mem_dc, h_dib, p_dib, cx_s, cy_s);
	pDC->RestoreDC(-1);
	mem_dc.SelectObject(old);
	DeleteObject(h_dib);

	// Light-direction indicator overlay. Drawn after the model so it sits on
	// top of voxels. Active only when the VXL Lighting dialog is open
	// (theme::vxl_light_indicator_visible). Mode:
	//   overlay: line from model center to sun-disc, scaled to ~35% of canvas.
	//   corner:  small widget in the pane's top-right (32px radius).
	//            Pinned to the view client rect (NOT the canvas) so it stays
	//            put as zoom changes and never overlaps the model.
	//
	// Axis mapping — must match the *shading* path, not raw camera space:
	//   The splat stores normals as cam_normal = (nrx, -nry_p, nrz_p), so a
	//   voxel whose post-rotation normal points down in screen (nry_p > 0)
	//   has ni8y < 0 and is bright when vpl_ly < 0. Therefore a *negative*
	//   vpl_ly means "light is coming from below", and the indicator must
	//   show the sun *below* center. Screen-Y in GDI is +down, so we negate
	//   ly when placing the sun. X has no flip (ni8x = nrx), so screen-X = +lx.
	//   Depth (lz): +lz means light is behind the model (away from camera),
	//   so the sun should *shrink*, not grow. Modulate radius by (1 - 0.3*lz).
	if (is_vxl_view() && theme::vxl_light_indicator_visible())
	{
		// Indicator vector mirrors the shading frame so it always points at
		// the side that's actually lit. world_fixed: rotate the world-space
		// light by live yaw+pitch+Y-flip (so the indicator tracks the model
		// as the camera orbits). camera_fixed: skip the rotation -- the
		// indicator stays put on screen, matching where the light is
		// effectively coming from.
		float lx, ly, lz;
		{
			float lwx, lwy, lwz;
			theme::vxl_light_direction(lwx, lwy, lwz);
			if (theme::vxl_light_frame_v() == theme::vlf_world_fixed)
			{
				const double cY = std::cos(m_vxl_yaw);
				const double sY = std::sin(m_vxl_yaw);
				const double cP = std::cos(m_vxl_pitch);
				const double sP = std::sin(m_vxl_pitch);
				float lrx = static_cast<float>(lwx * cY - lwy * sY);
				float lry = static_cast<float>(lwx * sY + lwy * cY);
				float lry_p = static_cast<float>(lry * cP - lwz * sP);
				float lrz_p = static_cast<float>(lry * sP + lwz * cP);
				lx = lrx;
				ly = -lry_p;	// match cam_normal's Y flip / shading axis mapping
				lz = lrz_p;
			}
			else
			{
				lx = lwx;
				ly = lwy;
				lz = lwz;
			}
		}

		int center_x, center_y, radius;
		if (theme::vxl_light_indicator_mode() == theme::vxl_light_indicator_corner)
		{
			// Pin to the pane's top-right (above the player band), not to the
			// canvas (x_d+cx_d / y_d). Anchoring to the canvas made the widget
			// overlap the model at low zoom on small voxels (canvas < 2*(margin
			// +widget_r)) and drift inward as zoom shrank the canvas.
			const int margin = 12;
			const int widget_r = 32;
			CRect cr;
			GetClientRect(&cr);
			center_x = cr.right - margin - widget_r;
			center_y = cr.top + margin + widget_r;
			radius = widget_r;
		}
		else
		{
			center_x = x_d + cx_d / 2;
			center_y = y_d + cy_d / 2;
			radius = static_cast<int>(std::min(cx_d, cy_d) * 0.35f);
		}
		const int light_x = center_x + static_cast<int>(lx * radius);
		const int light_y = center_y - static_cast<int>(ly * radius);
		const int sun_r = std::max(3, static_cast<int>(radius * 0.08f * (1.0f - 0.3f * lz)));

		// Corner mode: draw a faint background disc so the widget is readable
		// over any voxel color underneath. Overlay mode skips this so the
		// model stays visible.
		const COLORREF c_ring = RGB(255, 200, 0);
		const COLORREF c_line = RGB(255, 220, 80);
		const COLORREF c_sun  = RGB(255, 240, 120);
		const COLORREF c_bg   = theme::is_dark() ? RGB(30, 30, 30) : RGB(240, 240, 240);

		if (theme::vxl_light_indicator_mode() == theme::vxl_light_indicator_corner)
		{
			CBrush bg_brush(c_bg);
			CPen bg_pen(PS_SOLID, 1, c_ring);
			CBrush* old_brush = pDC->SelectObject(&bg_brush);
			CPen* old_pen = pDC->SelectObject(&bg_pen);
			pDC->Ellipse(center_x - radius, center_y - radius,
			             center_x + radius, center_y + radius);
			pDC->SelectObject(old_brush);
			pDC->SelectObject(old_pen);
		}

		// Line from center to sun.
		CPen line_pen(PS_SOLID, 2, c_line);
		CPen* old_pen = pDC->SelectObject(&line_pen);
		pDC->MoveTo(center_x, center_y);
		pDC->LineTo(light_x, light_y);
		pDC->SelectObject(old_pen);

		// Sun disc.
		CBrush sun_brush(c_sun);
		CPen sun_pen(PS_SOLID, 1, c_ring);
		CBrush* old_brush = pDC->SelectObject(&sun_brush);
		old_pen = pDC->SelectObject(&sun_pen);
		pDC->Ellipse(light_x - sun_r, light_y - sun_r,
		             light_x + sun_r, light_y + sun_r);
		pDC->SelectObject(old_brush);
		pDC->SelectObject(old_pen);

		// Small dot at center to mark the anchor.
		const int dot_r = 2;
		CBrush dot_brush(c_ring);
		CPen dot_pen(PS_SOLID, 1, c_ring);
		old_brush = pDC->SelectObject(&dot_brush);
		old_pen = pDC->SelectObject(&dot_pen);
		pDC->Ellipse(center_x - dot_r, center_y - dot_r,
		             center_x + dot_r, center_y + dot_r);
		pDC->SelectObject(old_brush);
		pDC->SelectObject(old_pen);
	}
}

void CXCCFileView::OnSize(UINT nType, int cx, int cy)
{
	CScrollView::OnSize(nType, cx, cy);
	if (m_player_mode)
		player_layout_controls();
}

void CXCCFileView::OnPaint()
{
	// Rate-limit is enforced at the invalidate side (request_repaint) and
	// at the input side (throttle_input_tick), so this just forwards.
	CScrollView::OnPaint();
}

bool CXCCFileView::throttle_input_tick()
{
	// Input-rate gate that matches the paint cap. Drops mouse-move /
	// slider-tick events that arrive within the cap window. At 15 fps that
	// means at most ~15 accepted ticks/sec — the dropped 985 don't run any
	// of the orbit math, request_repaint, or slider-side work. Mouse-poll
	// rate (1000Hz) was the actual CPU eater, not the paints.
	const int fps = theme::frame_rate_cap();
	const DWORD min_ms = static_cast<DWORD>(1000 / (fps > 0 ? fps : 60));
	const DWORD now = ::GetTickCount();
	if (m_last_input_ms != 0 && (now - m_last_input_ms) < min_ms)
		return false;
	m_last_input_ms = now;
	return true;
}

void CXCCFileView::request_repaint(LPCRECT rect)
{
	// Rate-limited invalidate. Multiple calls within the cap window are
	// coalesced into a single deferred invalidate via TIMER_FRAME_LIMIT_ID
	// so we don't even *queue* extra WM_PAINTs at high mouse-poll rates.
	// Leading + trailing edge: the first call after a quiet period
	// invalidates immediately; calls within the cap window mark the rect
	// dirty in m_pending_rect and arm the timer.
	const int fps = theme::frame_rate_cap();
	const DWORD min_ms = static_cast<DWORD>(1000 / (fps > 0 ? fps : 60));
	const DWORD now = ::GetTickCount();
	const DWORD elapsed = now - m_last_paint_ms;
	CRect r;
	if (rect) r = *rect; else { GetClientRect(&r); }
	if (m_last_paint_ms != 0 && elapsed < min_ms)
	{
		// Defer: union into the pending rect and arm the timer if not
		// already armed.
		if (m_paint_pending)
			m_pending_rect.UnionRect(&m_pending_rect, &r);
		else
		{
			m_pending_rect = r;
			m_paint_pending = true;
		}
		if (!m_timer_armed)
		{
			DWORD remaining = (elapsed < min_ms) ? (min_ms - elapsed) : 1;
			SetTimer(TIMER_FRAME_LIMIT_ID, remaining, NULL);
			m_timer_armed = true;
		}
		return;
	}
	// Window open: invalidate now and update the timestamp.
	m_last_paint_ms = now;
	m_paint_pending = false;
	InvalidateRect(&r, FALSE);
}

void CXCCFileView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_FRAME_LIMIT_ID)
	{
		KillTimer(TIMER_FRAME_LIMIT_ID);
		m_timer_armed = false;
		if (m_paint_pending)
		{
			m_paint_pending = false;
			m_last_paint_ms = ::GetTickCount();
			InvalidateRect(&m_pending_rect, FALSE);
		}
		return;
	}
	if (nIDEvent == 1 && m_player_mode && m_player_playing && m_player_cf > 0)
	{
		int range = m_player_cf;
		if (m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0)
			range = m_player_cf / 2;
		int next;
		// HVA mode with Loop off: stop at the end (Vengi-style one-shot).
		const bool loop = !(m_ft == ft_vxl && m_hva_loaded) || m_hva_loop;
		if (m_player_reverse_dir)
		{
			next = m_player_frame - 1;
			if (next < 0)
			{
				if (!loop) { m_player_playing = false; KillTimer(1); return; }
				next = range - 1;
			}
		}
		else
		{
			next = m_player_frame + 1;
			if (next >= range)
			{
				if (!loop) { m_player_playing = false; KillTimer(1); return; }
				next = 0;
			}
		}
		player_set_frame(next);
		return;
	}
	CScrollView::OnTimer(nIDEvent);
}

void CXCCFileView::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (m_player_mode && pScrollBar && pScrollBar->GetSafeHwnd() == m_player_slider.GetSafeHwnd())
	{
		int pos = m_player_slider.GetPos();
		if (pos != m_player_frame)
		{
			// Pause auto-advance while the user is scrubbing — prevents the
			// timer from fighting the drag and stuttering between two frames.
			if (m_player_playing && nSBCode == TB_THUMBTRACK)
				KillTimer(1);
			m_player_frame = pos;
			player_update_label();
			// VXL+HVA scrubbing: rebuild the point cloud for the new frame
			// and bump m_open_token so the splat cache (keyed on token)
			// invalidates and the next paint re-rasterizes. Without this the
			// slider moves but the model stays on the cloud built at the
			// original frame.
			if (m_ft == ft_vxl && m_hva_loaded)
			{
				player_decode_frames();
				m_open_token++;
			}
			// Partial invalidate of the image area only, no erase. Full
			// Invalidate() would repaint the controls band too, making the
			// slider thumb flicker on every drag tick.
			CRect cr; GetClientRect(&cr);
			cr.bottom -= player_band_h();
			if (cr.bottom < cr.top) cr.bottom = cr.top;
			InvalidateRect(&cr, FALSE);
			// Resume the timer when the drag ends.
			if (m_player_playing && nSBCode == TB_ENDTRACK)
				SetTimer(1, 1000 / std::max(1, m_player_fps), NULL);
		}
		return;
	}
	if (!handle_thumb_scroll_32bit(SB_HORZ, nSBCode, pScrollBar))
		CScrollView::OnHScroll(nSBCode, nPos, pScrollBar);
}

// WM_VSCROLL/WM_HSCROLL pack the thumb position into the low 16 bits of
// wParam, so MFC's nPos arg wraps once the real position passes 32767. For
// content taller/wider than ~32k px (big MIX listings, hex dumps) this makes
// the scrollbar snap to 0 mid-drag. For thumb codes, read the real 32-bit
// position from SCROLLINFO::nTrackPos via GetScrollInfo and scroll directly;
// other codes (line/page/end) fall through to the base class.
bool CXCCFileView::handle_thumb_scroll_32bit(int bar, UINT nSBCode, CScrollBar* pScrollBar)
{
	if ((nSBCode != SB_THUMBTRACK && nSBCode != SB_THUMBPOSITION) || pScrollBar != NULL)
		return false;
	SCROLLINFO si = { sizeof(si), SIF_TRACKPOS | SIF_PAGE | SIF_RANGE };
	if (!GetScrollInfo(bar, &si))
		return false;
	CPoint p = GetScrollPosition();
	if (bar == SB_HORZ) p.x = si.nTrackPos; else p.y = si.nTrackPos;
	ScrollToPosition(p);
	return true;
}

void CXCCFileView::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (!handle_thumb_scroll_32bit(SB_VERT, nSBCode, pScrollBar))
		CScrollView::OnVScroll(nSBCode, nPos, pScrollBar);
}

void CXCCFileView::OnPlayerPlay()
{
	player_toggle_play();
}

void CXCCFileView::OnPlayerGrid()
{
	player_exit();
}

void CXCCFileView::OnPlayerReverse()
{
	if (!m_player_controls_created)
		return;
	m_player_reverse_dir = m_player_reverse.GetCheck() == BST_CHECKED;
}

void CXCCFileView::OnPlayerNative()
{
	if (!m_player_controls_created)
		return;
	m_player_native_size = m_player_native.GetCheck() == BST_CHECKED;
	// Native is an explicit user choice; clear any wheel-zoom override so it
	// takes effect (otherwise the override would silently shadow the toggle).
	m_player_zoom_pct = 0;
	m_player_pan_x = m_player_pan_y = 0;
	CRect cr;
	GetClientRect(&cr);
	cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, TRUE);
}

// Helper: locate the GDI+ PNG encoder CLSID. Standard MSDN snippet.
static int get_png_encoder_clsid(CLSID& out)
{
	UINT num = 0, size = 0;
	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0) return -1;
	std::vector<byte> buf(size);
	auto* infos = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
	if (Gdiplus::GetImageEncoders(num, size, infos) != Gdiplus::Ok) return -1;
	for (UINT i = 0; i < num; i++)
	{
		if (wcscmp(infos[i].MimeType, L"image/png") == 0)
		{
			out = infos[i].Clsid;
			return 0;
		}
	}
	return -1;
}

// Helper: write a 32-bit top-down BMP from BGRA bytes.
// 32-bit uncompressed top-down TGA writer. Replaces the previous 32-bit BMP
// writer in v9.62 — TGA carries a real alpha channel that every image editor
// (Photoshop, GIMP, Krita, …) interprets correctly, while 32-bit BMPs are
// historically ambiguous: some viewers treat the high byte as alpha, some as
// padding. Switching to TGA also matches Westwood-tool conventions.
//
// Layout: 18-byte header, then cx*cy BGRA pixels. ImageDescriptor bit 5 = 1
// flips origin to top-left so we don't have to row-flip the buffer; bits 0..3
// = 8 declare 8 alpha bits. Pixel order BGRA matches our cache layout.
static bool write_tga32(const char* path, const byte* bgra, int cx, int cy)
{
	if (!bgra || cx <= 0 || cy <= 0) return false;
	byte hdr[18] = {};
	hdr[2] = 2;                 // image type: uncompressed true-color
	hdr[12] = static_cast<byte>(cx & 0xff);
	hdr[13] = static_cast<byte>((cx >> 8) & 0xff);
	hdr[14] = static_cast<byte>(cy & 0xff);
	hdr[15] = static_cast<byte>((cy >> 8) & 0xff);
	hdr[16] = 32;               // bits per pixel
	hdr[17] = 0x28;             // ImageDescriptor: top-left origin (bit5) + 8 alpha bits (bits0..3)
	const DWORD pixel_bytes = static_cast<DWORD>(cx) * cy * 4;
	FILE* f = std::fopen(path, "wb");
	if (!f) return false;
	bool ok = std::fwrite(hdr, sizeof(hdr), 1, f) == 1
		&& std::fwrite(bgra, pixel_bytes, 1, f) == 1;
	std::fclose(f);
	return ok;
}

// Capture the currently-rendered frame into a freshly-sized BGRA vector with
// alpha derived from BG mode (per the v9.62 indexed-buffer rule). Used by both
// the single-shot screenshot path and the turntable capture loop. UpdateWindow()
// is the caller's responsibility — turntable batches it across N frames so we
// don't re-pump messages from inside the hot loop.
bool CXCCFileView::capture_current_frame(std::vector<DWORD>& out_bgra, int& out_cx, int& out_cy,
	bool wysiwyg)
{
	if (!m_player_mode || !m_is_open)
		return false;
	const bool vxl = is_vxl_view();

	// VXL: cx_s/cy_s include supersampling. SHP/WSA/etc.: native player size,
	// multiplied by the pixel-art upscale factor when one is active (the BGRA
	// cache entries hold post-upscale pixels — see player_fill_bgra_cache_entry).
	int cx_s = 0, cy_s = 0;
	int shp_upscale = 1;
	if (vxl)
	{
		cx_s = m_vxl_splat.cx_s;
		cy_s = m_vxl_splat.cy_s;
	}
	else
	{
		shp_upscale = shp_effective_upscale_factor();
		cx_s = m_player_cx * shp_upscale;
		cy_s = m_player_cy * shp_upscale;
	}
	if (cx_s <= 0 || cy_s <= 0)
		return false;

	const int n_pixels = cx_s * cy_s;
	const DWORD* src_dwords = nullptr;
	if (vxl)
	{
		if (static_cast<int>(m_vxl_bgra.bgra.size()) == n_pixels)
			src_dwords = m_vxl_bgra.bgra.data();
	}
	else
	{
		if (m_player_frame >= 0 && m_player_frame < static_cast<int>(m_player_bgra.size())
			&& static_cast<int>(m_player_bgra[m_player_frame].bgra.size()) == n_pixels)
		{
			src_dwords = m_player_bgra[m_player_frame].bgra.data();
		}
	}
	if (!src_dwords)
		return false;

	// BG-mode-driven alpha: in Alpha (1) / Pane (2) the user wants real
	// transparency in the output; pull from the indexed buffer because the
	// BGRA cache has bg color baked into transparent pixels. Color (0): keep
	// the image fully opaque so engine appearance is preserved.
	// wysiwyg short-circuits the indexed-buffer alpha path: every output
	// pixel falls through to the (src_dwords | 0xFF000000) branch below,
	// preserving whatever the cache builder baked in for transparent
	// pixels (palette-0 color in BG=Color, 8x8 checker in BG=Alpha, pane
	// color in BG=Pane). This is what Record wants — match the on-screen
	// view exactly. The single-shot screenshot path leaves wysiwyg=false
	// and keeps the v9.62 real-transparency behavior in Alpha/Pane modes.
	const bool want_alpha = !wysiwyg && (m_player_bg_mode == 1 || m_player_bg_mode == 2);
	const byte* indexed = nullptr;
	const int idx_cx = vxl ? cx_s : m_player_cx;
	const int idx_cy = vxl ? cy_s : m_player_cy;
	if (want_alpha)
	{
		if (vxl)
		{
			if (m_vxl_splat.buf.size() != 0) indexed = m_vxl_splat.buf.data();
		}
		else
		{
			const int idx_n = idx_cx * idx_cy;
			if (m_player_frame >= 0 && m_player_frame < static_cast<int>(m_player_frames.size())
				&& static_cast<int>(m_player_frames[m_player_frame].size()) >= idx_n)
			{
				indexed = m_player_frames[m_player_frame].data();
			}
		}
	}

	out_bgra.assign(n_pixels, 0);
	if (indexed)
	{
		// VXL always uses idx 0 as "no voxel"; SHP/WSA respects the
		// player's configurable BG idx (0 for RA2/TS, 4 for TD/RA1).
		const byte bg = vxl ? byte(0) : static_cast<byte>(m_player_bg_idx);
		// When a pixel-art upscaler is active the BGRA buffer is upscaled but
		// the indexed buffer stays at native res — nearest-sample the index
		// per output pixel so the alpha mask matches the BGRA cache layout.
		if (!vxl && shp_upscale != 1)
		{
			for (int y = 0; y < cy_s; y++)
			{
				const int sy = y / shp_upscale;
				const byte* srow = indexed + static_cast<size_t>(sy) * idx_cx;
				DWORD* drow = out_bgra.data() + static_cast<size_t>(y) * cx_s;
				const DWORD* brow = src_dwords + static_cast<size_t>(y) * cx_s;
				for (int x = 0; x < cx_s; x++)
				{
					const int sx = x / shp_upscale;
					const DWORD a = srow[sx] == bg ? 0u : 0xFF000000u;
					drow[x] = srow[sx] == bg ? 0u : (brow[x] | a);
				}
			}
		}
		else
		{
			for (int i = 0; i < n_pixels; i++)
			{
				const DWORD a = indexed[i] == bg ? 0u : 0xFF000000u;
				out_bgra[i] = indexed[i] == bg ? 0u : (src_dwords[i] | a);
			}
		}
	}
	else
	{
		for (int i = 0; i < n_pixels; i++)
			out_bgra[i] = src_dwords[i] | 0xFF000000u;
	}

	out_cx = cx_s;
	out_cy = cy_s;
	return true;
}

bool CXCCFileView::take_screenshot()
{
	// Player mode + a renderable frame is the prerequisite. Outside player mode
	// (grid view), the BGRA caches don't exist; nudge the user instead of
	// silently failing.
	if (!m_player_mode || !m_is_open)
	{
		if (CMainFrame* mf = GetMainFrame())
			mf->SetMessageText("Press P to enter player mode before taking a screenshot.");
		return false;
	}
	const bool vxl = is_vxl_view();

	// Force any pending invalidate to actually paint so the BGRA cache is
	// fresh under the current settings (slider commits etc.).
	UpdateWindow();

	// Resolve output dimensions:
	//   VXL: cx_s/cy_s already include supersampling (cx_logical * SS).
	//   SHP/WSA/PCX/CPS/TMP: cx_s = m_player_cx, cy_s = m_player_cy (native).
	int cx_s = 0, cy_s = 0;
	if (vxl)
	{
		cx_s = m_vxl_splat.cx_s;
		cy_s = m_vxl_splat.cy_s;
	}
	else
	{
		cx_s = m_player_cx;
		cy_s = m_player_cy;
	}
	if (cx_s <= 0 || cy_s <= 0)
	{
		if (CMainFrame* mf = GetMainFrame())
			mf->SetMessageText("Screenshot: no rendered frame available.");
		return false;
	}

	// Build a default filename from the loaded asset's basename. m_fname is
	// the basename within the source MIX (or just the disk filename); strip
	// the extension before appending our own.
	CString default_name = m_fname.empty() ? "screenshot" : m_fname.c_str();
	{
		int dot = default_name.ReverseFind('.');
		if (dot > 0) default_name = default_name.Left(dot);
		if (m_player_frame > 0)
		{
			CString frame_suffix;
			frame_suffix.Format("_f%d", m_player_frame);
			default_name += frame_suffix;
		}
		default_name += ".png";
	}

	// Save dialog with three filters. The dialog's selected filter drives the
	// default extension via the filter index — order: PNG (default), TGA, PCX.
	// PNG is first because it's the lossless format users actually want for
	// sharing screenshots, and (along with TGA) it carries a real alpha
	// channel — see the bg_mode-driven transparency block below.
	CFileDialog dlg(FALSE, "png", default_name,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		"PNG (*.png)|*.png|Targa (*.tga)|*.tga|PCX (*.pcx)|*.pcx||",
		this);
	if (dlg.DoModal() != IDOK)
		return false;
	CString path = dlg.GetPathName();
	const int filter_idx = static_cast<int>(dlg.m_ofn.nFilterIndex);
	// Sanity: if user typed a filename without an extension, append one based
	// on the chosen filter. CFileDialog usually does this with lpstrDefExt for
	// filter 1 only, so handle 2/3 manually.
	if (path.ReverseFind('.') <= path.ReverseFind('\\'))
	{
		path += (filter_idx == 2) ? ".tga" : (filter_idx == 3) ? ".pcx" : ".png";
	}

	// Determine target format from filter index (preferred) with fallback to
	// the extension the user actually typed — covers the case where the user
	// typed "foo.tga" but left the filter on PNG.
	enum { fmt_png, fmt_tga, fmt_pcx } fmt;
	if      (filter_idx == 2) fmt = fmt_tga;
	else if (filter_idx == 3) fmt = fmt_pcx;
	else                      fmt = fmt_png;
	{
		CString ext = path.Right(4);
		ext.MakeLower();
		if      (ext == ".png") fmt = fmt_png;
		else if (ext == ".tga") fmt = fmt_tga;
		else if (ext == ".pcx") fmt = fmt_pcx;
	}

	// SHP/WSA zoom-aware output: scale captured buffers to the player's
	// current effective zoom so the saved file matches what the user sees.
	// VXL skipped — its cx_s/cy_s already include supersampling and zoom is a
	// logical-pixel concept that doesn't compose cleanly with the splat size.
	// BGRA uses bilinear (matches the on-screen view's default), indexed is
	// always nearest (palette indices can't be blended).
	const int z_pct = vxl ? 100 : player_effective_zoom_pct();
	const int zoom_cx = (z_pct == 100) ? cx_s : std::max(1, cx_s * z_pct / 100);
	const int zoom_cy = (z_pct == 100) ? cy_s : std::max(1, cy_s * z_pct / 100);

	bool ok = false;
	if (fmt == fmt_pcx)
	{
		// PCX: raw 8-bit indexed pixels + 256-entry RGB palette. Ignores
		// side-color tint / BG mode / shadows so the output round-trips into
		// other Westwood tools. Source pixels come from the pre-composite
		// indexed buffer (m_vxl_splat.buf for VXL, m_player_frames[n] for
		// SHP/WSA).
		const byte* indexed = nullptr;
		if (vxl)
		{
			if (m_vxl_splat.buf.size() != 0) indexed = m_vxl_splat.buf.data();
		}
		else
		{
			if (m_player_frame >= 0 && m_player_frame < static_cast<int>(m_player_frames.size())
				&& static_cast<int>(m_player_frames[m_player_frame].size()) >= cx_s * cy_s)
			{
				indexed = m_player_frames[m_player_frame].data();
			}
		}
		if (!indexed)
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Screenshot: indexed pixel buffer unavailable; try BMP or PNG.");
			return false;
		}
		// Build the palette. m_color_table is the already-gamma-corrected
		// (6-bit -> 8-bit) BGRA palette that on-screen rendering uses. Reading
		// from it guarantees PCX colors match what BMP/PNG export — without
		// it the raw 6-bit palette values get written, producing a ~25%-
		// brightness PCX that looks darker than the BMP/PNG versions.
		t_palette pal;
		for (int i = 0; i < 256; i++)
		{
			DWORD bgra = m_color_table[i];
			pal[i].b = static_cast<byte>(bgra & 0xff);
			pal[i].g = static_cast<byte>((bgra >> 8) & 0xff);
			pal[i].r = static_cast<byte>((bgra >> 16) & 0xff);
		}
		std::vector<byte> zoomed_indexed;
		const byte* pcx_src = indexed;
		int pcx_cx = cx_s, pcx_cy = cy_s;
		if (!vxl && z_pct != 100)
		{
			zoomed_indexed.assign(static_cast<size_t>(zoom_cx) * zoom_cy, 0);
			for (int y = 0; y < zoom_cy; y++)
			{
				const int sy = std::min(cy_s - 1, (y * 2 + 1) * cy_s / (zoom_cy * 2));
				const byte* srow = indexed + static_cast<size_t>(sy) * cx_s;
				byte* drow = zoomed_indexed.data() + static_cast<size_t>(y) * zoom_cx;
				for (int x = 0; x < zoom_cx; x++)
				{
					const int sx = std::min(cx_s - 1, (x * 2 + 1) * cx_s / (zoom_cx * 2));
					drow[x] = srow[sx];
				}
			}
			pcx_src = zoomed_indexed.data();
			pcx_cx = zoom_cx;
			pcx_cy = zoom_cy;
		}
		const int rv = pcx_file_write(std::string(path), pcx_src, pal, pcx_cx, pcx_cy, 1);
		ok = (rv == 0);
	}
	else
	{
		// PNG / TGA: capture the composited BGRA frame with BG-mode-driven
		// alpha (the v9.62 indexed-buffer rule lives in capture_current_frame).
		std::vector<DWORD> out;
		int out_cx = 0, out_cy = 0;
		if (!capture_current_frame(out, out_cx, out_cy))
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Screenshot: rendered buffer unavailable; try toggling the player or moving a slider, then retry.");
			return false;
		}
		cx_s = out_cx;
		cy_s = out_cy;
		std::vector<DWORD> zoomed_bgra;
		if (!vxl && z_pct != 100)
		{
			zoomed_bgra.assign(static_cast<size_t>(zoom_cx) * zoom_cy, 0);
			theme::bilinear_resample_bgra(out.data(), cx_s, cy_s,
				zoomed_bgra.data(), zoom_cx, zoom_cy);
			out.swap(zoomed_bgra);
			cx_s = zoom_cx;
			cy_s = zoom_cy;
		}
		const byte* bgra = reinterpret_cast<const byte*>(out.data());
		if (fmt == fmt_tga)
		{
			ok = write_tga32(path, bgra, cx_s, cy_s);
		}
		else // fmt_png
		{
			Gdiplus::Bitmap bmp(cx_s, cy_s, cx_s * 4, PixelFormat32bppARGB,
				const_cast<BYTE*>(bgra));
			CLSID clsid;
			if (get_png_encoder_clsid(clsid) == 0)
			{
				CString wpath_s(path);
				wchar_t wpath[MAX_PATH * 2] = {};
				::MultiByteToWideChar(CP_ACP, 0, wpath_s, -1, wpath, _countof(wpath));
				ok = bmp.Save(wpath, &clsid, NULL) == Gdiplus::Ok;
			}
		}
	}

	if (CMainFrame* mf = GetMainFrame())
	{
		CString msg;
		msg.Format(ok ? "Screenshot saved: %s" : "Screenshot failed: %s", path);
		mf->SetMessageText(msg);
	}
	return ok;
}

void CXCCFileView::OnPlayerScreenshot()
{
	// Split-button menu: Save As... (file dialog) or Copy to Clipboard.
	// Anchored below the Screenshot button so it looks like a dropdown.
	enum { k_save_cmd = 1, k_copy_cmd };
	int chosen = 0;
	{
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, k_save_cmd, "Save As...");
		menu.AppendMenu(MF_STRING, k_copy_cmd, "Copy to Clipboard");
		CRect br;
		m_player_screenshot.GetWindowRect(&br);
		chosen = menu.TrackPopupMenu(
			TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
			br.left, br.bottom, this);
	}
	if (chosen == k_save_cmd)
		take_screenshot();
	else if (chosen == k_copy_cmd)
		copy_screenshot_to_clipboard();
}

bool CXCCFileView::copy_screenshot_to_clipboard()
{
	// Two payload modes, controlled by Theme > Image > Clipboard Format:
	//   Indexed = 8bpp paletted CF_DIB (m_color_table as palette, raw indexed
	//     pixels). Mirrors the PCX export path; round-trips to Paste-as-SHP.
	//   RGB     = 32bpp BGRA CF_DIB from capture_current_frame(wysiwyg=true).
	//     Whatever the player shows (BG=Color / Alpha / Pane bake-in) is what
	//     lands on the clipboard, same as Record's wysiwyg path.
	if (!m_player_mode || !m_is_open)
	{
		if (CMainFrame* mf = GetMainFrame())
			mf->SetMessageText("Press P to enter player mode before copying a screenshot.");
		return false;
	}
	UpdateWindow();

	const theme::clipboard_format fmt = theme::clipboard_fmt();
	const bool vxl = is_vxl_view();

	int cx_s = 0, cy_s = 0;
	HGLOBAL h_mem = NULL;

	if (fmt == theme::clipboard_indexed)
	{
		cx_s = vxl ? m_vxl_splat.cx_s : m_player_cx;
		cy_s = vxl ? m_vxl_splat.cy_s : m_player_cy;
		if (cx_s <= 0 || cy_s <= 0)
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Copy to clipboard: no rendered frame available.");
			return false;
		}
		const byte* indexed = nullptr;
		if (vxl)
		{
			if (m_vxl_splat.buf.size() != 0) indexed = m_vxl_splat.buf.data();
		}
		else if (m_player_frame >= 0 && m_player_frame < static_cast<int>(m_player_frames.size())
			&& static_cast<int>(m_player_frames[m_player_frame].size()) >= cx_s * cy_s)
		{
			indexed = m_player_frames[m_player_frame].data();
		}
		if (!indexed)
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Copy to clipboard: indexed pixel buffer unavailable.");
			return false;
		}

		// SHP/WSA: nearest-resample the indexed buffer to the player's
		// current effective zoom so the clipboard matches what the user
		// sees. VXL skipped (see take_screenshot). Indexed always nearest.
		std::vector<byte> zoomed_indexed;
		if (!vxl)
		{
			const int z_pct = player_effective_zoom_pct();
			if (z_pct != 100)
			{
				const int zoom_cx = std::max(1, cx_s * z_pct / 100);
				const int zoom_cy = std::max(1, cy_s * z_pct / 100);
				zoomed_indexed.assign(static_cast<size_t>(zoom_cx) * zoom_cy, 0);
				for (int y = 0; y < zoom_cy; y++)
				{
					const int sy = std::min(cy_s - 1, (y * 2 + 1) * cy_s / (zoom_cy * 2));
					const byte* srow = indexed + static_cast<size_t>(sy) * cx_s;
					byte* drow = zoomed_indexed.data() + static_cast<size_t>(y) * zoom_cx;
					for (int x = 0; x < zoom_cx; x++)
					{
						const int sx = std::min(cx_s - 1, (x * 2 + 1) * cx_s / (zoom_cx * 2));
						drow[x] = srow[sx];
					}
				}
				indexed = zoomed_indexed.data();
				cx_s = zoom_cx;
				cy_s = zoom_cy;
			}
		}

		// CF_DIB layout: BITMAPINFOHEADER, 256 * RGBQUAD palette, rows
		// bottom-up, each row padded to a DWORD boundary.
		const int row_stride = (cx_s + 3) & ~3;
		const size_t pixel_bytes = static_cast<size_t>(row_stride) * cy_s;
		const size_t total = sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD) + pixel_bytes;
		h_mem = GlobalAlloc(GMEM_MOVEABLE, total);
		if (!h_mem)
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Copy to clipboard: allocation failed.");
			return false;
		}
		byte* mem = reinterpret_cast<byte*>(GlobalLock(h_mem));
		if (!mem) { GlobalFree(h_mem); return false; }
		BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(mem);
		ZeroMemory(header, sizeof(BITMAPINFOHEADER));
		header->biSize = sizeof(BITMAPINFOHEADER);
		header->biWidth = cx_s;
		header->biHeight = cy_s;
		header->biPlanes = 1;
		header->biBitCount = 8;
		header->biCompression = BI_RGB;
		header->biClrUsed = 256;
		RGBQUAD* pal = reinterpret_cast<RGBQUAD*>(mem + sizeof(BITMAPINFOHEADER));
		for (int i = 0; i < 256; i++)
		{
			DWORD bgra = m_color_table[i];
			pal[i].rgbBlue     = static_cast<BYTE>(bgra & 0xff);
			pal[i].rgbGreen    = static_cast<BYTE>((bgra >> 8) & 0xff);
			pal[i].rgbRed      = static_cast<BYTE>((bgra >> 16) & 0xff);
			pal[i].rgbReserved = 0;
		}
		byte* pixels = mem + sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD);
		for (int y = 0; y < cy_s; y++)
		{
			const byte* src = indexed + static_cast<size_t>(cy_s - 1 - y) * cx_s;
			byte* dst = pixels + static_cast<size_t>(y) * row_stride;
			memcpy(dst, src, cx_s);
			if (row_stride > cx_s)
				memset(dst + cx_s, 0, row_stride - cx_s);
		}
		GlobalUnlock(h_mem);
	}
	else // clipboard_rgb
	{
		std::vector<DWORD> bgra;
		if (!capture_current_frame(bgra, cx_s, cy_s, /*wysiwyg=*/true) || cx_s <= 0 || cy_s <= 0)
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Copy to clipboard: rendered buffer unavailable.");
			return false;
		}
		// SHP/WSA: bilinear-resample BGRA to the player's effective zoom so
		// the clipboard matches what the user sees. VXL skipped.
		if (!vxl)
		{
			const int z_pct = player_effective_zoom_pct();
			if (z_pct != 100)
			{
				const int zoom_cx = std::max(1, cx_s * z_pct / 100);
				const int zoom_cy = std::max(1, cy_s * z_pct / 100);
				std::vector<DWORD> zoomed(static_cast<size_t>(zoom_cx) * zoom_cy, 0);
				theme::bilinear_resample_bgra(bgra.data(), cx_s, cy_s,
					zoomed.data(), zoom_cx, zoom_cy);
				bgra.swap(zoomed);
				cx_s = zoom_cx;
				cy_s = zoom_cy;
			}
		}
		// 32bpp CF_DIB: BITMAPINFOHEADER + 4-byte-aligned rows of BGRA,
		// bottom-up. Alpha byte is set to 0xFF (BI_RGB ignores it but many
		// consumers carry it through, and zero alpha would render invisible).
		const size_t pixel_bytes = static_cast<size_t>(cx_s) * cy_s * 4;
		const size_t total = sizeof(BITMAPINFOHEADER) + pixel_bytes;
		h_mem = GlobalAlloc(GMEM_MOVEABLE, total);
		if (!h_mem)
		{
			if (CMainFrame* mf = GetMainFrame())
				mf->SetMessageText("Copy to clipboard: allocation failed.");
			return false;
		}
		byte* mem = reinterpret_cast<byte*>(GlobalLock(h_mem));
		if (!mem) { GlobalFree(h_mem); return false; }
		BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(mem);
		ZeroMemory(header, sizeof(BITMAPINFOHEADER));
		header->biSize = sizeof(BITMAPINFOHEADER);
		header->biWidth = cx_s;
		header->biHeight = cy_s;
		header->biPlanes = 1;
		header->biBitCount = 32;
		header->biCompression = BI_RGB;
		DWORD* pixels = reinterpret_cast<DWORD*>(mem + sizeof(BITMAPINFOHEADER));
		for (int y = 0; y < cy_s; y++)
		{
			const DWORD* src = bgra.data() + static_cast<size_t>(cy_s - 1 - y) * cx_s;
			DWORD* dst = pixels + static_cast<size_t>(y) * cx_s;
			for (int x = 0; x < cx_s; x++)
				dst[x] = src[x] | 0xFF000000;
		}
		GlobalUnlock(h_mem);
	}

	bool ok = false;
	if (OpenClipboard())
	{
		EmptyClipboard();
		if (SetClipboardData(CF_DIB, h_mem))
		{
			h_mem = NULL;	// clipboard owns it now
			ok = true;
		}
		CloseClipboard();
	}
	if (h_mem)
		GlobalFree(h_mem);

	if (CMainFrame* mf = GetMainFrame())
	{
		const char* fmt_name = (fmt == theme::clipboard_indexed) ? "8-bit indexed" : "32-bit RGB";
		CString msg;
		msg.Format(ok ? "Screenshot copied to clipboard (%s)." : "Copy to clipboard failed (%s).", fmt_name);
		mf->SetMessageText(msg);
	}
	return ok;
}

namespace {

// Self-contained paletted GIF frame writer. Lifted from gif.h's
// GifWriteLzwImage but parameterized: caller supplies the palette + the
// indexed pixels directly, plus the disposal byte (so we can request
// disposal=2 "restore to background" when emitting transparent frames
// without having to patch the file post-hoc).
//
// Why bypass gif.h's GifWriteFrame for the SHP-with-transparency path:
// gif.h expects RGBA8 input and runs its own per-frame quantizer. Forcing
// transparency on top of that with the oldImage trick poisons gif.h's
// inter-frame delta encoder (any sprite pixel that happens to match the
// previous frame at the same position gets emitted as transparent), so
// frames 2+ end up shredded. Writing paletted frames directly with the
// SHP's actual palette sidesteps all of that: every input pixel is
// already a palette index, palette index 0 is the GIF transparent index,
// no quantization, no delta-encoding surprises.
//
// Format reference: GIF89a per-frame block sequence is GCE -> Image
// Descriptor -> local color table -> LZW data. Disposal byte fields are
// at GCE byte 4 (bits 4-2 = disposal method, bit 0 = transparency flag).
void gif_write_paletted_frame(FILE* f, const unsigned char* indexed,
	int w, int h, int delay_cs,
	const DWORD* color_table_256, int trans_idx, int disposal)
{
	// --- Graphics Control Extension ---
	fputc(0x21, f);
	fputc(0xf9, f);
	fputc(0x04, f);
	const unsigned char gce_flags = static_cast<unsigned char>(
		((disposal & 0x07) << 2) | 0x01);  // bit 0: transparency flag
	fputc(gce_flags, f);
	fputc(delay_cs & 0xff, f);
	fputc((delay_cs >> 8) & 0xff, f);
	fputc(trans_idx & 0xff, f);
	fputc(0, f); // block terminator

	// --- Image Descriptor ---
	fputc(0x2c, f);
	fputc(0, f); fputc(0, f);   // left
	fputc(0, f); fputc(0, f);   // top
	fputc(w & 0xff, f); fputc((w >> 8) & 0xff, f);
	fputc(h & 0xff, f); fputc((h >> 8) & 0xff, f);
	// 0x87 = local color table present, sorted=no, 256 entries (size=7 -> 2^(7+1))
	fputc(0x87, f);

	// --- Local Color Table (256 entries) ---
	// color_table_256 is BGRA in DWORD (B|G<<8|R<<16). Emit as RGB triples.
	// Entry 0 forced to (0,0,0): it's the transparent index, the actual
	// color doesn't matter because no opaque pixel ever references it,
	// but writing zeros makes the global bg-color reference resolve to
	// black (which is also the canvas-clear color when disposal=2).
	fputc(0, f); fputc(0, f); fputc(0, f);
	for (int i = 1; i < 256; i++)
	{
		const DWORD bgra = color_table_256[i];
		const unsigned char b = static_cast<unsigned char>(bgra & 0xff);
		const unsigned char g = static_cast<unsigned char>((bgra >> 8) & 0xff);
		const unsigned char r = static_cast<unsigned char>((bgra >> 16) & 0xff);
		fputc(r, f); fputc(g, f); fputc(b, f);
	}

	// --- LZW data ---
	// 8-bit min code size matches the 256-entry palette. Code dictionary
	// starts at 258 (256 + clear + EOI) and grows up to 4095, then resets.
	const int min_code_size = 8;
	const uint32_t clear_code = 1u << min_code_size; // 256
	const uint32_t eoi_code   = clear_code + 1;      // 257
	fputc(min_code_size, f);

	struct lzw_node { uint16_t next_[256]; };
	std::vector<lzw_node> tree(4096);
	memset(tree.data(), 0, sizeof(lzw_node) * 4096);

	// Bit-stream writer with 255-byte sub-block chunking (GIF spec).
	struct bit_writer {
		FILE* f;
		uint8_t chunk[256];
		int chunk_idx;
		uint8_t byte_;
		int bit_idx;
		void init(FILE* fp) { f = fp; chunk_idx = 0; byte_ = 0; bit_idx = 0; }
		void write_chunk()
		{
			fputc(chunk_idx, f);
			fwrite(chunk, 1, chunk_idx, f);
			chunk_idx = 0;
		}
		void write_bit(uint32_t bit)
		{
			byte_ |= (bit & 1) << bit_idx;
			bit_idx++;
			if (bit_idx > 7)
			{
				chunk[chunk_idx++] = byte_;
				byte_ = 0;
				bit_idx = 0;
				if (chunk_idx == 255) write_chunk();
			}
		}
		void write_code(uint32_t code, int length)
		{
			for (int b = 0; b < length; b++)
			{
				write_bit(code);
				code >>= 1;
			}
		}
		void flush()
		{
			while (bit_idx) write_bit(0);
			if (chunk_idx) write_chunk();
		}
	} bw;
	bw.init(f);

	int32_t cur_code = -1;
	int code_size = min_code_size + 1;       // 9 bits initially
	uint32_t max_code = clear_code + 1;       // 257 (clear + EOI assigned)

	bw.write_code(clear_code, code_size);

	for (int yy = 0; yy < h; yy++)
	{
		for (int xx = 0; xx < w; xx++)
		{
			const uint8_t v = indexed[yy * w + xx];
			if (cur_code < 0)
			{
				cur_code = v;
			}
			else if (tree[cur_code].next_[v])
			{
				cur_code = tree[cur_code].next_[v];
			}
			else
			{
				bw.write_code(static_cast<uint32_t>(cur_code), code_size);
				tree[cur_code].next_[v] = static_cast<uint16_t>(++max_code);
				if (max_code >= (1u << code_size))
					code_size++;
				if (max_code == 4095)
				{
					bw.write_code(clear_code, code_size);
					memset(tree.data(), 0, sizeof(lzw_node) * 4096);
					code_size = min_code_size + 1;
					max_code = clear_code + 1;
				}
				cur_code = v;
			}
		}
	}

	bw.write_code(static_cast<uint32_t>(cur_code), code_size);
	bw.write_code(clear_code, code_size);
	bw.write_code(eoi_code, min_code_size + 1);
	bw.flush();

	fputc(0, f); // block terminator (end of image data)
}

} // anonymous namespace

// Animated capture: VXL turntable (rotation / HVA / combined) or SHP/WSA
// frame walk. Drives the relevant frame state through N frames, captures
// each via capture_current_frame(), and emits either an animated GIF
// (gif.h) or a numbered PNG sequence. SS / lighting / VPL / shading
// (VXL) and side color / BG / shadows / palette (SHP) all flow through
// player_draw's existing rendering, so every theme setting the user has
// dialed in is honored without us touching the renderer.
void CXCCFileView::OnPlayerTurntable()
{
	const bool vxl = is_vxl_view();
	const bool shp_animated = !vxl && m_player_cf > 1;
	if (!m_player_mode || !m_is_open || (!vxl && !shp_animated))
	{
		if (CMainFrame* mf = GetMainFrame())
			mf->SetMessageText("Record needs a VXL or a multi-frame SHP/WSA in player mode.");
		return;
	}

	const bool hva_available = vxl && m_hva_loaded && m_player_cf > 1;
	const int  hva_cf = hva_available ? m_player_cf : 0;
	const int  ss = vxl ? std::max(1, static_cast<int>(theme::vxl_supersample())) : 1;

	// SHP/WSA effective frame count: when Shadows is on, the second half of
	// m_player_cf holds the per-frame shadow masks (frame N+cf/2 is the
	// shadow for frame N), so the player only walks the first cf/2 frames.
	// Recording past that index would capture pure shadow-mask frames as if
	// they were main frames, which renders as garbage. Mirror the player's
	// own range halving — same condition as player_set_frame at line 3453.
	int shp_capture_cf = 0;
	if (shp_animated)
	{
		shp_capture_cf = m_player_cf;
		if (m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0)
			shp_capture_cf = m_player_cf / 2;
	}

	CTurntableDlg dlg(hva_available, hva_cf, ss, this,
		shp_animated, shp_capture_cf);
	if (dlg.DoModal() != IDOK)
		return;

	const int  N         = dlg.m_frames;
	const bool dir_cw    = dlg.m_dir_cw;
	const int  fmt       = dlg.m_format;
	const int  anim_mode = dlg.m_anim;
	const int  delay_cs  = dlg.m_delay_cs;
	const int  ds_mode   = dlg.m_downscale;
	// SHP filter: Crisp = nearest + paletted GIF (default), Filtered =
	// bilinear + RGB-quantized GIF (loses transparent_pal0). Filtered
	// route also forces transparent_pal0 off downstream so the encoder
	// branches into the gif.h RGBA path rather than the paletted writer.
	const int  shp_filter = dlg.m_shp_filter;
	// SHP-only: emit palette index 0 as transparent. PNG gets real alpha=0;
	// GIF uses gif.h's inter-frame transparency mechanism (oldImage trick).
	// Filtered SHP capture can't go through the paletted GIF path: bilinear
	// resampling produces off-palette colors that have nowhere to land in
	// the 256-entry palette. Force transparent_pal0 off so the encoder
	// branches into gif.h's quantizer.
	const bool transparent_pal0 = dlg.m_transparent_pal0 && shp_animated
		&& (!shp_animated || shp_filter == CTurntableDlg::shp_filter_crisp);

	// Build default filename from asset basename, append turntable tag so the
	// user can tell screenshots and turntables apart at a glance.
	CString default_name = m_fname.empty() ? (shp_animated ? "recording" : "turntable") : m_fname.c_str();
	{
		int dot = default_name.ReverseFind('.');
		if (dot > 0) default_name = default_name.Left(dot);
		default_name += shp_animated ? "_rec" : "_tt";
		default_name += (fmt == CTurntableDlg::fmt_gif) ? ".gif" : "";
	}

	// Path collection — GIF: single .gif. PNG sequence: pick a base name; we
	// derive name_yNNN_hNNN.png per frame in the same directory.
	CString out_path;
	CString out_dir;
	CString out_base;
	if (fmt == CTurntableDlg::fmt_gif)
	{
		CFileDialog fdlg(FALSE, "gif", default_name,
			OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
			"Animated GIF (*.gif)|*.gif||", this);
		if (fdlg.DoModal() != IDOK)
			return;
		out_path = fdlg.GetPathName();
		if (out_path.ReverseFind('.') <= out_path.ReverseFind('\\'))
			out_path += ".gif";
	}
	else
	{
		// PNG sequence: pick a base path; per-frame names are derived from it.
		// CFileDialog with no real format makes for a reasonable folder picker
		// (the user types/picks a base name in their preferred folder).
		CFileDialog fdlg(FALSE, "png", default_name,
			OFN_HIDEREADONLY,
			"PNG sequence base name (*.png)|*.png||", this);
		if (fdlg.DoModal() != IDOK)
			return;
		out_path = fdlg.GetPathName();
		int slash = out_path.ReverseFind('\\');
		out_dir = (slash >= 0) ? out_path.Left(slash) : CString(".");
		out_base = (slash >= 0) ? out_path.Mid(slash + 1) : out_path;
		// Strip extension from base so we can append our own _yNNN_hNNN.png.
		int dot = out_base.ReverseFind('.');
		if (dot > 0) out_base = out_base.Left(dot);
	}

	// Save originals so we can restore exactly when capture finishes (or the
	// user cancels mid-loop). Resetting m_vxl_yaw to start_yaw isn't enough —
	// the user expects the model to look the same as before they clicked.
	const double start_yaw = m_vxl_yaw;
	const int    start_frame = m_player_frame;

	// CLSID for PNG (only needed for PNG sequence path).
	CLSID png_clsid = {};
	bool png_clsid_ok = false;
	if (fmt == CTurntableDlg::fmt_png_seq)
		png_clsid_ok = (get_png_encoder_clsid(png_clsid) == 0);

	// GIF writer state. Two paths:
	//   - Default (gif.h GifWriter): RGB->palette quantization done by
	//     gif.h. Used for VXL captures and SHP captures without
	//     transparency. Header takes the first frame's dimensions;
	//     deferred until we have the first capture.
	//   - Paletted (raw FILE* + gif_write_paletted_frame): used when
	//     transparent_pal0 is on for SHP. Bypasses gif.h's quantizer +
	//     delta encoder so per-frame transparency masks survive intact.
	GifWriter gif = {};
	bool gif_open = false;
	FILE* pal_gif_file = nullptr;
	bool pal_gif_open = false;

	int succeeded = 0;
	int failed = 0;
	const double two_pi = 6.283185307179586476925286766559;
	// Reused across frames so we don't reallocate on every step. Sized
	// lazily inside the loop based on the chosen ds_mode + actual capture
	// dimensions.
	std::vector<DWORD> ds_scratch;
	bool cancelled = false;
	// Drain any ESC keystroke that might have been pressed while the file
	// dialog was open, so the loop only sees fresh presses. GetAsyncKeyState
	// returns the key's state since the last call to itself for this thread,
	// so a throwaway read clears the sticky bit.
	GetAsyncKeyState(VK_ESCAPE);

	for (int i = 0; i < N; i++)
	{
		// Cancel check at the top of each frame: ESC pressed since the last
		// poll. Cheaper than running a CWaitCursor / PeekMessage pump and
		// works while the window has no focus (e.g. user clicked away mid-
		// capture). Stops the loop cleanly so the GifEnd / yaw-restore code
		// below still runs and the partial GIF is at least playable.
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			cancelled = true;
			break;
		}
		if (shp_animated)
		{
			// SHP/WSA: walk the native frames in order. player_set_frame
			// updates m_player_frame + slider + label; the existing BGRA
			// cache (m_player_bgra[i]) is already populated for SHP/WSA so
			// the next paint just blits the cached frame.
			player_set_frame(i);
		}
		else
		{
			// Update yaw / HVA frame for this step. anim_combined and anim_hva are
			// only reachable when hva_available is true (dialog enforces it), so
			// player_set_frame is safe to call for those modes.
			if (anim_mode != CTurntableDlg::anim_hva)
			{
				const double step = two_pi / N;
				const double signed_step = dir_cw ? step : -step;
				m_vxl_yaw = start_yaw + signed_step * i;
			}
			if (anim_mode != CTurntableDlg::anim_rotation && hva_available)
			{
				// Map turntable frame i -> HVA timeline frame. Round so endpoints
				// land cleanly: i=0 -> 0, i=N-1 -> ~hva_cf-1.
				int hf = static_cast<int>((static_cast<double>(i) * hva_cf) / N + 0.5);
				if (hf >= hva_cf) hf = hva_cf - 1;
				player_set_frame(hf);
			}

			// Force splat rebuild for the new yaw/frame. SS=-1 sentinel matches
			// the trick used elsewhere when external state changes invalidate
			// the splat key (lines 158, 213). SHP path doesn't have a splat.
			m_vxl_splat.ss = -1;
		}
		Invalidate(FALSE);
		UpdateWindow();

		std::vector<DWORD> bgra;
		int cx = 0, cy = 0;
		// wysiwyg=true: capture the on-screen composite as-is (BG color/
		// checker/pane already baked into the cached BGRA). Without this
		// flag, BG=Alpha and BG=Pane would write black-with-alpha=0 instead
		// of the visible BG, which surprises users who expect the recorded
		// GIF / PNG sequence to match their preview.
		if (!capture_current_frame(bgra, cx, cy, /*wysiwyg=*/true))
		{
			failed++;
			continue;
		}

		// Optional SS downscale before the writer sees the buffer. ds_full
		// (or SS=1) skips this entirely; ds_native targets the logical voxel
		// size; ds_half halves the supersampled dimensions. theme::bilinear_
		// resample_bgra handles the alpha channel in the same averaging pass
		// as RGB, so BG=Alpha screenshots get a clean anti-aliased mask.
		if (ds_mode != CTurntableDlg::ds_full && ss > 1)
		{
			int out_w = cx;
			int out_h = cy;
			if (ds_mode == CTurntableDlg::ds_native)
			{
				out_w = std::max(1, cx / ss);
				out_h = std::max(1, cy / ss);
			}
			else if (ds_mode == CTurntableDlg::ds_half)
			{
				out_w = std::max(1, cx / 2);
				out_h = std::max(1, cy / 2);
			}
			if (out_w != cx || out_h != cy)
			{
				ds_scratch.assign(static_cast<size_t>(out_w) * out_h, 0);
				theme::bilinear_resample_bgra(bgra.data(), cx, cy,
					ds_scratch.data(), out_w, out_h);
				bgra.swap(ds_scratch);
				cx = out_w;
				cy = out_h;
			}
		}

		// SHP/WSA + transparency: grab the raw indexed buffer for the current
		// frame. cx must equal m_player_cx here (downscale is forced off for
		// SHP), so the indexed buffer's size matches the BGRA's pixel count
		// one-to-one. Palette index 0 in the SHP becomes the transparent
		// index in the GIF / the alpha=0 pixel in the PNG.
		//
		// Shadow compositing (when m_player_shadows_on and cf is even, same
		// condition the renderer uses): SHPs store the shadow as frame
		// [main + cf/2]. Where the shadow frame has a non-zero pixel and
		// the main frame's pixel is palette 0, the renderer paints a
		// darkened-BG halo on screen. To preserve that halo in the
		// transparent GIF/PNG output we composite into a scratch indexed
		// buffer, writing palette index 255 at those positions. Slot 255
		// in the GIF palette is overridden to a fixed dark RGB
		// (kShadowR/G/B below) — on the rare SHP that uses idx 255 as a
		// real sprite color, those pixels would render as the shadow
		// color, but the C&C palette convention reserves the slot.
		const byte* shp_indexed = nullptr;
		std::vector<byte> shp_composite;
		const int kShadowIdx = 255;
		// "Background" palette index: 0 for RA2/TS (the default), 4 for the
		// TD/RA1 cycle state. Routes through to both the transparency mask
		// (which idx becomes alpha=0 / GIF transparent) and the shadow
		// composite (which idx counts as "no sprite here, paint shadow").
		const byte bg_idx = static_cast<byte>(m_player_bg_idx);
		if (transparent_pal0
			&& m_player_frame >= 0
			&& m_player_frame < static_cast<int>(m_player_frames.size())
			&& static_cast<int>(m_player_frames[m_player_frame].size()) >= cx * cy)
		{
			shp_indexed = m_player_frames[m_player_frame].data();
			const bool shadow_on = m_player_shadows_on && m_player_cf >= 2
				&& (m_player_cf % 2) == 0;
			const int shadow_idx_frame = m_player_frame + m_player_cf / 2;
			// RA1 mode (state 2): idx 4 (LTGREEN) in the body frame is the
			// raw shadow value. Remap to kShadowIdx (255) so the GIF picks
			// up the same dark-gray slot the RA2 pair-frame path uses and
			// the PNG semi-transparency loop catches it. Mutually exclusive
			// with shadow_on (state 1).
			if (m_player_shadows_ra1)
			{
				shp_composite.assign(shp_indexed, shp_indexed + cx * cy);
				const byte ra1_shadow_idx = 4;
				for (int p = 0; p < cx * cy; p++)
				{
					if (shp_composite[p] == ra1_shadow_idx)
						shp_composite[p] = static_cast<byte>(kShadowIdx);
				}
				shp_indexed = shp_composite.data();
			}
			else if (shadow_on
				&& shadow_idx_frame >= 0
				&& shadow_idx_frame < static_cast<int>(m_player_frames.size())
				&& static_cast<int>(m_player_frames[shadow_idx_frame].size()) >= cx * cy)
			{
				const byte* shadow = m_player_frames[shadow_idx_frame].data();
				shp_composite.assign(shp_indexed, shp_indexed + cx * cy);
				for (int p = 0; p < cx * cy; p++)
				{
					if (shp_composite[p] == bg_idx && shadow[p] != bg_idx)
						shp_composite[p] = static_cast<byte>(kShadowIdx);
				}
				shp_indexed = shp_composite.data();
			}
		}

		// Zoom-aware capture: scale both BGRA and (if present) indexed buffers
		// to the player's current effective zoom so the recorded output matches
		// what the user sees. SHP/WSA only — VXL already supports ds_mode.
		// Filter chosen explicitly via the Record dialog (decoupled from
		// theme::interp() which only affects on-screen preview): Crisp uses
		// nearest on BGRA + paletted GIF; Filtered uses bilinear on BGRA +
		// RGB-quantized GIF. The indexed buffer (paletted-GIF path) is
		// nearest regardless because palette indices can't be blended.
		std::vector<DWORD> shp_zoom_bgra;
		std::vector<byte>  shp_zoom_indexed;
		if (shp_animated)
		{
			const int z_pct = player_effective_zoom_pct();
			if (z_pct != 100 && cx > 0 && cy > 0)
			{
				const int out_w = std::max(1, cx * z_pct / 100);
				const int out_h = std::max(1, cy * z_pct / 100);
				// BGRA resample.
				shp_zoom_bgra.assign(static_cast<size_t>(out_w) * out_h, 0);
				const bool nearest_bgra =
					(shp_filter == CTurntableDlg::shp_filter_crisp);
				if (nearest_bgra)
				{
					// Sample-center nearest: pick src pixel under each dst
					// center for stable integer-ratio pixel doubling.
					for (int y = 0; y < out_h; y++)
					{
						const int sy = std::min(cy - 1, (y * 2 + 1) * cy / (out_h * 2));
						const DWORD* srow = bgra.data() + static_cast<size_t>(sy) * cx;
						DWORD* drow = shp_zoom_bgra.data() + static_cast<size_t>(y) * out_w;
						for (int x = 0; x < out_w; x++)
						{
							const int sx = std::min(cx - 1, (x * 2 + 1) * cx / (out_w * 2));
							drow[x] = srow[sx];
						}
					}
				}
				else
				{
					theme::bilinear_resample_bgra(bgra.data(), cx, cy,
						shp_zoom_bgra.data(), out_w, out_h);
				}
				bgra.swap(shp_zoom_bgra);
				// Indexed resample (nearest only — palette indices can't be
				// blended).
				if (shp_indexed)
				{
					shp_zoom_indexed.assign(static_cast<size_t>(out_w) * out_h, 0);
					for (int y = 0; y < out_h; y++)
					{
						const int sy = std::min(cy - 1, (y * 2 + 1) * cy / (out_h * 2));
						const byte* srow = shp_indexed + static_cast<size_t>(sy) * cx;
						byte* drow = shp_zoom_indexed.data() + static_cast<size_t>(y) * out_w;
						for (int x = 0; x < out_w; x++)
						{
							const int sx = std::min(cx - 1, (x * 2 + 1) * cx / (out_w * 2));
							drow[x] = srow[sx];
						}
					}
					shp_indexed = shp_zoom_indexed.data();
				}
				cx = out_w;
				cy = out_h;
			}
		}

		if (fmt == CTurntableDlg::fmt_gif && shp_indexed)
		{
			// Paletted-GIF path. Open the file once on first frame, write
			// header (GifBegin's screen descriptor + NETSCAPE2.0 loop block),
			// then per frame call our own gif_write_paletted_frame with the
			// SHP's indexed buffer + m_color_table as the palette, with
			// disposal=2 so transparent areas reset cleanly between frames.
			if (!pal_gif_open)
			{
				fopen_s(&pal_gif_file, std::string(out_path).c_str(), "wb");
				if (!pal_gif_file)
				{
					if (CMainFrame* mf = GetMainFrame())
						mf->SetMessageText("Turntable: GIF open failed.");
					break;
				}
				// File scaffolding: GIF89a + logical screen descriptor +
				// global color table (256 entries from m_color_table) +
				// optional NETSCAPE2.0 looping extension. Mirrors gif.h's
				// GifBegin but with our actual palette as the global table
				// (each frame still emits its own local table, but having a
				// real global table is harmless and helps decoders that
				// peek before the first image block).
				FILE* f = pal_gif_file;
				fputs("GIF89a", f);
				fputc(cx & 0xff, f); fputc((cx >> 8) & 0xff, f);
				fputc(cy & 0xff, f); fputc((cy >> 8) & 0xff, f);
				fputc(0xf7, f); // global color table present, 256 entries
				fputc(bg_idx, f); // bg color index = transparent index
				fputc(0, f);    // pixel aspect ratio
				// Full 256 entries from m_color_table. Slot 255 overwritten
				// with a fixed dark RGB so the per-frame shadow composite
				// can reference it as the "shadow" color regardless of what
				// m_color_table[255] holds.
				for (int e = 0; e < 256; e++)
				{
					if (e == kShadowIdx)
					{
						fputc(16, f); fputc(16, f); fputc(16, f);
					}
					else
					{
						const DWORD d = m_color_table[e];
						fputc(static_cast<int>((d >> 16) & 0xff), f); // R
						fputc(static_cast<int>((d >>  8) & 0xff), f); // G
						fputc(static_cast<int>( d        & 0xff), f); // B
					}
				}
				if (delay_cs != 0)
				{
					fputc(0x21, f); fputc(0xff, f); fputc(11, f);
					fputs("NETSCAPE2.0", f);
					fputc(3, f); fputc(1, f);
					fputc(0, f); fputc(0, f); // loop infinitely
					fputc(0, f);
				}
				pal_gif_open = true;
			}
			// Per-frame palette: copy m_color_table, override slot 255 with
			// the same dark RGB the global palette uses for shadow pixels.
			// Built per call (cheap — 256 DWORDs); could be hoisted out of
			// the loop if profiling shows it matters.
			DWORD pal_for_frame[256];
			std::memcpy(pal_for_frame, m_color_table, sizeof(pal_for_frame));
			pal_for_frame[kShadowIdx] = 0x00101010; // B=16 G=16 R=16
			gif_write_paletted_frame(pal_gif_file, shp_indexed,
				cx, cy, delay_cs, pal_for_frame, /*trans_idx=*/bg_idx,
				/*disposal=*/2);
			succeeded++;
		}
		else if (fmt == CTurntableDlg::fmt_gif)
		{
			// gif.h wants RGBA8 (alpha is ignored). Our buffer is BGRA — swap
			// B<->R into a scratch buffer per frame. Note: the GIF encoder
			// quantizes to 256 colors per frame, which is effectively lossless
			// for VXL output (source is already a 256-entry palette).
			std::vector<uint8_t> rgba(static_cast<size_t>(cx) * cy * 4);
			for (int p = 0; p < cx * cy; p++)
			{
				const DWORD d = bgra[p];
				rgba[p * 4 + 0] = static_cast<uint8_t>((d >> 16) & 0xff); // R
				rgba[p * 4 + 1] = static_cast<uint8_t>((d >>  8) & 0xff); // G
				rgba[p * 4 + 2] = static_cast<uint8_t>( d        & 0xff); // B
				rgba[p * 4 + 3] = static_cast<uint8_t>((d >> 24) & 0xff); // A (gif ignores)
			}
			if (!gif_open)
			{
				if (!GifBegin(&gif, std::string(out_path).c_str(),
					static_cast<uint32_t>(cx), static_cast<uint32_t>(cy),
					static_cast<uint32_t>(delay_cs)))
				{
					if (CMainFrame* mf = GetMainFrame())
						mf->SetMessageText("Turntable: GIF open failed.");
					break;
				}
				gif_open = true;
			}
			if (GifWriteFrame(&gif, rgba.data(),
				static_cast<uint32_t>(cx), static_cast<uint32_t>(cy),
				static_cast<uint32_t>(delay_cs)))
				succeeded++;
			else
				failed++;
		}
		else // PNG sequence
		{
			if (!png_clsid_ok)
			{
				failed++;
				continue;
			}
			CString frame_path;
			if (shp_animated)
			{
				// SHP/WSA: filename reflects native frame index, not yaw/HVA.
				frame_path.Format("%s\\%s_f%03d.png", (LPCSTR)out_dir, (LPCSTR)out_base, i);
			}
			else
			{
				const int hf = (anim_mode == CTurntableDlg::anim_rotation || !hva_available)
					? -1
					: static_cast<int>((static_cast<double>(i) * hva_cf) / N + 0.5);
				if (hf >= 0)
					frame_path.Format("%s\\%s_y%03d_h%03d.png", (LPCSTR)out_dir, (LPCSTR)out_base, i, hf);
				else
					frame_path.Format("%s\\%s_y%03d.png", (LPCSTR)out_dir, (LPCSTR)out_base, i);
			}
			// Palette-0 transparency for PNG: clear alpha + zero RGB on pixels
			// where the source palette index is 0. Mutates bgra in place; the
			// per-frame buffer is throwaway, no aliasing risk. Pure
			// sprite-only output — RA2 pair-frame shadows (which the renderer
			// paints as darkened BG over palette-0 positions) drop to
			// transparent along with the rest of the BG, by design.
			// RA1 mode: idx 4 was remapped to kShadowIdx (255) by the
			// composite step above; emit it as a semi-transparent dark pixel
			// (RGB 40,40,40 at alpha 128) so the shadow halo survives into
			// the PNG over any background.
			if (shp_indexed)
			{
				for (int p = 0; p < cx * cy; p++)
				{
					const byte ix = shp_indexed[p];
					if (ix == bg_idx)
						bgra[p] = 0;
					else if (m_player_shadows_ra1 && ix == kShadowIdx)
						bgra[p] = 0xC0000000; // ARGB: A=192 over pure black
				}
			}
			Gdiplus::Bitmap bmp(cx, cy, cx * 4, PixelFormat32bppARGB,
				const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bgra.data())));
			wchar_t wpath[MAX_PATH * 2] = {};
			::MultiByteToWideChar(CP_ACP, 0, frame_path, -1, wpath, _countof(wpath));
			if (bmp.Save(wpath, &png_clsid, NULL) == Gdiplus::Ok)
				succeeded++;
			else
				failed++;
		}

		// Status update so the user sees progress on a slow capture (high SS
		// or many frames). Updated every frame; the existing message-bar
		// throttle keeps repaint cost bounded.
		if (CMainFrame* mf = GetMainFrame())
		{
			CString msg;
			msg.Format("Turntable: %d / %d  (press ESC to cancel)", i + 1, N);
			mf->SetMessageText(msg);
		}
	}

	if (gif_open)
		GifEnd(&gif);
	if (pal_gif_open && pal_gif_file)
	{
		// GIF89a trailer + close. Mirrors GifEnd's tail.
		fputc(0x3b, pal_gif_file);
		fclose(pal_gif_file);
		pal_gif_file = nullptr;
		pal_gif_open = false;
	}

	// Restore pre-capture state. Bump splat ss=-1 so the post-capture paint
	// rebuilds at the original yaw/frame (otherwise the model is frozen at
	// the last captured pose until the user touches a slider). For SHP we
	// just restore the frame; yaw / splat invalidation are no-ops for it.
	if (!shp_animated)
		m_vxl_yaw = start_yaw;
	if (shp_animated || (anim_mode != CTurntableDlg::anim_rotation && hva_available))
		player_set_frame(start_frame);
	m_vxl_splat.ss = -1;
	Invalidate(FALSE);

	if (CMainFrame* mf = GetMainFrame())
	{
		CString msg;
		if (cancelled)
			msg.Format("Turntable: cancelled after %d / %d frame(s) (ESC). Partial output saved to %s",
				succeeded,
				N,
				(fmt == CTurntableDlg::fmt_gif) ? (LPCSTR)out_path : (LPCSTR)out_dir);
		else if (failed == 0)
			msg.Format("Turntable: saved %d frame(s) to %s", succeeded,
				(fmt == CTurntableDlg::fmt_gif) ? (LPCSTR)out_path : (LPCSTR)out_dir);
		else
			msg.Format("Turntable: %d ok, %d failed", succeeded, failed);
		mf->SetMessageText(msg);
	}
}

HBRUSH CXCCFileView::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (theme::is_dark())
	{
		pDC->SetTextColor(theme::text());
		pDC->SetBkColor(theme::bg());
		pDC->SetBkMode(TRANSPARENT);
		return theme::bg_brush();
	}
	return CScrollView::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CXCCFileView::reapply_player_theme()
{
	// (The Load PAL button is hosted on the frame's status bar and re-themed
	// in CMainFrame's theme-switch walk, not here.)
	if (!m_player_controls_created)
		return;
	HWND ctrls[] = {
		m_player_play.GetSafeHwnd(),
		m_player_reverse.GetSafeHwnd(),
		m_player_grid.GetSafeHwnd(),
		m_player_native.GetSafeHwnd(),
		m_player_slider.GetSafeHwnd(),
		m_player_label.GetSafeHwnd(),
		m_player_fps_label.GetSafeHwnd(),
		m_player_fps_edit.GetSafeHwnd(),
		m_player_fps_spin.GetSafeHwnd(),
		m_player_shadows.GetSafeHwnd(),
		m_player_bg.GetSafeHwnd(),
		m_player_iso_grid.GetSafeHwnd(),
		m_player_side_custom.GetSafeHwnd(),
		m_vxl_hva_load.GetSafeHwnd(),
		m_vxl_hva_loop.GetSafeHwnd(),
		m_vxl_vpl_load.GetSafeHwnd(),
	};
	for (HWND h : ctrls)
		if (h)
		{
			theme::apply_window(h);
			::InvalidateRect(h, NULL, TRUE);
		}
	for (int i = 0; i < 8; i++)
		if (HWND h = m_player_side[i].GetSafeHwnd())
		{
			theme::apply_window(h);
			::InvalidateRect(h, NULL, TRUE);
		}
	for (int i = 0; i < 8; i++)
		if (HWND h = m_vxl_side[i].GetSafeHwnd())
		{
			theme::apply_window(h);
			::InvalidateRect(h, NULL, TRUE);
		}
	if (HWND h = m_vxl_side_custom.GetSafeHwnd())
	{
		theme::apply_window(h);
		::InvalidateRect(h, NULL, TRUE);
	}
	// (Game Grid is now a plain pushbutton, themed by the apply_window walk
	// above — no combobox dropdown listbox to pull out and theme separately.)
	// Theme switch changes theme::bg(), which feeds BG=Pane mode. Invalidate
	// the SHP/WSA BGRA cache so the next paint rebuilds against the new pane
	// color; the VXL cache key includes pane_c so it self-invalidates.
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
}

void CXCCFileView::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT mis)
{
	CScrollView::OnMeasureItem(nIDCtl, mis);
}

void CXCCFileView::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis)
{
	const bool is_preset = dis && nIDCtl >= IDC_PLAYER_SIDE0 && nIDCtl <= IDC_PLAYER_SIDE7;
	const bool is_custom = dis && nIDCtl == IDC_PLAYER_SIDE_CUSTOM;
	const bool is_vxl_preset = dis && nIDCtl >= IDC_VXL_SIDE0 && nIDCtl <= IDC_VXL_SIDE7;
	const bool is_vxl_custom = dis && nIDCtl == IDC_VXL_SIDE_CUSTOM;
	if (!is_preset && !is_custom && !is_vxl_preset && !is_vxl_custom)
	{
		CScrollView::OnDrawItem(nIDCtl, dis);
		return;
	}
	HDC hdc = dis->hDC;
	RECT r = dis->rcItem;
	int slot;          // active-state index this swatch corresponds to
	COLORREF fill;
	const bool vxl_set = (is_vxl_preset || is_vxl_custom);
	const int active_idx = vxl_set ? m_vxl_side_idx : m_player_side_idx;
	const COLORREF custom_color = vxl_set ? m_vxl_side_custom_color : m_player_side_custom_color;
	if (is_preset)        { slot = nIDCtl - IDC_PLAYER_SIDE0; fill = k_side_colors[slot]; }
	else if (is_vxl_preset) { slot = nIDCtl - IDC_VXL_SIDE0;  fill = k_side_colors[slot]; }
	else                  { slot = 8;                         fill = custom_color; }
	// Solid color fill.
	HBRUSH brush = ::CreateSolidBrush(fill);
	::FillRect(hdc, &r, brush);
	::DeleteObject(brush);
	// "+" hint on the unselected custom swatch so it's discoverable.
	if ((is_custom || is_vxl_custom) && active_idx != 8)
	{
		::SetBkMode(hdc, TRANSPARENT);
		::SetTextColor(hdc, RGB(255, 255, 255));
		::DrawTextA(hdc, "+", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	// Active outline if this swatch is currently selected.
	if (active_idx == slot)
	{
		HPEN pen = ::CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
		HGDIOBJ old = ::SelectObject(hdc, pen);
		HGDIOBJ old_b = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
		::Rectangle(hdc, r.left, r.top, r.right, r.bottom);
		::SelectObject(hdc, old);
		::SelectObject(hdc, old_b);
		::DeleteObject(pen);
	}
}

void CXCCFileView::OnPlayerShadows()
{
	if (!m_player_controls_created) return;
	// 3-state cycle: Off -> Shadows RA2/TS -> Shadows RA1 -> Off ...
	// Pushlike checkbox style fires BN_CLICKED; we ignore the auto-toggle
	// and drive state ourselves. SetCheck below reflects "shadow-on" (any
	// state >= 1) so the button stays "pressed" in states 1/2.
	m_player_shadows_state = (m_player_shadows_state + 1) % 3;
	// State 1 (RA2/TS) = pair-frame composite from frame[i+cf/2]; needs even cf.
	// State 2 (RA1) = inline 0xFF magic-marker shadow, faithful to the RA1
	// engine (SHAPE.INC SHADOW_COL, DS_DS.ASM ShadowingTable lookup). No cf
	// halving — RA1 SHPs are not pair-frame.
	const bool can_pair = m_player_cf >= 2 && (m_player_cf % 2) == 0;
	if (m_player_shadows_state == 1 && !can_pair)
		m_player_shadows_state = 2;
	m_player_shadows_on  = (m_player_shadows_state == 1);
	m_player_shadows_ra1 = (m_player_shadows_state == 2);
	m_player_bg_idx = 0;
	m_player_shadows.SetCheck((m_player_shadows_state >= 1) ? BST_CHECKED : BST_UNCHECKED);
	m_player_shadows.SetWindowText(
		m_player_shadows_state == 0 ? "Shadows" :
		m_player_shadows_state == 1 ? "Shadows RA2/TS" :
		                              "Shadows RA1");
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
	// Pair-mode reduces navigable range to cf/2; clamp current frame.
	if (m_player_shadows_on)
	{
		int max_f = std::max(0, m_player_cf / 2 - 1);
		if (m_player_frame > max_f) m_player_frame = 0;
		m_player_slider.SetRange(0, max_f);
	}
	else
	{
		m_player_slider.SetRange(0, std::max(0, m_player_cf - 1));
	}
	m_player_slider.SetPos(m_player_frame);
	player_update_label();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::OnPlayerBg()
{
	if (!m_player_controls_created) return;
	// Cycle: 0 (Color) → 1 (Alpha checker) → 2 (Pane bg).
	m_player_bg_mode = (m_player_bg_mode + 1) % 3;
	player_update_bg_label();
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::player_update_bg_label()
{
	if (!m_player_bg.GetSafeHwnd()) return;
	const char* label = "BG Color";
	if (m_player_bg_mode == 1) label = "BG Alpha";
	else if (m_player_bg_mode == 2) label = "BG Pane";
	m_player_bg.SetWindowText(label);
}

void CXCCFileView::player_update_grid_label()
{
	if (!m_player_iso_grid.GetSafeHwnd()) return;
	// Off state reads plain "Grid" (per the request) so the control is
	// self-describing; active states name the game ruleset.
	const char* label = "Grid";
	if (m_player_grid_mode == 1) label = "TS Grid";
	else if (m_player_grid_mode == 2) label = "RA2 Grid";
	m_player_iso_grid.SetWindowText(label);
}

void CXCCFileView::OnPlayerSide(UINT id)
{
	int i = static_cast<int>(id) - IDC_PLAYER_SIDE0;
	if (i < 0 || i > 7) return;
	// Toggle: clicking the active swatch clears.
	m_player_side_idx = (m_player_side_idx == i) ? -1 : i;
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
	for (int k = 0; k < 8; k++)
		m_player_side[k].Invalidate();
	m_player_side_custom.Invalidate();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::OnPlayerSideCustom()
{
	// Open color picker. If user accepts, activate slot 8 with the chosen color.
	// Clicking the active custom swatch (without changing color) clears remap.
	if (m_player_side_idx == 8)
	{
		m_player_side_idx = -1;
		m_player_bgra_version++;
		if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
		for (int k = 0; k < 8; k++)
			m_player_side[k].Invalidate();
		m_player_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	// Live preview: snapshot pre-picker state, activate slot 8 + bump the
	// BGRA cache version on every picker change so the player frame retints
	// in real time. Cancel restores the snapshot. See OnVxlSideCustom for the
	// matching VXL-side flow.
	const COLORREF saved_color = m_player_side_custom_color;
	const int      saved_idx   = m_player_side_idx;
	CColorPickerDlg dlg(m_player_side_custom_color, this);
	dlg.set_on_color_change([this](COLORREF c) {
		m_player_side_custom_color = c;
		m_player_side_idx = 8;
		m_player_bgra_version++;
		if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
		for (int k = 0; k < 8; k++)
			m_player_side[k].Invalidate();
		m_player_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		UpdateWindow();
	});
	if (dlg.DoModal() != IDOK)
	{
		m_player_side_custom_color = saved_color;
		m_player_side_idx = saved_idx;
		m_player_bgra_version++;
		if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
		for (int k = 0; k < 8; k++)
			m_player_side[k].Invalidate();
		m_player_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
}

void CXCCFileView::OnVxlSide(UINT id)
{
	int i = static_cast<int>(id) - IDC_VXL_SIDE0;
	if (i < 0 || i > 7) return;
	m_vxl_side_idx = (m_vxl_side_idx == i) ? -1 : i;
	for (int k = 0; k < 8; k++)
		m_vxl_side[k].Invalidate();
	m_vxl_side_custom.Invalidate();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::OnVxlSideCustom()
{
	if (m_vxl_side_idx == 8)
	{
		m_vxl_side_idx = -1;
		for (int k = 0; k < 8; k++)
			m_vxl_side[k].Invalidate();
		m_vxl_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	// Live preview: snapshot the pre-picker state, activate slot 8 immediately,
	// and wire a callback that pushes every picker change into the splat
	// (cache key includes vxl_side_custom_color so the next paint rebuilds the
	// tinted view). On Cancel we restore the snapshot so nothing is committed.
	const COLORREF saved_color = m_vxl_side_custom_color;
	const int      saved_idx   = m_vxl_side_idx;
	CColorPickerDlg dlg(m_vxl_side_custom_color, this);
	dlg.set_on_color_change([this](COLORREF c) {
		m_vxl_side_custom_color = c;
		m_vxl_side_idx = 8;
		for (int k = 0; k < 8; k++)
			m_vxl_side[k].Invalidate();
		m_vxl_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		UpdateWindow();	// drive the live repaint while the modal is up
	});
	if (dlg.DoModal() != IDOK)
	{
		// Revert: restore color + previous side index, repaint.
		m_vxl_side_custom_color = saved_color;
		m_vxl_side_idx = saved_idx;
		for (int k = 0; k < 8; k++)
			m_vxl_side[k].Invalidate();
		m_vxl_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	// OK path: the callback already pushed the final color + activated slot 8.
	// Nothing else to do — the registry save lives in close_f() / app exit,
	// just like before this change.
}

Cvirtual_binary CXCCFileView::find_in_sources(const string& name)
{
	Cvirtual_binary out;
	if (name.empty())
		return out;

	auto search_mix = [&](Cmix_file* mix) -> bool {
		if (!mix) return false;
		// Cmix_file::get_id is name-hashed, so a direct query would work in
		// principle — but the hashing is case- and game-specific, and we
		// already know the entry's name. Walk the index and compare strings
		// case-insensitively, mirroring OnVxlHvaLoad's HVA enumeration.
		for (size_t i = 0; i < mix->get_c_files(); i++)
		{
			const int id = mix->get_id(static_cast<int>(i));
			string entry = mix->get_name(id);
			if (entry.size() != name.size())
				continue;
			bool eq = true;
			for (size_t k = 0; k < entry.size(); k++)
			{
				char a = static_cast<char>(tolower(static_cast<unsigned char>(entry[k])));
				char b = static_cast<char>(tolower(static_cast<unsigned char>(name[k])));
				if (a != b) { eq = false; break; }
			}
			if (!eq) continue;
			Cvirtual_binary bytes = mix->get_vdata(id);
			if (bytes.size() > 0)
			{
				out = bytes;
				return true;
			}
		}
		return false;
	};

	// 1. Body's source MIX.
	if (search_mix(m_source_mix))
		return out;

	// 2. Opposite Mixer pane's MIX (if different from #1).
	if (CMainFrame* mf = GetMainFrame())
	{
		Cmix_file* left = mf->left_mix_pane() ? mf->left_mix_pane()->current_mix() : nullptr;
		Cmix_file* right = mf->right_mix_pane() ? mf->right_mix_pane()->current_mix() : nullptr;
		if (left && left != m_source_mix && search_mix(left))
			return out;
		if (right && right != m_source_mix && right != left && search_mix(right))
			return out;
	}

	// 3. Same folder on disk (only when body came from disk).
	if (!m_disk_dir.empty())
	{
		const string path = m_disk_dir + name;
		Cvirtual_binary bytes;
		if (!bytes.load(path) && bytes.size() > 0)
			out = bytes;
	}

	return out;
}

void CXCCFileView::vxl_load_parts()
{
	// Derive base = lowercase basename without extension. Used to compose
	// <base>tur.vxl / <base>barl.vxl and their .hva siblings.
	string base = Cfname(m_fname).get_ftitle();
	for (auto& c : base) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (base.empty())
		return;

	// Skip if the body itself is already a turret/barrel — we don't want to
	// recursively load apoctur.vxl's "tur" by appending another "tur". A simple
	// suffix check on the body's basename catches it.
	auto ends_with = [](const string& s, const string& suf) {
		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	};
	if (ends_with(base, "tur") || ends_with(base, "barl"))
		return;

	auto try_part = [&](const string& suffix) {
		const string vxl_name = base + suffix + ".vxl";
		const string hva_name = base + suffix + ".hva";
		Cvirtual_binary vxl_bytes = find_in_sources(vxl_name);
		if (vxl_bytes.size() == 0)
			return;
		// Validate before storing — a name match could resolve to garbage if
		// somehow another file uses the same name.
		Cvxl_file probe;
		probe.load(vxl_bytes);
		if (!probe.is_valid())
			return;
		t_vxl_part p;
		p.vxl_data = vxl_bytes;
		p.name = vxl_name;
		Cvirtual_binary hva_bytes = find_in_sources(hva_name);
		if (hva_bytes.size() > 0)
		{
			Chva_file hva_probe;
			hva_probe.load(hva_bytes);
			if (hva_probe.is_valid() && hva_probe.get_c_frames() > 0 && hva_probe.get_c_sections() > 0)
			{
				p.hva_data = hva_bytes;
				p.hva_loaded = true;
			}
		}
		m_vxl_parts.push_back(std::move(p));
	};

	try_part("tur");
	try_part("barl");
}

void CXCCFileView::try_auto_load_vpl()
{
	// Engine convention: a single "voxels.vpl" per game lives alongside the
	// voxel assets. Look for it in the same places vxl_load_parts looks for
	// sibling .vxl/.hva files (source MIX, opposite pane's MIX, disk folder).
	Cvirtual_binary bytes = find_in_sources("voxels.vpl");
	if (bytes.size() == 0)
		return;
	Cvpl_file probe;
	probe.load(bytes);
	if (!probe.is_valid())
		return;
	m_vpl_data = bytes;
	m_vpl_file.load(m_vpl_data);
	m_vpl_loaded = true;
	m_vpl_name = "voxels.vpl";
	// Splat cache is keyed on m_vpl_loaded; nothing to invalidate explicitly,
	// player_enter / next paint will rebuild.
}

void CXCCFileView::OnVxlVplLoad()
{
	if (m_ft != ft_vxl)
		return;

	// Same MIX-popup-then-Browse pattern as OnVxlHvaLoad. No similarity
	// filter — VPLs aren't named after the VXL; just list every .vpl in the
	// source MIX.
	const int k_browse_cmd = 1;
	const int k_clear_cmd  = 2;
	const int k_reload_cmd = 3;
	const int k_mix_base   = 100;
	struct mix_choice { int id; string label; };
	std::vector<mix_choice> mix_choices;
	if (m_source_mix)
	{
		for (size_t i = 0; i < m_source_mix->get_c_files(); i++)
		{
			const int id = m_source_mix->get_id(static_cast<int>(i));
			string name = m_source_mix->get_name(id);
			if (name.size() < 4)
				continue;
			string lc = name;
			for (auto& c : lc) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (lc.substr(lc.size() - 4) != ".vpl")
				continue;
			mix_choices.push_back({ id, name });
		}
	}

	// Active VPL name (case-insensitive) for matching against MIX entries
	// so we can show a checkmark next to the one currently in use.
	string active_lc = m_vpl_name;
	for (auto& c : active_lc) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

	int chosen = k_browse_cmd;
	{
		CMenu menu;
		menu.CreatePopupMenu();

		// Status header — shows the currently active VPL (or "none"). Disabled
		// so it can't be clicked but it makes the source visible.
		string header = m_vpl_loaded
			? ("Active: " + m_vpl_name)
			: string("Active: (none - synthetic shading)");
		menu.AppendMenu(MF_STRING | MF_GRAYED | MF_DISABLED, 0, header.c_str());
		menu.AppendMenu(MF_SEPARATOR);

		for (size_t i = 0; i < mix_choices.size(); i++)
		{
			string lc = mix_choices[i].label;
			for (auto& c : lc) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			UINT flags = MF_STRING;
			if (m_vpl_loaded && lc == active_lc)
				flags |= MF_CHECKED;
			menu.AppendMenu(flags, k_mix_base + i, mix_choices[i].label.c_str());
		}
		if (!mix_choices.empty())
			menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, k_browse_cmd, "Browse disk...");
		// Always offer the auto-detect path so a manual clear can be undone
		// even when the VPL lives outside m_source_mix (opposite pane / disk).
		menu.AppendMenu(MF_STRING, k_reload_cmd, "Auto-detect (voxels.vpl)");
		if (m_vpl_loaded)
		{
			menu.AppendMenu(MF_SEPARATOR);
			menu.AppendMenu(MF_STRING, k_clear_cmd, "Clear (use synthetic shading)");
		}
		CRect br;
		m_vxl_vpl_load.GetWindowRect(&br);
		chosen = menu.TrackPopupMenu(
			TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
			br.left, br.bottom, this);
		if (chosen == 0)
			return;
	}

	if (chosen == k_clear_cmd)
	{
		clear_vpl();
		return;
	}

	if (chosen == k_reload_cmd)
	{
		// Re-run the same auto-detection used on file open. Searches
		// m_source_mix → opposite pane's MIX → m_disk_dir for "voxels.vpl".
		if (!reload_vpl())
			AfxMessageBox("No voxels.vpl found in this MIX, the opposite pane, or the source folder.", MB_ICONINFORMATION);
		return;
	}

	Cvirtual_binary data;
	string source_label;
	if (chosen >= k_mix_base && chosen < k_mix_base + static_cast<int>(mix_choices.size()))
	{
		const mix_choice& c = mix_choices[chosen - k_mix_base];
		data = m_source_mix->get_vdata(c.id);
		source_label = c.label;
		if (data.size() == 0)
		{
			AfxMessageBox("Could not read the selected VPL entry from the MIX.", MB_ICONERROR);
			return;
		}
	}
	else
	{
		CFileDialog dlg(TRUE, "vpl", NULL,
			OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
			"VPL files (*.vpl)|*.vpl|All files (*.*)|*.*||", this);
		if (dlg.DoModal() != IDOK)
			return;
		const string path = static_cast<const char*>(dlg.GetPathName());
		if (data.load(path) || data.size() == 0)
		{
			AfxMessageBox("Could not read the selected VPL file.", MB_ICONERROR);
			return;
		}
		source_label = Cfname(path).get_fname();
	}

	Cvpl_file probe;
	probe.load(data);
	if (!probe.is_valid())
	{
		AfxMessageBox("File is not a valid VPL.", MB_ICONERROR);
		return;
	}
	m_vpl_data = data;
	m_vpl_file.load(m_vpl_data);
	m_vpl_loaded = true;
	m_vpl_name = source_label;
	m_vpl_baked_lighting_version = -1;	// force re-bake against the new VPL
	m_open_token++;	// invalidate splat cache
	Invalidate();
}

void CXCCFileView::clear_vpl()
{
	if (!m_vpl_loaded && m_vpl_data.size() == 0)
		return;	// already synthetic; nothing to drop or repaint
	m_vpl_loaded = false;
	m_vpl_data.clear();
	m_vpl_name.clear();
	m_vpl_baked.clear();
	m_vpl_baked_lighting_version = -1;
	m_open_token++;	// invalidate splat cache (key includes m_vpl_loaded)
	Invalidate();
}

// Re-bake the VPL section table from its internal palette + the current
// Ambient/Diffuse/Specular sliders, exactly as vxl-renderer's editor does
// (mainwindow.cpp:530 update_vpl). For each section s (0..31) and palette index
// i (1..255): multiply the display color by vpl_curve(s) and remap to the
// nearest palette entry (redmean distance). The splat then indexes m_vpl_baked
// with sec = (int)(16*f), so amb/dif/spec become live in VPL mode without
// touching the on-disk VPL. Baked against m_color_table (the 24-bit RGB we
// actually render through), so the nearest-color search is in display space.
void CXCCFileView::rebuild_baked_vpl()
{
	if (!m_vpl_loaded)
		return;
	const float amb = theme::vxl_light_ambient();
	const float dif = theme::vxl_light_diffuse();
	const float spec = theme::vxl_light_specular();
	// vxl-renderer's per-section brightness curve (mainwindow.cpp:543):
	//   f = s/16 ; f<1 ? amb + f*dif : amb + dif + (f-1)*(dif + spec)
	auto vpl_curve = [&](int s) -> double {
		double f = s / 16.0;
		return f < 1.0 ? (amb + f * dif) : (amb + dif + (f - 1.0) * (dif + spec));
	};
	// Redmean color distance (mainwindow.cpp:546 color_sqdistance).
	auto sqdist = [](int r1, int g1, int b1, int r2, int g2, int b2) -> long {
		int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
		int rmean = (r1 + r2) / 2;
		return (((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg + (((767 - rmean) * db * db) >> 8);
	};
	// Source colors = the 24-bit display palette (BGRA in m_color_table).
	const t_palette32bgr_entry* ct = reinterpret_cast<const t_palette32bgr_entry*>(m_color_table);
	m_vpl_baked.assign(static_cast<size_t>(32) * 256, 0);
	for (int s = 0; s < 32; s++)
	{
		const double eff = vpl_curve(s);
		byte* row = m_vpl_baked.data() + static_cast<size_t>(s) * 256;
		for (int i = 1; i < 256; i++)
		{
			int tr = static_cast<int>(std::clamp(ct[i].r * eff, 0.0, 255.0));
			int tg = static_cast<int>(std::clamp(ct[i].g * eff, 0.0, 255.0));
			int tb = static_cast<int>(std::clamp(ct[i].b * eff, 0.0, 255.0));
			// Preserve Westwood remap membership: a house-color input (16-31)
			// must bake to a house-color output, else the side-color retint in
			// the composite loop (which gates on the *baked* index 16-31) can't
			// see it and recoloring silently no-ops under a loaded VPL. Mirrors
			// vxl-renderer update_vpl()'s colorset.color_selection mask
			// (mainwindow.cpp:571-586), which restricts the nearest search to
			// 16-31 for the house-color set. Non-remap inputs search 1-255 as
			// before (the renderer's all-inputs path does the same).
			const bool remap = (i >= 16 && i <= 31);
			const int f_lo = remap ? 16 : 1;
			const int f_hi = remap ? 31 : 255;
			long best = LONG_MAX;
			int best_i = f_lo;
			for (int f = f_lo; f <= f_hi; f++)
			{
				long d = sqdist(ct[f].r, ct[f].g, ct[f].b, tr, tg, tb);
				if (d < best) { best = d; best_i = f; }
			}
			row[i] = static_cast<byte>(best_i);
		}
	}
	m_vpl_baked_lighting_version = theme::vxl_lighting_version();
	m_vpl_baked_pal_version = m_player_bgra_version;
}

bool CXCCFileView::reload_vpl()
{
	// Re-run the same auto-detection used on file open. Searches
	// m_source_mix -> opposite pane's MIX -> m_disk_dir for "voxels.vpl".
	m_vpl_loaded = false;
	m_vpl_data.clear();
	m_vpl_name.clear();
	try_auto_load_vpl();
	m_vpl_baked_lighting_version = -1;	// force re-bake against the (re)loaded VPL
	m_open_token++;	// invalidate splat cache regardless of outcome
	Invalidate();
	return m_vpl_loaded;
}

void CXCCFileView::OnVxlHvaLoad()
{
	if (m_ft != ft_vxl)
		return;

	// Build a popup menu listing every .hva entry in the source MIX (if any),
	// plus a "Browse disk..." fallback. Selecting a MIX entry loads it
	// directly from the MIX via get_vdata(id); selecting Browse falls
	// through to the file picker path below.
	const int k_browse_cmd = 1;
	const int k_mix_base   = 100;	// MIX entries get k_mix_base + index
	struct mix_choice { int id; string label; };
	std::vector<mix_choice> mix_choices;
	// Derive the VXL basename (sans extension) once for similarity matching.
	// Filter HVA candidates by longest-common-prefix score so users see only
	// the entries that are plausibly paired with this VXL — typical pairs
	// share the full base (e.g. siren2tur.vxl <-> siren2tur.hva).
	string vxl_base;
	{
		Cfname fn(m_fname);
		vxl_base = fn.get_ftitle();
		for (auto& c : vxl_base) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	auto similarity = [](const string& a, const string& b) -> double {
		if (a.empty() || b.empty()) return 0.0;
		size_t i = 0;
		const size_t m = (std::min)(a.size(), b.size());
		while (i < m && a[i] == b[i]) i++;
		return static_cast<double>(i) / static_cast<double>((std::max)(a.size(), b.size()));
	};
	if (m_source_mix)
	{
		for (size_t i = 0; i < m_source_mix->get_c_files(); i++)
		{
			const int id = m_source_mix->get_id(static_cast<int>(i));
			string name = m_source_mix->get_name(id);
			if (name.empty())
				continue;
			// Match by extension; mix_database names always carry the suffix
			// when known. Case-insensitive compare for safety.
			if (name.size() < 4)
				continue;
			string lc = name;
			for (auto& c : lc) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (lc.substr(lc.size() - 4) != ".hva")
				continue;
			// Similarity filter: prefix match against the VXL basename. ~80%
			// keeps near-twins (siren2tur vs siren2tur, harv vs harvtur) and
			// drops unrelated rigs sharing only a letter or two.
			string hva_base = lc.substr(0, lc.size() - 4);
			if (!vxl_base.empty() && similarity(vxl_base, hva_base) < 0.8)
				continue;
			mix_choices.push_back({ id, name });
		}
	}

	int chosen = k_browse_cmd;
	if (!mix_choices.empty())
	{
		CMenu menu;
		menu.CreatePopupMenu();
		for (size_t i = 0; i < mix_choices.size(); i++)
			menu.AppendMenu(MF_STRING, k_mix_base + i, mix_choices[i].label.c_str());
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, k_browse_cmd, "Browse disk...");
		CRect br;
		m_vxl_hva_load.GetWindowRect(&br);
		chosen = menu.TrackPopupMenu(
			TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
			br.left, br.bottom, this);
		if (chosen == 0)	// user dismissed
			return;
	}

	Cvirtual_binary data;
	string source_label;
	// Captured here (rather than in the Browse branch's inner scope) so the
	// part-HVA auto-pair block below can derive the folder from it.
	string picked_disk_path;
	if (chosen >= k_mix_base && chosen < k_mix_base + static_cast<int>(mix_choices.size()))
	{
		const mix_choice& c = mix_choices[chosen - k_mix_base];
		data = m_source_mix->get_vdata(c.id);
		source_label = c.label;
		if (data.size() == 0)
		{
			AfxMessageBox("Could not read the selected HVA entry from the MIX.", MB_ICONERROR);
			return;
		}
	}
	else
	{
		CFileDialog dlg(TRUE, "hva", NULL,
			OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
			"HVA files (*.hva)|*.hva|All files (*.*)|*.*||", this);
		if (dlg.DoModal() != IDOK)
			return;
		const string path = static_cast<const char*>(dlg.GetPathName());
		if (data.load(path) || data.size() == 0)
		{
			AfxMessageBox("Could not read the selected HVA file.", MB_ICONERROR);
			return;
		}
		picked_disk_path = path;
	}

	// Validate by parsing — Chva_file::is_valid() checks the header sizing
	// against the file size, so we don't trust extension alone.
	Chva_file probe;
	probe.load(data);
	if (!probe.is_valid() || probe.get_c_frames() <= 0 || probe.get_c_sections() <= 0)
	{
		AfxMessageBox("File is not a valid HVA.", MB_ICONERROR);
		return;
	}
	m_hva_data = data;
	m_hva_loaded = true;
	m_hva_vxl_half = 0;

	// Auto-pair part HVAs for any sub-VXLs currently loaded by Full Hierarchy.
	// vxl_load_parts() ran at file open and stored each tur/barl in
	// m_vxl_parts; if their .hva siblings weren't co-located with the body
	// at that time (or weren't probed), the parts stay frozen even after the
	// user manually picks a body HVA here. Re-probe now using the same
	// naming scheme vxl_load_parts uses (<base><suffix>.hva), looking in
	// (1) the source MIX when a MIX entry was chosen, (2) the picked HVA's
	// disk folder when Browse was used. Existing per-part HVAs are not
	// overwritten — the user may have manually loaded them too.
	if (!m_vxl_parts.empty())
	{
		string body_base = Cfname(m_fname).get_ftitle();
		for (auto& c : body_base) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

		// Probe order: source MIX first (matches OnVxlHvaLoad's own
		// preference), then the disk folder of the picked HVA when Browse
		// was used. The MIX scan helps even on Browse if the part HVAs
		// happen to live in the same MIX as the part VXLs.
		string disk_dir;
		if (!picked_disk_path.empty())
		{
			int slash = static_cast<int>(picked_disk_path.find_last_of("\\/"));
			if (slash > 0)
				disk_dir = picked_disk_path.substr(0, slash);
		}

		auto try_pair_part = [&](t_vxl_part& p) {
			if (p.hva_loaded && p.hva_data.size() > 0)
				return;	// keep existing pairing
			// Derive the part suffix from the part's name vs body_base.
			// p.name is "<body_base><suffix>.vxl" lowercased by vxl_load_parts.
			string pname = p.name;
			for (auto& c : pname) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			string pstem = pname;
			if (pstem.size() >= 4 && pstem.substr(pstem.size() - 4) == ".vxl")
				pstem.resize(pstem.size() - 4);
			if (pstem.size() <= body_base.size() ||
				pstem.compare(0, body_base.size(), body_base) != 0)
				return;
			const string hva_name = pstem + ".hva";

			Cvirtual_binary hva_bytes;
			// (1) Source MIX (covers both MIX-pick and Browse cases when the
			// part HVAs happen to live in the same MIX as the part VXLs).
			hva_bytes = find_in_sources(hva_name);
			// (2) Disk folder of the picked HVA (Browse case).
			if (hva_bytes.size() == 0 && !disk_dir.empty())
			{
				string disk_path = disk_dir + "\\" + hva_name;
				Cvirtual_binary t;
				if (!t.load(disk_path) && t.size() > 0)
					hva_bytes = t;
			}
			if (hva_bytes.size() == 0)
				return;

			Chva_file hva_probe;
			hva_probe.load(hva_bytes);
			if (!hva_probe.is_valid() || hva_probe.get_c_frames() <= 0
				|| hva_probe.get_c_sections() <= 0)
				return;
			p.hva_data = hva_bytes;
			p.hva_loaded = true;
		};

		for (auto& p : m_vxl_parts)
			try_pair_part(p);
	}

	// Re-enter the player so the transport row / slider rebind to the new
	// frame count. player_enter calls player_decode_frames which now sees
	// m_hva_loaded and produces m_player_cf = HVA frame count.
	player_exit();
	player_enter();
}

void CXCCFileView::OnVxlHvaLoop()
{
	m_hva_loop = (m_vxl_hva_loop.GetCheck() == BST_CHECKED);
}

bool CXCCFileView::apply_loaded_pal(const Cvirtual_binary& data, const string& display_name)
{
	// Parse the bytes as a PAL. Cpal_file derives from Ccc_file; load()
	// takes the raw buffer and (via base) sets up size+data without needing
	// a backing file.
	Cpal_file pf;
	pf.load(data);
	if (!pf.is_valid())
		return false;
	CMainFrame* mf = GetMainFrame();
	// Append a fresh pal_list entry under a synthetic "Loaded" tree node so
	// it shows up in Select Palette and participates in Ctrl+Q traversal.
	// Lazy-create the root once per session by scanning for an existing
	// "Loaded" entry with parent == -1; reuse if present so multiple loads
	// cluster under one node instead of spamming the tree.
	int loaded_root = -1;
	for (auto& it : mf->pal_map_list_mut())
	{
		if (it.second.parent == -1 && it.second.name == "Loaded")
		{
			loaded_root = it.first;
			break;
		}
	}
	if (loaded_root == -1)
	{
		loaded_root = mf->pal_list_create_map("Loaded", -1);
		// Mark session_only so reload_pal_paths() doesn't erase it.
		mf->pal_map_list_mut()[loaded_root].session_only = true;
	}
	// Dedupe: if a previous load under the Loaded root already has the same
	// display name AND the same palette bytes, reselect it. Without the
	// bytes check, two distinct MIXes with same-named PALs (e.g. both
	// containing 'palette.pal' with different colors) would collide on the
	// stale entry, painting B with A's palette — exactly the failure mode
	// we hit when navigating between paired SHPs from different MIXes.
	int new_idx = -1;
	{
		auto& pl = mf->pal_list_mut();
		for (size_t i = 0; i < pl.size(); i++)
		{
			if (pl[i].parent != loaded_root || pl[i].name != display_name)
				continue;
			if (memcmp(pl[i].palette, pf.get_data(), sizeof(t_palette)) == 0)
			{
				new_idx = static_cast<int>(i);
				break;
			}
		}
	}
	if (new_idx < 0)
	{
		t_pal_list_entry e;
		e.name = display_name;
		memcpy(e.palette, pf.get_data(), sizeof(t_palette));
		e.parent = loaded_root;
		mf->pal_list_mut().push_back(e);
		new_idx = static_cast<int>(mf->pal_list_mut().size()) - 1;
	}
	// Drive the view through the same path as Ctrl+Q / auto_select: set the
	// global palette index, which invalidates the file-info pane. The next
	// paint reads get_pal_data() and rebuilds the color table.
	mf->set_palette(new_idx);
	mf->set_msg(display_name + " selected");
	// Bump player BGRA cache so any cached SHP frames repaint with the new
	// palette on the next tick.
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view())
		player_prefill_bgra_cache();
	Invalidate();
	return true;
}

void CXCCFileView::try_auto_load_paired_pal()
{
	if (!m_is_open || !is_paletted_file() || !m_source_mix)
		return;
	string file_base;
	{
		Cfname fn(m_fname);
		file_base = fn.get_ftitle();
		for (auto& c : file_base) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	if (file_base.empty())
		return;
	// Use the same pairing rule as the Load PAL... button: exact stem match
	// preferred, otherwise first-4-chars prefix (Westwood data clusters PALs
	// by 4-letter stem: flashmuz <-> flashbeam, tibtree <-> tibsnow). Rank
	// candidates by longest common prefix length so the closest name wins;
	// ties broken by alphabetic order for determinism.
	struct pal_hit { int id; string name; size_t score; };
	std::vector<pal_hit> hits;
	const int n_total = m_source_mix->get_c_files();
	for (int i = 0; i < n_total; i++)
	{
		const int id = m_source_mix->get_id(i);
		string name = m_source_mix->get_name(id);
		if (name.size() < 5)
			continue;
		string lc = name;
		for (auto& c : lc) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		if (lc.compare(lc.size() - 4, 4, ".pal") != 0)
			continue;
		const string pal_base = lc.substr(0, lc.size() - 4);
		size_t j = 0;
		const size_t m = (std::min)(file_base.size(), pal_base.size());
		while (j < m && file_base[j] == pal_base[j]) j++;
		const bool exact = (pal_base == file_base);
		const bool prefix_ok = (file_base.size() >= 4 && pal_base.size() >= 4 && j >= 4);
		if (!exact && !prefix_ok)
			continue;
		hits.push_back({ id, name, exact ? SIZE_MAX : j });
	}
	if (hits.empty())
		return;
	std::sort(hits.begin(), hits.end(), [](const pal_hit& a, const pal_hit& b) {
		if (a.score != b.score) return a.score > b.score;
		return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	const pal_hit& best = hits.front();
	Cvirtual_binary data = m_source_mix->get_vdata(best.id);
	if (data.size() == 0)
		return;
	apply_loaded_pal(data, best.name);
}

void CXCCFileView::OnLoadPal()
{
	open_load_pal_dialog();
}

void CXCCFileView::open_load_pal_dialog()
{
	if (!m_is_open || !is_paletted_file())
		return;
	// Searchable picker dialog. Replaces the unmanageably-tall TrackPopupMenu
	// that scrolled hundreds of entries off-screen on PAL-rich MIXes (ra2.mix
	// etc.). Dialog handles ranking, filter, live preview, Browse disk and
	// Cancel-revert internally; we just open it.
	CLoadPalDlg dlg(this);
	dlg.set(GetMainFrame(), this, m_source_mix, m_fname);
	dlg.DoModal();
}

void CXCCFileView::OnPlayerGridSel()
{
	if (!m_player_controls_created) return;
	// 3-state cycle: 0 (off) -> 1 (TS) -> 2 (RA2).
	m_player_grid_mode = (m_player_grid_mode + 1) % 3;
	player_update_grid_label();
	// Grid lines are baked into the cached BGRA bytes (per-paint cost is then
	// memcpy + StretchBlt). Bump version + re-prefill so the next animation
	// tick is a hit. VXL gets its grid drawn per-paint (no SHP cache there).
	m_player_bgra_version++;
	if (m_player_mode && !is_vxl_view()) player_prefill_bgra_cache();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::OnPlayerFpsChange()
{
	if (!m_player_controls_created)
		return;
	CString s;
	m_player_fps_edit.GetWindowText(s);
	int v = atoi(s);
	if (v < 1) v = 1;
	if (v > 120) v = 120;
	if (v != m_player_fps)
	{
		m_player_fps = v;
		if (m_player_mode && m_player_playing)
		{
			KillTimer(1);
			SetTimer(1, 1000 / m_player_fps, NULL);
		}
	}
}

void CXCCFileView::auto_select()
{
	if (!m_can_pick)
	{
		// View->Game (m_game) is the user's explicit selection; the -1 sentinel
		// means Auto, in which case we pass game_unknown so the filter applies
		// no bias and rotates rules in score order as before.
		t_game cur = GetMainFrame()->get_game();
		if (static_cast<int>(cur) < 0 || cur >= game_unknown)
			cur = game_unknown;
		m_palette_filter.select(m_ft, m_cx, m_cy, m_fname, cur);
		m_can_pick = true;
	}
	t_game game;
	int i = 4;
	while (i--)
	{
		string palette = m_palette_filter.pick(game);
		if (!palette.empty() && GetMainFrame()->auto_select(game, palette))
			break;
	}
}

bool CXCCFileView::can_auto_select()
{
	return m_is_open;
}
