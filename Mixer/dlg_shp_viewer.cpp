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
	DDX_Text(pDX, IDC_FRAME, m_index);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(Cdlg_shp_viewer, ETSLayoutDialog)
	//{{AFX_MSG_MAP(Cdlg_shp_viewer)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_PLAY, OnPlay)
	ON_WM_DESTROY()
	//}}AFX_MSG_MAP
	ON_WM_CTLCOLOR()
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
			<< item(IDC_PLAY, NORESIZE)
			<< item(IDOK, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	m_slider.SetRange(0, m_decoder->cf() - 1);
	m_frame = 0;
	show_frame();
	update_duration_label();
	m_last_access = 0;
	// VQA: drive the timer at the source frame rate so video and audio
	// stay aligned. Non-VQA decoders (SHP/WSA) keep the legacy 100 ms
	// poll + 15 s auto-advance behaviour.
	const UINT period = (m_av_fps > 0)
		? static_cast<UINT>(1000.0 / m_av_fps + 0.5)
		: 100;
	m_timer_id = SetTimer(1, period > 0 ? period : 1, NULL);
	theme::apply_dialog(GetSafeHwnd());
	// Start audio if we were handed a pre-decoded WAV buffer.
	if (m_av_wav.size() && GetMainFrame() && GetMainFrame()->get_ds())
	{
		xap_play(GetMainFrame()->get_ds(), m_av_wav, m_av_name);
		m_av_started = true;
		if (CWnd* btn = GetDlgItem(IDC_PLAY))
			btn->SetWindowText("Pause");
	}
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
		else if (m_av_fps > 0)
		{
			// VQA: drive video frame off audio playback position so we don't
			// drift on coarse timer ticks. xap_get_progress returns 0..1 of
			// the WAV buffer; the buffer was decoded at the file's frame
			// rate, so progress * cf is the target frame. Falls back to a
			// tick-based advance when audio is paused or unavailable.
			if (m_paused)
				return;
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
		else if (t - m_last_access > 15)
		{
			// SHP/WSA legacy behaviour: auto-advance after 15 s of idle.
			m_frame++;
			m_frame %= m_decoder->cf();
		}
		else
			return;
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
	// SHP/WSA: legacy "kick the 15 s idle auto-advance" behaviour.
	if (m_av_fps <= 0)
	{
		m_last_access = 0;
		return;
	}
	// VQA: real pause/resume. Pause both the audio (DirectSound buffer
	// stays alive at its current byte offset) and the video advance in
	// OnTimer. Slider scrubbing still works while paused.
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
