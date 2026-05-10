#include "stdafx.h"
#include "MainFrm.h"
#include "XCCFileView.h"
#include "resource.h"
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
#include <hva_file.h>
#include <id_log.h>
#include <map_ra_ini_reader.h>
#include <map_td_ini_reader.h>
#include <map_ts_ini_reader.h>
#include <mp3_file.h>
#include <mix_rg_file.h>
#include <pak_file.h>
#include <pal_file.h>
#include <pcx_decode.h>
#include <pcx_file.h>
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
#include <vxl_file.h>
#include <wav_file.h>
#include <wsa_dune2_file.h>
#include <wsa_file.h>
#include <png_file.h>
#include <numbers>
#include "theme.h"

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
	ON_EN_CHANGE(IDC_PLAYER_FPS_EDIT, OnPlayerFpsChange)
	ON_BN_CLICKED(IDC_PLAYER_SHADOWS, OnPlayerShadows)
	ON_BN_CLICKED(IDC_PLAYER_BG, OnPlayerBg)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_PLAYER_SIDE0, IDC_PLAYER_SIDE7, OnPlayerSide)
	ON_BN_CLICKED(IDC_PLAYER_SIDE_CUSTOM, OnPlayerSideCustom)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_VXL_SIDE0, IDC_VXL_SIDE7, OnVxlSide)
	ON_BN_CLICKED(IDC_VXL_SIDE_CUSTOM, OnVxlSideCustom)
	ON_CBN_SELCHANGE(IDC_PLAYER_GRID_SEL, OnPlayerGridSel)
	ON_WM_DRAWITEM()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

void CXCCFileView::OnLButtonDown(UINT nFlags, CPoint point)
{
	// Click steals focus so M actually reaches our key handler.
	SetFocus();
	if (is_vxl_view())
	{
		// Only start orbit drag in the image area, not over the controls band.
		CRect cr;
		GetClientRect(&cr);
		if (point.y < cr.bottom - 32)
		{
			m_vxl_dragging = true;
			m_vxl_drag_origin = point;
			m_vxl_drag_yaw0 = m_vxl_yaw;
			m_vxl_drag_pitch0 = m_vxl_pitch;
			SetCapture();
			return;
		}
	}
	CScrollView::OnLButtonDown(nFlags, point);
}

void CXCCFileView::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_vxl_dragging)
	{
		m_vxl_dragging = false;
		ReleaseCapture();
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
		CRect cr;
		GetClientRect(&cr);
		if (point.y < cr.bottom - player_band_h())
		{
			m_player_panning = true;
			m_player_pan_origin = point;
			m_player_pan_x0 = m_player_pan_x;
			m_player_pan_y0 = m_player_pan_y;
			SetCapture();
			::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
			return;
		}
	}
	CScrollView::OnRButtonDown(nFlags, point);
}

void CXCCFileView::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (m_player_panning)
	{
		m_player_panning = false;
		ReleaseCapture();
		return;
	}
	CScrollView::OnRButtonUp(nFlags, point);
}

