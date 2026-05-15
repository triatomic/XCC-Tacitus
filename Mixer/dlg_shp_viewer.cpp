#include "stdafx.h"
#include "dlg_shp_viewer.h"
#include "MainFrm.h"
#include "theme.h"
#include "xap.h"

static CMainFrame* GetMainFrame()
{
	return static_cast<CMainFrame*>(AfxGetMainWnd());
}

Cdlg_shp_viewer::Cdlg_shp_viewer(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(Cdlg_shp_viewer::IDD, pParent, "shp_viewer_dlg")
	, m_av_fps(0)
	, m_av_started(false)
	, m_paused(false)
	, m_fps_value(15)
	, m_updating_fps(false)
{
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

static HBITMAP create_bitmap(Cvirtual_image image, bool /*source_was_paletted*/)
{
	if (image.cx() & 3)
	{
		Cvirtual_image d(NULL, image.cx() + 3 & ~3, image.cy(), image.cb_pixel(), image.palette());
		const byte* r = image.image();
		byte* w = d.image_edit();
		for (int y = 0; y < image.cy(); y++)
		{
			memcpy(w, r, image.cx() * image.cb_pixel());
			memset(w + image.cx(), 0, image.cb_pixel() * (- image.cx() & 3));
			r += image.cx();
			w += d.cx();
		}
		image = d;
	}
	memset(image.image_edit(), 0, image.cb_pixel());
	image.increase_color_depth(3);
	image.swap_rb();
	BITMAPINFOHEADER header;
	ZeroMemory(&header, sizeof(BITMAPINFOHEADER));
	header.biSize = sizeof(BITMAPINFOHEADER);
	header.biWidth = image.cx();
	header.biHeight = -image.cy();
	header.biPlanes = 1;
	header.biBitCount = image.cb_pixel() << 3;
	header.biCompression = BI_RGB;
	return CreateDIBitmap(CClientDC(NULL), &header, CBM_INIT, image.image(), reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
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

void Cdlg_shp_viewer::show_frame()
{
	const bool paletted = (m_decoder->cb_pixel() == 1);
	DeleteObject(m_image.SetBitmap(create_bitmap(decode_image(m_frame), paletted)));
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
