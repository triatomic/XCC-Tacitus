#include "stdafx.h"
#include "dlg_shp_viewer.h"
#include "MainFrm.h"
#include "theme.h"
#include "xap.h"

static CMainFrame* GetMainFrame()
{
	return static_cast<CMainFrame*>(AfxGetMainWnd());
}

// Win32 subclass on IDC_IMAGE so we can scale the video frame to whatever
// size the static currently occupies, instead of SS_BITMAP painting the
// HBITMAP at native resolution and ignoring resizes. The owner Cdlg_shp_viewer
// stashes itself via SetPropW so the wndproc can reach the BGRA buffer.
static const wchar_t* k_owner_prop = L"xcc.video_dlg_owner";
static const wchar_t* k_orig_proc_prop = L"xcc.video_image_orig_proc";

static void paint_image_subclass(HWND h, Cdlg_shp_viewer* owner)
{
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(h, &ps);
	RECT rc;
	GetClientRect(h, &rc);
	const int rc_w = rc.right - rc.left;
	const int rc_h = rc.bottom - rc.top;
	HBRUSH bg = theme::bg_brush();
	if (!owner || owner->m_frame_cx <= 0 || owner->m_frame_cy <= 0 ||
		owner->m_frame_bgra.empty() || rc_w <= 0 || rc_h <= 0)
	{
		FillRect(hdc, &rc, bg ? bg : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
		EndPaint(h, &ps);
		return;
	}
	// Aspect-fit: largest dst within rc preserving source aspect.
	const int sw = owner->m_frame_cx;
	const int sh = owner->m_frame_cy;
	int dw = rc_w;
	int dh = static_cast<int>(static_cast<long long>(rc_w) * sh / sw);
	if (dh > rc_h)
	{
		dh = rc_h;
		dw = static_cast<int>(static_cast<long long>(rc_h) * sw / sh);
	}
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	const int dx = rc.left + (rc_w - dw) / 2;
	const int dy = rc.top + (rc_h - dh) / 2;
	// Letterbox bars (top/bottom + left/right of the centered image).
	if (dy > rc.top)
	{
		RECT t = { rc.left, rc.top, rc.right, dy };
		FillRect(hdc, &t, bg ? bg : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
	}
	if (dy + dh < rc.bottom)
	{
		RECT b = { rc.left, dy + dh, rc.right, rc.bottom };
		FillRect(hdc, &b, bg ? bg : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
	}
	if (dx > rc.left)
	{
		RECT l = { rc.left, dy, dx, dy + dh };
		FillRect(hdc, &l, bg ? bg : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
	}
	if (dx + dw < rc.right)
	{
		RECT r = { dx + dw, dy, rc.right, dy + dh };
		FillRect(hdc, &r, bg ? bg : reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
	}
	std::vector<DWORD> scaled(static_cast<size_t>(dw) * dh);
	theme::bilinear_resample_bgra(owner->m_frame_bgra.data(), sw, sh,
		scaled.data(), dw, dh);
	BITMAPINFO bmi;
	ZeroMemory(&bmi, sizeof bmi);
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = dw;
	bmi.bmiHeader.biHeight = -dh; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	SetDIBitsToDevice(hdc, dx, dy, dw, dh, 0, 0, 0, dh,
		scaled.data(), &bmi, DIB_RGB_COLORS);
	EndPaint(h, &ps);
}

static LRESULT CALLBACK image_subclass_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	WNDPROC orig = reinterpret_cast<WNDPROC>(GetPropW(h, k_orig_proc_prop));
	switch (msg)
	{
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		Cdlg_shp_viewer* owner =
			reinterpret_cast<Cdlg_shp_viewer*>(GetPropW(h, k_owner_prop));
		paint_image_subclass(h, owner);
		return 0;
	}
	case WM_NCDESTROY:
	{
		RemovePropW(h, k_owner_prop);
		RemovePropW(h, k_orig_proc_prop);
		if (orig)
			SetWindowLongPtr(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
		break;
	}
	}
	return orig ? CallWindowProc(orig, h, msg, wp, lp)
		: DefWindowProc(h, msg, wp, lp);
}

Cdlg_shp_viewer::Cdlg_shp_viewer(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(Cdlg_shp_viewer::IDD, pParent, "shp_viewer_dlg")
	, m_frame_cx(0)
	, m_frame_cy(0)
	, m_av_fps(0)
	, m_av_started(false)
	, m_paused(false)
	, m_fps_value(15)
	, m_updating_fps(false)
	, m_fullscreen(false)
	, m_pre_fs_style(0)
	, m_pre_fs_ex_style(0)
{
	ZeroMemory(&m_pre_fs_placement, sizeof m_pre_fs_placement);
	//{{AFX_DATA_INIT(Cdlg_shp_viewer)
	//}}AFX_DATA_INIT
}

void Cdlg_shp_viewer::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(Cdlg_shp_viewer)
	DDX_Control(pDX, IDC_SLIDER, m_slider);
	DDX_Control(pDX, IDC_IMAGE, m_image);
	DDX_Control(pDX, IDC_DURATION, m_duration);
	DDX_Control(pDX, IDC_SHPVIEW_FPS_EDIT, m_fps_edit);
	DDX_Control(pDX, IDC_SHPVIEW_FPS_SPIN, m_fps_spin);
	DDX_Text(pDX, IDC_FRAME, m_index);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(Cdlg_shp_viewer, ETSLayoutDialog)
	//{{AFX_MSG_MAP(Cdlg_shp_viewer)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_PLAY, OnPlay)
	ON_WM_DESTROY()
	ON_EN_CHANGE(IDC_SHPVIEW_FPS_EDIT, OnFpsChange)
	//}}AFX_MSG_MAP
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_NOTIFY(UDN_DELTAPOS, IDC_SHPVIEW_FPS_SPIN, &Cdlg_shp_viewer::OnDeltaposShpviewFpsSpin)
END_MESSAGE_MAP()

HBRUSH Cdlg_shp_viewer::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void Cdlg_shp_viewer::write(Cvideo_decoder* decoder)
{
	m_decoder = decoder;
}

void Cdlg_shp_viewer::write_av(Cvirtual_binary wav, double fps, const std::string& name)
{
	m_av_wav = wav;
	m_av_fps = fps;
	m_av_name = name;
}

static std::string format_mmss(int total_s)
{
	if (total_s < 0)
		total_s = 0;
	char buf[16];
	wsprintfA(buf, "%d:%02d", total_s / 60, total_s % 60);
	return buf;
}

void Cdlg_shp_viewer::update_duration_label()
{
	if (!m_duration.GetSafeHwnd())
		return;
	if (m_av_fps <= 0 || !m_decoder)
	{
		m_duration.SetWindowText("");
		return;
	}
	const int elapsed_s = static_cast<int>(m_frame / m_av_fps);
	const int total_s = static_cast<int>(m_decoder->cf() / m_av_fps);
	const std::string s = format_mmss(elapsed_s) + " / " + format_mmss(total_s);
	m_duration.SetWindowText(s.c_str());
}

BOOL Cdlg_shp_viewer::OnInitDialog()
{
	CreateRoot(VERTICAL)
		<< item(IDC_IMAGE, GREEDY)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_FRAME, NORESIZE)
			<< item(IDC_DURATION, NORESIZE)
			<< item(IDC_SLIDER, GREEDY)
			<< item(IDC_SHPVIEW_FPS_LABEL, NORESIZE)
			<< item(IDC_SHPVIEW_FPS_EDIT, NORESIZE)
			<< item(IDC_SHPVIEW_FPS_SPIN, NORESIZE)
			<< item(IDC_PLAY, NORESIZE)
			<< item(IDOK, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	// Subclass IDC_IMAGE so we own its WM_PAINT (aspect-fit + scale).
	if (HWND hi = m_image.GetSafeHwnd())
	{
		SetPropW(hi, k_owner_prop, this);
		LONG_PTR prev = SetWindowLongPtr(hi, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(image_subclass_proc));
		SetPropW(hi, k_orig_proc_prop, reinterpret_cast<HANDLE>(prev));
	}
	m_slider.SetRange(0, m_decoder->cf() - 1);
	m_frame = 0;
	show_frame();
	update_duration_label();
	m_last_access = 0;
	// Seed FPS: VQA uses the file's declared frame rate; SHP/WSA defaults
	// to 15 (RA1/TD/TS sprite convention). User can edit either.
	const int seed = (m_av_fps > 0)
		? std::max(1, static_cast<int>(m_av_fps + 0.5))
		: 15;
	m_fps_value = seed;
	m_fps_spin.SetBuddy(&m_fps_edit);
	m_fps_spin.SetRange(1, 120);
	m_updating_fps = true;
	{
		char buf[16];
		wsprintfA(buf, "%d", m_fps_value);
		m_fps_edit.SetWindowText(buf);
	}
	m_fps_spin.SetPos(m_fps_value);
	m_updating_fps = false;
	m_timer_id = SetTimer(1, 1000 / std::max(1, m_fps_value), NULL);
	theme::apply_dialog(GetSafeHwnd());
	// Start audio if we were handed a pre-decoded WAV buffer. Button text
	// reflects current transport state: animation always starts playing,
	// so "Pause" is the action label.
	if (m_av_wav.size() && GetMainFrame() && GetMainFrame()->get_ds())
	{
		xap_play(GetMainFrame()->get_ds(), m_av_wav, m_av_name);
		m_av_started = true;
	}
	if (CWnd* btn = GetDlgItem(IDC_PLAY))
		btn->SetWindowText("Pause");
	return true;
}

void Cdlg_shp_viewer::OnDestroy()
{
	// Stop any audio we started so it doesn't outlive the dialog.
	if (m_av_started)
	{
		xap_stop();
		m_av_started = false;
	}
	ETSLayoutDialog::OnDestroy();
}

void Cdlg_shp_viewer::OnTimer(UINT nIDEvent)
{
	if (m_timer_id == nIDEvent)
	{
		int frame = m_slider.GetPos();
		time_t t = time(NULL);
		if (m_frame != frame)
		{
			// User scrubbed the slider — sync the video position. For VQA
			// also seek audio so the two stay aligned; otherwise the next
			// tick's xap_get_progress would snap video right back to where
			// audio is and the scrub would appear to do nothing.
			m_last_access = t;
			m_frame = frame;
			if (m_av_started && m_decoder->cf() > 1)
				xap_seek(static_cast<double>(frame) / (m_decoder->cf() - 1));
		}
		else if (m_paused)
		{
			return;
		}
		else if (m_av_started && m_av_fps > 0)
		{
			// VQA with audio: drive video frame off audio playback position
			// so the two stay aligned regardless of the user-chosen timer
			// rate. xap_get_progress returns 0..1 of the WAV; multiplying
			// by cf gives the target video frame.
			int target = m_frame;
			const double p = xap_get_progress();
			if (p >= 0)
				target = static_cast<int>(p * m_decoder->cf());
			else if (m_frame + 1 < m_decoder->cf())
				target = m_frame + 1;
			if (target >= m_decoder->cf())
				target = m_decoder->cf() - 1;
			if (target == m_frame)
				return;
			m_frame = target;
		}
		else
		{
			// SHP/WSA (or VQA without audio): advance one frame per tick at
			// the user-chosen FPS, wrap at the end. No more 15 s idle wait.
			m_frame++;
			if (m_frame >= m_decoder->cf())
				m_frame = 0;
		}
		show_frame();
	}
	else
		ETSLayoutDialog::OnTimer(nIDEvent);
}

Cvirtual_image Cdlg_shp_viewer::decode_image(int i) const
{
	Cvirtual_image d;
	d.load(NULL, m_decoder->cx(), m_decoder->cy(), m_decoder->cb_pixel(), NULL);
	m_decoder->seek(i);
	m_decoder->decode(d.image_edit());
	if (m_decoder->palette())
	{
		const t_palette_entry* p = m_decoder->palette();
		int i = 0;
		for (; i < 256; i++)
		{
			if ((p[i].r | p[i].g | p[i].b) & 0xc0)
				break;
		}
		d.palette(p, i == 256);
	}
	return d;
}

void Cdlg_shp_viewer::update_frame_bgra(int i)
{
	Cvirtual_image img = decode_image(i);
	const int cx = img.cx();
	const int cy = img.cy();
	if (cx <= 0 || cy <= 0)
	{
		m_frame_bgra.clear();
		m_frame_cx = 0;
		m_frame_cy = 0;
		return;
	}
	// Promote to 24-bit RGB then walk it into 32-bit BGRA (top-down).
	// Mirrors the SHP/WSA player's BGRA cache in XCCFileView.
	img.increase_color_depth(3);
	img.swap_rb();
	m_frame_bgra.assign(static_cast<size_t>(cx) * cy, 0);
	const byte* r = img.image();
	DWORD* w = m_frame_bgra.data();
	for (int y = 0; y < cy; y++)
	{
		for (int x = 0; x < cx; x++)
		{
			const byte b = r[0], g = r[1], rd = r[2];
			w[x] = static_cast<DWORD>(b) | (static_cast<DWORD>(g) << 8)
				| (static_cast<DWORD>(rd) << 16) | (DWORD(0xff) << 24);
			r += 3;
		}
		w += cx;
	}
	m_frame_cx = cx;
	m_frame_cy = cy;
}

void Cdlg_shp_viewer::show_frame()
{
	update_frame_bgra(m_frame);
	if (m_image.GetSafeHwnd())
		m_image.Invalidate(FALSE);
	m_index = m_frame;
	m_slider.SetPos(m_frame);
	update_duration_label();
	UpdateData(false);
}

void Cdlg_shp_viewer::OnPlay()
{
	// Real pause/resume for everything. SHP/WSA: the OnTimer auto-advance
	// branch checks m_paused first and returns. VQA: also pause the audio
	// so the DirectSound buffer stays alive at its current byte offset and
	// the next tick's xap_get_progress doesn't snap video back to where
	// audio is. Slider scrubbing still works while paused.
	m_paused = !m_paused;
	if (m_av_started)
	{
		if (m_paused)
			xap_pause();
		else
			xap_resume();
	}
	if (CWnd* btn = GetDlgItem(IDC_PLAY))
		btn->SetWindowText(m_paused ? "Play" : "Pause");
}

void Cdlg_shp_viewer::OnFpsChange()
{
	if (m_updating_fps || !m_fps_edit.GetSafeHwnd())
		return;
	CString s;
	m_fps_edit.GetWindowText(s);
	if (s.IsEmpty())
		return;
	int v = atoi(s);
	if (v < 1) v = 1;
	if (v > 120) v = 120;
	if (v == m_fps_value)
		return;
	m_fps_value = v;
	restart_timer();
}

void Cdlg_shp_viewer::restart_timer()
{
	if (m_timer_id)
		KillTimer(m_timer_id);
	m_timer_id = SetTimer(1, 1000 / std::max(1, m_fps_value), NULL);
}

void Cdlg_shp_viewer::OnDeltaposShpviewFpsSpin(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMUPDOWN pNMUpDown = reinterpret_cast<LPNMUPDOWN>(pNMHDR);
	// TODO: Add your control notification handler code here
	*pResult = 0;
}

void Cdlg_shp_viewer::OnSize(UINT nType, int cx, int cy)
{
	ETSLayoutDialog::OnSize(nType, cx, cy);
	if (m_fullscreen)
	{
		// Layout engine is bypassed in fullscreen — image fills the whole
		// client area directly so it gets the entire monitor.
		if (m_image.GetSafeHwnd())
			m_image.MoveWindow(0, 0, cx, cy, FALSE);
	}
	if (m_image.GetSafeHwnd())
		m_image.Invalidate(FALSE);
}

void Cdlg_shp_viewer::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = 320;
	lpMMI->ptMinTrackSize.y = 200;
	ETSLayoutDialog::OnGetMinMaxInfo(lpMMI);
}

BOOL Cdlg_shp_viewer::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_F11)
		{
			toggle_fullscreen();
			return TRUE;
		}
		if (pMsg->wParam == VK_ESCAPE && m_fullscreen)
		{
			enter_fullscreen(false);
			return TRUE;
		}
	}
	return ETSLayoutDialog::PreTranslateMessage(pMsg);
}

void Cdlg_shp_viewer::toggle_fullscreen()
{
	enter_fullscreen(!m_fullscreen);
}

void Cdlg_shp_viewer::enter_fullscreen(bool on)
{
	HWND h = GetSafeHwnd();
	if (!h || on == m_fullscreen)
		return;
	const UINT chrome_ids[] = {
		IDC_FRAME, IDC_DURATION, IDC_SLIDER,
		IDC_SHPVIEW_FPS_LABEL, IDC_SHPVIEW_FPS_EDIT, IDC_SHPVIEW_FPS_SPIN,
		IDC_PLAY, IDOK
	};
	if (on)
	{
		m_pre_fs_placement.length = sizeof m_pre_fs_placement;
		GetWindowPlacement(&m_pre_fs_placement);
		m_pre_fs_style = static_cast<DWORD>(GetWindowLong(h, GWL_STYLE));
		m_pre_fs_ex_style = static_cast<DWORD>(GetWindowLong(h, GWL_EXSTYLE));
		for (UINT id : chrome_ids)
			if (CWnd* w = GetDlgItem(id))
				w->ShowWindow(SW_HIDE);
		DWORD style = m_pre_fs_style;
		style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU
			| WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_BORDER | WS_DLGFRAME);
		style |= WS_POPUP;
		DWORD ex = m_pre_fs_ex_style;
		ex &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE
			| WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
		::SetWindowLong(h, GWL_STYLE, static_cast<LONG>(style));
		::SetWindowLong(h, GWL_EXSTYLE, static_cast<LONG>(ex));
		HMONITOR hm = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		mi.cbSize = sizeof mi;
		if (hm && GetMonitorInfo(hm, &mi))
		{
			const int mx = mi.rcMonitor.left;
			const int my = mi.rcMonitor.top;
			const int mw = mi.rcMonitor.right - mi.rcMonitor.left;
			const int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
			::SetWindowPos(h, HWND_TOP, mx, my, mw, mh,
				SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
		}
		m_fullscreen = true;
		if (m_image.GetSafeHwnd())
		{
			RECT rc;
			::GetClientRect(h, &rc);
			m_image.MoveWindow(0, 0, rc.right, rc.bottom, FALSE);
			m_image.Invalidate(FALSE);
		}
	}
	else
	{
		m_fullscreen = false;
		::SetWindowLong(h, GWL_STYLE, static_cast<LONG>(m_pre_fs_style));
		::SetWindowLong(h, GWL_EXSTYLE, static_cast<LONG>(m_pre_fs_ex_style));
		::SetWindowPos(h, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
			| SWP_FRAMECHANGED);
		SetWindowPlacement(&m_pre_fs_placement);
		for (UINT id : chrome_ids)
			if (CWnd* w = GetDlgItem(id))
				w->ShowWindow(SW_SHOW);
		// ETSLayoutDialog reflows on the WM_SIZE produced by SetWindowPlacement;
		// our OnSize then invalidates the image. Belt-and-suspenders here.
		if (m_image.GetSafeHwnd())
			m_image.Invalidate(FALSE);
	}
}