void CXCCFileView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_vxl_dragging && (nFlags & MK_LBUTTON))
	{
		// 3dsmax-style orbit: horizontal drag = yaw, vertical drag = pitch.
		// ~0.4 deg per pixel feels right for the tiny VXL framebuffer.
		const double k = 0.4 * 3.14159265358979323846 / 180.0;
		m_vxl_yaw = m_vxl_drag_yaw0 + (point.x - m_vxl_drag_origin.x) * k;
		m_vxl_pitch = m_vxl_drag_pitch0 + (point.y - m_vxl_drag_origin.y) * k;
		// Clamp pitch so the camera doesn't flip past straight up/down.
		const double lim = 89.0 * 3.14159265358979323846 / 180.0;
		if (m_vxl_pitch > lim) m_vxl_pitch = lim;
		if (m_vxl_pitch < -lim) m_vxl_pitch = -lim;
		CRect cr;
		GetClientRect(&cr);
		cr.bottom -= 32;
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	if (m_player_panning && (nFlags & MK_RBUTTON))
	{
		m_player_pan_x = m_player_pan_x0 + (point.x - m_player_pan_origin.x);
		m_player_pan_y = m_player_pan_y0 + (point.y - m_player_pan_origin.y);
		// Repaint the image area only (margins included). Skip the control
		// band so the panning doesn't strobe the buttons.
		CRect cr;
		GetClientRect(&cr);
		cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	CScrollView::OnMouseMove(nFlags, point);
}

BOOL CXCCFileView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
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

void CXCCFileView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == 'M')
	{
		m_show_alpha_only = !m_show_alpha_only;
		Invalidate();
		return;
	}
	if (nChar == 'P' && is_playable_file())
	{
		if (m_player_mode)
			player_exit();
		else
			player_enter();
		return;
	}
	if (m_player_mode && nChar == VK_LEFT)
	{
		player_set_frame(m_player_frame - 1);
		return;
	}
	if (m_player_mode && nChar == VK_RIGHT)
	{
		player_set_frame(m_player_frame + 1);
		return;
	}
	bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	if (ctrl && m_zoomable_file && (nChar == '0' || nChar == VK_NUMPAD0))
	{
		if (m_zoom_pct != 100)
		{
			m_zoom_pct = 100;
			m_text_cache_valid = false;
			Invalidate();
		}
		return;
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

BOOL CXCCFileView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CPoint position = GetScrollPosition();
	SHORT shiftState = GetAsyncKeyState(VK_SHIFT);
	bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

	if (ctrl && m_player_mode)
	{
		// Player zoom: works for SHP/WSA frames and the VXL orbit viewer.
		// Seed from the currently-displayed s_pct (auto-fit or 100 for Native)
		// so the first wheel tick zooms relative to what the user sees, not 0.
		int cur = m_player_zoom_pct;
		if (cur <= 0)
		{
			if (m_player_native_size)
				cur = 100;
			else
			{
				CRect rc; GetClientRect(&rc);
				int avail_w = rc.Width();
				int avail_h = rc.Height() - player_band_h();
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
		int step = zDelta > 0 ? 25 : -25;
		int next = cur + step;
		if (next < 25) next = 25;
		if (next > 1600) next = 1600;
		if (next != m_player_zoom_pct)
		{
			m_player_zoom_pct = next;
			// Reset pan so the new zoom level starts centered. Without this,
			// the user's previous pan would persist into the new zoom and
			// could leave the image out of view at a higher magnification.
			m_player_pan_x = m_player_pan_y = 0;
			Invalidate(FALSE);
		}
		return TRUE;
	}
	if (ctrl && m_zoomable_file)
	{
		int step = zDelta > 0 ? 25 : -25;
		int next = m_zoom_pct + step;
		if (next < 25) next = 25;
		if (next > 1600) next = 1600;
		if (next != m_zoom_pct)
		{
			m_zoom_pct = next;
			m_text_cache_valid = false;
			Invalidate();
		}
		return TRUE;
	}
	if (shiftState)
	{
		ScrollToPosition(CPoint(position.x - zDelta, position.y));
		return false;
	}
	ScrollToPosition(CPoint(position.x, position.y - zDelta));
	return false;
}

void CXCCFileView::OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt)
{
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
	test_brush.CreateSolidBrush(m_colour);
	//m_font.CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
	m_font.CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Lucida Console");
	//m_font.CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
	//m_font.CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, ""); //default font, but if it isn't monospace it sucks
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
				for (int i = 0; i < c_images; i++)
				{
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
					CRect r_test(offset, m_y, offset + cx * zoom_pre / 100, m_y + cy * zoom_pre / 100);
					if (pDC->RectVisible(&r_test))
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
						draw_image8(image, cx, cy, pDC, offset);
						delete[] image;
					}
					m_y += cy + m_y_inc;
				}
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
				const byte* r = image.image();
				for (int i = 0; i < f.cf(); i++)
				{
					draw_image8(r, f.cx(), f.cy(), pDC, offset);
					r += f.cb_image();
					m_y += f.cy() + m_y_inc;
				}
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
				for (int i = 0; i < c_images; i++)
				{
#ifndef NDEBUG	//this doesn't display right and i don't know if it's useful information at all
					draw_info("Radar Color:", "R:" + nwzl(3, f.get_image_header(i)->red) + " G:" + nwzl(3, f.get_image_header(i)->green) + " B:" + nwzl(3, f.get_image_header(i)->blue) + " A:" + nwzl(3, f.get_image_header(i)->alpha));
					CBrush box;
					CBrush color;
					box.CreateSolidBrush(RGB(0, 0, 0));
					color.CreateSolidBrush(RGB(f.get_image_header(i)->red, f.get_image_header(i)->green, f.get_image_header(i)->blue));
					//Draw box that will fill the background edges, needed for light colors
					pDC->FillRect(CRect(CPoint(94, m_y - 12), CSize(26, m_y_inc * 2 / 3 + 2)), &box);
					//Draw the actual color
					pDC->FillRect(CRect(CPoint(95, m_y - 11), CSize(24, m_y_inc * 2 / 3)), &color);
					draw_info("Frame Flags:", nh(8, f.get_image_header(i)->flags));
					draw_info("Unknown:", nh(8, f.get_image_header(i)->zero));
#endif
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					if (cx && cy)
					{
						int zoom_pre = (m_zoomable_file && m_zoom_pct > 0) ? m_zoom_pct : 100;
						CRect r_test(offset, m_y, offset + cx * zoom_pre / 100, m_y + cy * zoom_pre / 100);
						if (pDC->RectVisible(&r_test))
						{
							if (f.is_compressed(i))
							{
								Cvirtual_binary image;
								RLEZeroTSDecompress(f.get_image(i), image.write_start(cx * cy), cx, cy);
								draw_image8(image.data(), cx, cy, pDC, offset);
							}
							else
								draw_image8(f.get_image(i), cx, cy, pDC, offset);
						}
						m_y += cy + m_y_inc;
					}
				}
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
					byte* image = new byte[c_pixels];
					byte* image_s = new byte[c_pixels];
					char* image_z = new char[c_pixels];
					m_y += m_y_inc;
					for (int yr = 0; yr < 8; yr++)
					{
						for (int xr = 0; xr < 8; xr++)
						{
							{
								memset(image, 0, c_pixels);
								memset(image_s, 0, c_pixels);
								memset(image_z, CHAR_MIN, c_pixels);
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
								switch (vxl_mode)
								{
								case 0:
									draw_image8(image, cl, cl, pDC, xr * (cl + m_y_inc) + offset);
									break;
								case 1:
									{
										t_palette gray_palette;
										if (section_tailer.unknown == 2)
										{
											for (int i = 0; i < 256; i++)
												gray_palette[i].r = gray_palette[i].g = gray_palette[i].b = i * 255 / 35;
										}
										else
										{
											for (int i = 0; i < 256; i++)
												gray_palette[i].r = gray_palette[i].g = gray_palette[i].b = i;
										}
										load_color_table(gray_palette, false);
										draw_image8(image_s, cl, cl, pDC, xr * (cl + m_y_inc) + offset);
									}
									break;
								case 2:
									{
										int min_z = INT_MAX;
										int max_z = INT_MIN;
										int o;
										for (o = 1; o < c_pixels; o++)
										{
											int v = image_z[o];
											if (v == CHAR_MIN)
												continue;
											if (v < min_z)
												min_z = v;
											if (v > max_z)
												max_z = v;
										}
										for (o = 0; o < c_pixels; o++)
										{
											if (image_z[o] == CHAR_MIN)
												image_z[o] = -1;
											else
												image_z[o] -= min_z;
										}
										max_z -= min_z;
										t_palette gray_palette;
										for (int p = 0; p < max_z; p++)
											gray_palette[p].r = gray_palette[p].g = gray_palette[p].b = p * 255 / max_z;
										gray_palette[0xff].r = 0;
										gray_palette[0xff].g = 0;
										gray_palette[0xff].b = 0xff;
										load_color_table(gray_palette, false);
										draw_image8(reinterpret_cast<const byte*>(image_z), cl, cl, pDC, xr * (cl + m_y_inc) + offset);
										break;
									}
								}
							}
						}
						m_y += cl + m_y_inc;
					}
					delete[] image_z;
					delete[] image_s;
					delete[] image;
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
	post_open(f);
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
		int cb_max_data = (m_ft == ft_dds || m_ft == ft_jpeg || m_ft == ft_map_td || m_ft == ft_map_ra
			|| m_ft == ft_map_ts || m_ft == ft_mix_rg || m_ft == ft_pcx || m_ft == ft_png || m_ft == ft_shp
			|| m_ft == ft_shp_ts || m_ft == ft_tga || m_ft == ft_vxl || m_ft == ft_wsa_dune2
			|| m_ft == ft_wsa || m_ft == ft_xif) ? m_size :
			(m_ft == ft_csf ? 64 << 8 : 256 << 10);
		int cb_data = m_size > cb_max_data ? cb_max_data : m_size;
		f.read(m_data.write_start(cb_data), cb_data);
		f.close();
		m_text_cache_valid = false;
		m_is_open = true;
		m_zoom_pct = 100;
		m_zoomable_file =
			m_ft == ft_dds || m_ft == ft_cps ||
			m_ft == ft_jpeg || m_ft == ft_jpeg_single ||
			m_ft == ft_png || m_ft == ft_png_single ||
			m_ft == ft_pcx || m_ft == ft_pcx_single ||
			m_ft == ft_tga || m_ft == ft_tga_single ||
			m_ft == ft_bmp;
	}
	if (m_player_mode)
		player_exit();
	ScrollToPosition(CPoint(0, 0));
	Invalidate();
}

void CXCCFileView::close_f()
{
	m_is_open = false;
	m_text_cache.clear();
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
		Cvxl_file f;
		f.load(m_data);
		load_color_table(get_default_palette(), true);

		// Build the object-space point cloud once. The viewer rasterizes it
		// per frame at the current m_vxl_yaw/m_vxl_pitch.
		m_vxl_cloud.clear();
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
			// Section-local occupancy grid for neighbor-based normal derivation.
			// Each voxel's normal points away from its empty neighbor cells (sum
			// of empty-side unit vectors). Store color sentinel 0 = empty so a
			// single byte per cell suffices; 1 = occupied.
			std::vector<unsigned char> occ(static_cast<size_t>(cx) * cy * cz, 0);
			auto occ_idx = [cx, cy](int x, int y, int z) { return x + cx * (y + cy * z); };
			// Pass 1: parse spans into a parallel scratch list of (lx,ly,lz,color)
			// and populate the occupancy grid.
			struct local_voxel { int lx, ly, lz; unsigned char color; };
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
							locals.push_back({ x, y, z, *r });
							if (x >= 0 && x < cx && y >= 0 && y < cy && z >= 0 && z < cz)
								occ[occ_idx(x, y, z)] = 1;
							r += 2;
							z++;
						}
						r++;
					}
				}
			}
			// Pass 2: emit world-space voxels with neighbor-derived normals.
			// Local-space normal is the sum of unit vectors pointing toward each
			// empty 6-neighbor; this picks out the lit-able cube faces. Y is
			// flipped to match the position flip below. Then rotate by the 3x3
			// rotation submatrix of st.transform (translation column doesn't
			// apply to directions). Renormalize, with a fallback +Z if the voxel
			// is fully surrounded (no visible faces).
			m_vxl_cloud.reserve(m_vxl_cloud.size() + locals.size());
			for (const auto& lv : locals)
			{
				const int x = lv.lx, y = lv.ly, z = lv.lz;
				double lx = (x + 0.5 - cx / 2.0) * sx;
				// Flip file Y so the model's front faces the camera at
				// yaw=0 (matches Vengi's VXL viewer convention).
				double ly = (cy - y - 0.5 - cy / 2.0) * sy;
				double lz = (z + 0.5 - cz / 2.0) * sz;
				double wx = st.transform[0][0] * lx + st.transform[0][1] * ly + st.transform[0][2] * lz + st.transform[0][3];
				double wy = st.transform[1][0] * lx + st.transform[1][1] * ly + st.transform[1][2] * lz + st.transform[1][3];
				double wz = st.transform[2][0] * lx + st.transform[2][1] * ly + st.transform[2][2] * lz + st.transform[2][3];
				// Local-space normal from empty-neighbor sides.
				float lnx = 0.0f, lny = 0.0f, lnz = 0.0f;
				auto empty = [&](int xx, int yy, int zz) {
					if (xx < 0 || xx >= cx || yy < 0 || yy >= cy || zz < 0 || zz >= cz)
						return true;
					return occ[occ_idx(xx, yy, zz)] == 0;
				};
				if (empty(x - 1, y, z)) lnx -= 1.0f;
				if (empty(x + 1, y, z)) lnx += 1.0f;
				if (empty(x, y - 1, z)) lny += 1.0f; // Y is flipped on emit
				if (empty(x, y + 1, z)) lny -= 1.0f;
				if (empty(x, y, z - 1)) lnz -= 1.0f;
				if (empty(x, y, z + 1)) lnz += 1.0f;
				if (lnx == 0.0f && lny == 0.0f && lnz == 0.0f)
					lnz = 1.0f;
				// Rotate by 3x3 submatrix of section transform (directions only).
				float wnx = static_cast<float>(st.transform[0][0]) * lnx + static_cast<float>(st.transform[0][1]) * lny + static_cast<float>(st.transform[0][2]) * lnz;
				float wny = static_cast<float>(st.transform[1][0]) * lnx + static_cast<float>(st.transform[1][1]) * lny + static_cast<float>(st.transform[1][2]) * lnz;
				float wnz = static_cast<float>(st.transform[2][0]) * lnx + static_cast<float>(st.transform[2][1]) * lny + static_cast<float>(st.transform[2][2]) * lnz;
				float nlen = std::sqrt(wnx * wnx + wny * wny + wnz * wnz);
				if (nlen > 1e-6f) { wnx /= nlen; wny /= nlen; wnz /= nlen; }
				else              { wnx = 0; wny = 0; wnz = 1; }
				t_vxl_voxel v{ wx, wy, wz, lv.color, wnx, wny, wnz };
				m_vxl_cloud.push_back(v);
			}
		}

		if (m_vxl_cloud.empty())
			return;

		double max_r2 = 0;
		for (const auto& v : m_vxl_cloud)
		{
			double r2 = v.x * v.x + v.y * v.y + v.z * v.z;
			if (r2 > max_r2) max_r2 = r2;
		}
		const double bound = std::sqrt(max_r2);
		m_vxl_half = std::max(8, static_cast<int>(std::ceil(bound)) + 2);
		m_player_cx = 2 * m_vxl_half;
		m_player_cy = 2 * m_vxl_half;
		m_player_cf = 1; // single interactive frame
	}
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
	m_player_playing = true;
	m_player_zoom_pct = 0;
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
		m_player_grid.Create("Grid", WS_CHILD | BS_PUSHBUTTON, r, this, IDC_PLAYER_GRID);
		m_player_native.Create("Native", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, r, this, IDC_PLAYER_NATIVE);
		m_player_slider.Create(WS_CHILD | TBS_HORZ | TBS_NOTICKS, r, this, IDC_PLAYER_SLIDER);
		m_player_label.Create("", WS_CHILD | SS_LEFT, r, this, IDC_PLAYER_FRAME_LABEL);
		m_player_fps_label.Create("FPS", WS_CHILD | SS_LEFT, r, this, IDC_PLAYER_FPS_LABEL);
		m_player_fps_edit.Create(WS_CHILD | ES_NUMBER | ES_RIGHT | WS_BORDER, r, this, IDC_PLAYER_FPS_EDIT);
		m_player_fps_spin.Create(WS_CHILD | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS, r, this, IDC_PLAYER_FPS_SPIN);
		m_player_fps_spin.SetBuddy(&m_player_fps_edit);
		m_player_fps_spin.SetRange(1, 120);
		m_player_play.SetFont(&m_font);
		m_player_reverse.SetFont(&m_font);
		m_player_grid.SetFont(&m_font);
		m_player_native.SetFont(&m_font);
		m_player_label.SetFont(&m_font);
		m_player_fps_label.SetFont(&m_font);
		m_player_fps_edit.SetFont(&m_font);
		theme::apply_window(m_player_play.GetSafeHwnd());
		theme::apply_window(m_player_reverse.GetSafeHwnd());
		theme::apply_window(m_player_grid.GetSafeHwnd());
		theme::apply_window(m_player_native.GetSafeHwnd());
		theme::apply_window(m_player_slider.GetSafeHwnd());
		theme::apply_window(m_player_label.GetSafeHwnd());
		theme::apply_window(m_player_fps_label.GetSafeHwnd());
		theme::apply_window(m_player_fps_edit.GetSafeHwnd());
		theme::apply_window(m_player_fps_spin.GetSafeHwnd());

		m_player_shadows.Create("Shadows", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, r, this, IDC_PLAYER_SHADOWS);
		m_player_bg.Create("BG", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, r, this, IDC_PLAYER_BG);
		for (int i = 0; i < 8; i++)
		{
			m_player_side[i].Create("", WS_CHILD | BS_OWNERDRAW, r, this, IDC_PLAYER_SIDE0 + i);
		}
		m_player_side_custom.Create("", WS_CHILD | BS_OWNERDRAW, r, this, IDC_PLAYER_SIDE_CUSTOM);
		// Owner-draw: the system combobox refused to paint dark even with
		// styles matching a known-working app's combo. Owner-draw is the
		// pragmatic fix — paint it ourselves in WM_DRAWITEM and we get
		// guaranteed theming with no uxtheme dependency.
		m_player_iso_grid.Create(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN
			| CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
			r, this, IDC_PLAYER_GRID_SEL);
		m_player_iso_grid.AddString("No Grid");
		m_player_iso_grid.AddString("TS Grid");
		m_player_iso_grid.AddString("RA2 Grid");
		m_player_iso_grid.SetCurSel(m_player_grid_mode);
		m_player_shadows.SetFont(&m_font);
		m_player_bg.SetFont(&m_font);
		m_player_iso_grid.SetFont(&m_font);
		theme::apply_window(m_player_shadows.GetSafeHwnd());
		theme::apply_window(m_player_bg.GetSafeHwnd());
		theme::apply_window(m_player_iso_grid.GetSafeHwnd());
		// Combobox internals: theme the dropdown listbox + edit field too,
		// otherwise the dropped-down list paints white in dark mode.
		{
			COMBOBOXINFO cbi = {};
			cbi.cbSize = sizeof(cbi);
			if (::GetComboBoxInfo(m_player_iso_grid.GetSafeHwnd(), &cbi))
			{
				if (cbi.hwndList) theme::apply_window(cbi.hwndList);
				if (cbi.hwndItem) theme::apply_window(cbi.hwndItem);
			}
		}
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

		m_player_controls_created = true;
	}
	const bool vxl = (m_ft == ft_vxl);
	m_player_slider.SetRange(0, std::max(0, m_player_cf - 1));
	m_player_slider.SetPos(0);
	m_player_fps_edit.SetWindowText(n(m_player_fps).c_str());
	m_player_fps_spin.SetPos(m_player_fps);
	const int playback_show = vxl ? SW_HIDE : SW_SHOW;
	m_player_play.ShowWindow(playback_show);
	m_player_reverse.ShowWindow(playback_show);
	m_player_reverse.SetCheck(m_player_reverse_dir ? BST_CHECKED : BST_UNCHECKED);
	m_player_grid.ShowWindow(SW_SHOW);
	m_player_native.ShowWindow(SW_SHOW);
	m_player_native.SetCheck(m_player_native_size ? BST_CHECKED : BST_UNCHECKED);
	m_player_slider.ShowWindow(playback_show);
	m_player_label.ShowWindow(playback_show);
	m_player_fps_label.ShowWindow(playback_show);
	m_player_fps_edit.ShowWindow(playback_show);
	m_player_fps_spin.ShowWindow(playback_show);
	// Shadow/BG/SideColor controls are SHP-family only (not VXL).
	const int shp_show = vxl ? SW_HIDE : SW_SHOW;
	const bool can_shadows = !vxl && m_player_cf >= 2 && (m_player_cf % 2) == 0;
	m_player_shadows.ShowWindow(shp_show);
	m_player_shadows.EnableWindow(can_shadows ? TRUE : FALSE);
	if (!can_shadows) m_player_shadows_on = false;
	m_player_shadows.SetCheck(m_player_shadows_on ? BST_CHECKED : BST_UNCHECKED);
	// BG toggle applies to both SHP and VXL — palette index 0 = "no voxel" /
	// "transparent" pixel in either case, so the show-bg-vs-checker switch is
	// meaningful for both.
	m_player_bg.ShowWindow(SW_SHOW);
	m_player_bg.SetCheck(m_player_bg_on ? BST_CHECKED : BST_UNCHECKED);
	for (int i = 0; i < 8; i++)
		m_player_side[i].ShowWindow(shp_show);
	m_player_side_custom.ShowWindow(shp_show);
	// VXL parallel: opposite gating — only shown when viewing a VXL.
	const int vxl_show = vxl ? SW_SHOW : SW_HIDE;
	for (int i = 0; i < 8; i++)
		m_vxl_side[i].ShowWindow(vxl_show);
	m_vxl_side_custom.ShowWindow(vxl_show);
	// Game Grid combobox shows for both SHP and VXL — the overlay applies in
	// either case (already drawn for VXL via the post-stretch path below).
	m_player_iso_grid.ShowWindow(SW_SHOW);
	m_player_iso_grid.SetCurSel(m_player_grid_mode);
	player_layout_controls();
	player_update_label();
	if (!vxl)
		SetTimer(1, 1000 / std::max(1, m_player_fps), NULL);
	else
		m_player_playing = false;
	SetScrollSizes(MM_TEXT, CSize(1, 1));
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
		m_player_iso_grid.ShowWindow(SW_HIDE);
	}
	m_player_frames.clear();
	m_vxl_cloud.clear();
	m_vxl_dragging = false;
	if (GetCapture() == this)
		ReleaseCapture();
	m_text_cache_valid = false;
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
	// Bottom row: transport controls (Play, <<, Grid, Native, slider, FPS).
	int y = cr.bottom - H - pad;
	int x = pad;
	m_player_play.MoveWindow(x, y, 60, H);            x += 60 + pad;
	m_player_reverse.MoveWindow(x, y, 30, H);         x += 30 + pad;
	m_player_grid.MoveWindow(x, y, 50, H);            x += 50 + pad;
	m_player_native.MoveWindow(x, y, 60, H);          x += 60 + pad;
	int label_w = 110;
	m_player_label.MoveWindow(x, y + 4, label_w, H - 8); x += label_w + pad;
	int fps_label_w = 26;
	m_player_fps_label.MoveWindow(x, y + 4, fps_label_w, H - 8); x += fps_label_w + 2;
	int fps_w = 44;
	m_player_fps_edit.MoveWindow(x, y, fps_w, H);     x += fps_w + pad;
	int slider_x = x;
	int slider_w = cr.right - slider_x - pad;
	if (slider_w < 60) slider_w = 60;
	m_player_slider.MoveWindow(slider_x, y, slider_w, H);
	if (vxl)
	{
		// VXL: most transport controls are hidden, so the iso-grid combo
		// sits to the right of the Native button on the single visible row.
		// Slider/label/FPS positions above are computed but never shown.
		int gx = pad + 60 + pad + 30 + pad + 50 + pad + 60 + pad;
		m_player_iso_grid.MoveWindow(gx, y, 90, H * 8);
		// Upper row: BG toggle + 9 VXL side-color swatches.
		int y2 = y - H - pad;
		int x2 = pad;
		m_player_bg.MoveWindow(x2, y2, 36, H); x2 += 36 + pad;
		const int swatch = H;
		for (int i = 0; i < 8; i++)
		{
			m_vxl_side[i].MoveWindow(x2, y2, swatch, H);
			x2 += swatch + 2;
		}
		m_vxl_side_custom.MoveWindow(x2, y2, swatch, H);
		return;
	}
	// Upper row (SHP family only): Shadows, BG, 8 side-color swatches.
	int y2 = y - H - pad;
	int x2 = pad;
	m_player_shadows.MoveWindow(x2, y2, 64, H);       x2 += 64 + pad;
	m_player_bg.MoveWindow(x2, y2, 36, H);            x2 += 36 + pad;
	const int swatch = H;
	for (int i = 0; i < 8; i++)
	{
		m_player_side[i].MoveWindow(x2, y2, swatch, H);
		x2 += swatch + 2;
	}
	// 9th swatch — custom (opens color picker).
	m_player_side_custom.MoveWindow(x2, y2, swatch, H); x2 += swatch + pad;
	// Game Grid combo. Use a tall MoveWindow so the dropdown list fits.
	m_player_iso_grid.MoveWindow(x2, y2, 90, H * 8);
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

	// VXL: point-splat the cached point cloud into an 8bpp framebuffer at 2x
	// canvas resolution. The chosen interpolation mode (theme::interp()) then
	// downsamples that supersample buffer to the destination viewport via the
	// shared theme::stretch_image path — same way SHP/WSA frames are scaled.
	// This makes Bilinear/Bicubic/Lanczos actually do real silhouette AA on
	// VXL, rather than the previous Gaussian-splat path that ignored the user
	// choice. SHP/WSA path below stays unchanged.
	Cvirtual_binary vxl_buf;
	const byte* s = nullptr;
	int vxl_ss_cx = 0, vxl_ss_cy = 0;
	// Shading factor per pixel (0..255 = 0..2.0 at half=128). Only populated
	// for VXL when theme::vxl_shading() is on; SHP/WSA leaves it empty.
	std::vector<unsigned char> vxl_shade;
	if (is_vxl_view())
	{
		if (m_vxl_cloud.empty())
			return;
		const int ss = static_cast<int>(theme::vxl_supersample());
		vxl_ss_cx = m_player_cx * ss;
		vxl_ss_cy = m_player_cy * ss;
		const int half_ss = m_vxl_half * ss;
		const int c_pixels = vxl_ss_cx * vxl_ss_cy;
		const double cosY = std::cos(m_vxl_yaw);
		const double sinY = std::sin(m_vxl_yaw);
		const double cosP = std::cos(m_vxl_pitch);
		const double sinP = std::sin(m_vxl_pitch);
		byte* d = vxl_buf.write_start(c_pixels);
		memset(d, 0, c_pixels);
		vector<short> z_buf(c_pixels, SHRT_MIN);
		// Camera-relative directional light. The viewer is camera-axis aligned
		// (post-rotation rx,py,pz axes), so a fixed direction in this space
		// effectively orbits with the user. Pick "upper-left-front" (-x, -y, +z
		// in screen space → light comes from the upper-left, facing the
		// camera). We dot this with each voxel's camera-space normal.
		const bool shading = theme::vxl_shading();
		const float light_x = -0.40825f;
		const float light_y = -0.40825f;
		const float light_z =  0.81650f;
		const float ambient = 0.35f;	// floor: faces fully turned away from light still get this much
		const float diffuse = 1.0f;	// shading range above ambient (max = ambient + diffuse = 1.20)
		if (shading)
			vxl_shade.assign(c_pixels, 128);	// 128 = neutral 1.0 (ambient + diffuse * 0.5 default)
		for (const auto& v : m_vxl_cloud)
		{
			double rx = v.x * cosY - v.y * sinY;
			double ry = v.x * sinY + v.y * cosY;
			double rz = v.z;
			double py = ry * cosP - rz * sinP;
			double pz = ry * sinP + rz * cosP;
			// At supersample resolution, each voxel covers a ss-by-ss footprint.
			// A single-pixel splat would leave 75% of the silhouette as holes
			// (the "uncovered = background" pixels), so write the full footprint.
			int sx0 = static_cast<int>(rx * ss) + half_ss;
			int sy0 = -static_cast<int>(py * ss) + half_ss;
			short depth = static_cast<short>(pz);
			// Camera-space normal: same yaw/pitch transform as position. We
			// reuse the screen-space convention where camera +Z faces user.
			unsigned char shade_byte = 128;
			if (shading)
			{
				float nrx = static_cast<float>(v.nx * cosY - v.ny * sinY);
				float nry = static_cast<float>(v.nx * sinY + v.ny * cosY);
				float nrz = static_cast<float>(v.nz);
				float nry_p = static_cast<float>(nry * cosP - nrz * sinP);
				float nrz_p = static_cast<float>(nry * sinP + nrz * cosP);
				// Screen Y is flipped (we negate py earlier), so flip the
				// normal Y for the dot product to match.
				float ndotl = nrx * light_x + (-nry_p) * light_y + nrz_p * light_z;
				if (ndotl < 0.0f) ndotl = 0.0f;
				float shade = ambient + diffuse * ndotl;
				int sb = static_cast<int>(shade * 128.0f + 0.5f);
				if (sb < 0) sb = 0; else if (sb > 255) sb = 255;
				shade_byte = static_cast<unsigned char>(sb);
			}
			for (int dy = 0; dy < ss; dy++)
			{
				int sy_pix = sy0 + dy;
				if (sy_pix < 0 || sy_pix >= vxl_ss_cy) continue;
				for (int dx = 0; dx < ss; dx++)
				{
					int sx_pix = sx0 + dx;
					if (sx_pix < 0 || sx_pix >= vxl_ss_cx) continue;
					int ofs = sx_pix + vxl_ss_cx * sy_pix;
					if (depth > z_buf[ofs])
					{
						z_buf[ofs] = depth;
						d[ofs] = v.color;
						if (shading)
							vxl_shade[ofs] = shade_byte;
					}
				}
			}
		}
		s = vxl_buf.data();
	}
	else
	{
		if (m_player_frames.empty())
			return;
		s = m_player_frames[m_player_frame].data();
	}
	// Source buffer size (what gets passed to theme::stretch_image as the
	// source DIB). VXL renders at supersample resolution; SHP/WSA at native.
	int cx_s = is_vxl_view() ? vxl_ss_cx : m_player_cx;
	int cy_s = is_vxl_view() ? vxl_ss_cy : m_player_cy;
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
	{
		// Paletted SHP/WSA/VXL path with three optional ASE-style modifiers:
		//   - BG off: index 0 paints as alpha-checker (transparent preview).
		//   - Side-color remap: indices 16..31 retinted via brightness * preset.
		//   - Shadow pair: when on and cf is even, blend frame[f + cf/2] black
		//     at 120/255 alpha over the body frame (engine convention).
		const bool show_bg = m_player_bg_on;
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
		const int n = cx_s * cy_s;
		for (int i = 0; i < n; i++)
		{
			byte idx = s[i];
			DWORD bgra;
			if (idx == 0)
			{
				if (show_bg)
					bgra = m_color_table[0];
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
			// Shadow overlay: black tint at 47% alpha over the body pixel.
			if (sshad && sshad[i] != 0)
			{
				float fa = 120.0f / 255.0f;
				float r = static_cast<float>((bgra >> 16) & 0xff) * (1.0f - fa);
				float g = static_cast<float>((bgra >> 8) & 0xff) * (1.0f - fa);
				float b = static_cast<float>(bgra & 0xff) * (1.0f - fa);
				int rr = static_cast<int>(r + 0.5f), gg = static_cast<int>(g + 0.5f), bb = static_cast<int>(b + 0.5f);
				bgra = static_cast<DWORD>(bb) | (static_cast<DWORD>(gg) << 8) | (static_cast<DWORD>(rr) << 16);
			}
			// VXL directional shading: scale the voxel color by the per-pixel
			// shade factor that the splat wrote (128 = neutral 1.0). Skips
			// background pixels (idx == 0) so the bg color / alpha-checker stay
			// at full intensity.
			if (!vxl_shade.empty() && idx != 0)
			{
				int sb = vxl_shade[i];
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
	// Game Grid overlay (isometric guide, ASE convention). Drawn into the
	// source DIB before scaling so the lines participate in the chosen
	// interpolation. Works for SHP (skips sprite pixels via index 0) and
	// VXL (skips covered pixels via the BGRA buffer's non-bg color).
	if (m_player_grid_mode > 0)
	{
		const int tileW = (m_player_grid_mode == 1) ? 48 : 60;
		const int gcx = cx_s / 2;
		const int gcy = cy_s;
		const DWORD line = 0x00FFFFFF;
		// Sprite-content check via the paletted source buffer: 0 = uncovered
		// (background) for both SHP/WSA frames and the VXL nearest splat.
		for (int py = 0; py < cy_s; py++)
		{
			for (int px = 0; px < cx_s; px++)
			{
				int i = px + cx_s * py;
				if (s[i] != 0) continue; // sprite pixel — keep
				int dx = px - gcx;
				int dy = py - gcy;
				int u = dx + 2 * dy;
				int v = 2 * dy - dx;
				int au = u % tileW; if (au < 0) au += tileW;
				int av = v % tileW; if (av < 0) av += tileW;
				if (au < 2 || av < 2 || au > tileW - 2 || av > tileW - 2)
					p_dib[i] = line;
			}
		}
	}
	// FXAA: post-process the source DIB before stretch_image. Run on the
	// supersample buffer so a single edge pass smooths voxel staircases
	// before the downscale, rather than hunting for them in the smaller
	// destination where they've already been mixed by the resampler.
	if (theme::fxaa())
		theme::apply_fxaa(p_dib, cx_s, cy_s);
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
	theme::stretch_image(pDC, x_d, y_d, cx_d, cy_d, &mem_dc, h_dib, p_dib, cx_s, cy_s);
	pDC->RestoreDC(-1);
	mem_dc.SelectObject(old);
	DeleteObject(h_dib);
}

void CXCCFileView::OnSize(UINT nType, int cx, int cy)
{
	CScrollView::OnSize(nType, cx, cy);
	if (m_player_mode)
		player_layout_controls();
}

void CXCCFileView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1 && m_player_mode && m_player_playing && m_player_cf > 0)
	{
		int range = m_player_cf;
		if (m_player_shadows_on && m_player_cf >= 2 && (m_player_cf % 2) == 0)
			range = m_player_cf / 2;
		int next;
		if (m_player_reverse_dir)
		{
			next = m_player_frame - 1;
			if (next < 0)
				next = range - 1;
		}
		else
		{
			next = m_player_frame + 1;
			if (next >= range)
				next = 0;
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
	CScrollView::OnHScroll(nSBCode, nPos, pScrollBar);
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
	// Comboboxes are composed: closed-state edit/button (themed by apply_window
	// on the combobox HWND) + dropped-down listbox (separate HWND). Pull the
	// listbox out via CB_GETCOMBOBOXINFO and theme it explicitly so the
	// dropdown also paints dark.
	if (HWND h = m_player_iso_grid.GetSafeHwnd())
	{
		COMBOBOXINFO cbi = {};
		cbi.cbSize = sizeof(cbi);
		if (::GetComboBoxInfo(h, &cbi))
		{
			if (cbi.hwndList) theme::apply_window(cbi.hwndList);
			if (cbi.hwndItem) theme::apply_window(cbi.hwndItem);
		}
	}
}

void CXCCFileView::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT dis)
{
	// Owner-draw Game Grid combobox: paint dark in dark mode, system colors
	// otherwise. Same handler covers the closed-state field and each
	// dropped-down item (CtlType=ODT_COMBOBOX in both cases, itemID =
	// current sel for the closed state, item index for dropdown items).
	if (dis && nIDCtl == IDC_PLAYER_GRID_SEL && dis->CtlType == ODT_COMBOBOX)
	{
		HDC hdc = dis->hDC;
		RECT r = dis->rcItem;
		const bool dark = theme::is_dark();
		const bool selected = (dis->itemState & ODS_SELECTED) != 0;
		COLORREF bk = dark
			? (selected ? theme::accent() : theme::bg())
			: (selected ? ::GetSysColor(COLOR_HIGHLIGHT) : ::GetSysColor(COLOR_WINDOW));
		COLORREF fg = dark
			? (selected ? theme::accent_text() : theme::text())
			: (selected ? ::GetSysColor(COLOR_HIGHLIGHTTEXT) : ::GetSysColor(COLOR_WINDOWTEXT));
		HBRUSH bkbr = ::CreateSolidBrush(bk);
		::FillRect(hdc, &r, bkbr);
		::DeleteObject(bkbr);
		if (static_cast<int>(dis->itemID) >= 0)
		{
			char buf[64] = {};
			m_player_iso_grid.GetLBText(dis->itemID, buf);
			::SetBkMode(hdc, TRANSPARENT);
			::SetTextColor(hdc, fg);
			RECT tr = r; tr.left += 4;
			::DrawTextA(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if (dis->itemState & ODS_FOCUS)
			::DrawFocusRect(hdc, &r);
		return;
	}
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
	m_player_shadows_on = m_player_shadows.GetCheck() == BST_CHECKED;
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
	m_player_bg_on = m_player_bg.GetCheck() == BST_CHECKED;
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::OnPlayerSide(UINT id)
{
	int i = static_cast<int>(id) - IDC_PLAYER_SIDE0;
	if (i < 0 || i > 7) return;
	// Toggle: clicking the active swatch clears.
	m_player_side_idx = (m_player_side_idx == i) ? -1 : i;
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
		for (int k = 0; k < 8; k++)
			m_player_side[k].Invalidate();
		m_player_side_custom.Invalidate();
		CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
		if (cr.bottom < cr.top) cr.bottom = cr.top;
		InvalidateRect(&cr, FALSE);
		return;
	}
	CColorDialog dlg(m_player_side_custom_color, CC_FULLOPEN | CC_RGBINIT, this);
	if (dlg.DoModal() != IDOK)
		return;
	m_player_side_custom_color = dlg.GetColor();
	m_player_side_idx = 8;
	for (int k = 0; k < 8; k++)
		m_player_side[k].Invalidate();
	m_player_side_custom.Invalidate();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
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
	CColorDialog dlg(m_vxl_side_custom_color, CC_FULLOPEN | CC_RGBINIT, this);
	if (dlg.DoModal() != IDOK)
		return;
	m_vxl_side_custom_color = dlg.GetColor();
	m_vxl_side_idx = 8;
	for (int k = 0; k < 8; k++)
		m_vxl_side[k].Invalidate();
	m_vxl_side_custom.Invalidate();
	CRect cr; GetClientRect(&cr); cr.bottom -= player_band_h();
	if (cr.bottom < cr.top) cr.bottom = cr.top;
	InvalidateRect(&cr, FALSE);
}

void CXCCFileView::OnPlayerGridSel()
{
	if (!m_player_controls_created) return;
	int sel = m_player_iso_grid.GetCurSel();
	if (sel == CB_ERR) sel = 0;
	m_player_grid_mode = sel;
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
		m_palette_filter.select(m_ft, m_cx, m_cy, m_fname);
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
