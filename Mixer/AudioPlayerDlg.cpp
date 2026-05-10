#include "stdafx.h"
#include "AudioPlayerDlg.h"
#include "theme.h"

#include <xap.h>

#include <cmath>
#include <cstdio>

namespace
{
	constexpr UINT TIMER_POLL = 1;
	constexpr UINT POLL_MS = 100;	// 10 Hz is enough for a seek slider + clock
	// Slider range is mapped to a 0..1000 integer space; the actual seek
	// uses the 0..1 progress ratio so the underlying byte alignment math
	// stays in xap_seek.
	constexpr int SLIDER_MAX = 1000;
}

CAudioPlayerDlg::CAudioPlayerDlg(CWnd* pParent)
	: CDialog(CAudioPlayerDlg::IDD, pParent)
{
}

void CAudioPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_AUDIO_PLAY_PAUSE, m_play_pause);
	DDX_Control(pDX, IDC_AUDIO_STOP, m_stop);
	DDX_Control(pDX, IDC_AUDIO_SEEK, m_seek);
	DDX_Control(pDX, IDC_AUDIO_TIME, m_time);
	DDX_Control(pDX, IDC_AUDIO_FILENAME, m_filename);
}

BEGIN_MESSAGE_MAP(CAudioPlayerDlg, CDialog)
	ON_BN_CLICKED(IDC_AUDIO_PLAY_PAUSE, OnPlayPause)
	ON_BN_CLICKED(IDC_AUDIO_STOP, OnStop)
	ON_WM_HSCROLL()
	ON_WM_TIMER()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CAudioPlayerDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	m_seek.SetRange(0, SLIDER_MAX);
	m_seek.SetPos(0);
	update_time_label(0, -1);
	theme::apply_dialog(GetSafeHwnd());
	// Poll timer fires whether playing or paused: while paused we still want
	// to refresh on a seek-drag without depending on movement events.
	SetTimer(TIMER_POLL, POLL_MS, NULL);
	return TRUE;
}

void CAudioPlayerDlg::OnCancel()
{
	// Hitting Esc or the system close box stops playback and hides the
	// dialog (but keeps the C++ object alive for reuse on the next play).
	xap_stop();
	ShowWindow(SW_HIDE);
}

void CAudioPlayerDlg::on_playback_started(const std::string& filename)
{
	if (!GetSafeHwnd())
		return;
	m_filename.SetWindowText(filename.c_str());
	m_play_pause.SetWindowText("Pause");
	m_seek.SetPos(0);
	update_time_label(0, xap_get_duration());
	if (!IsWindowVisible())
		ShowWindow(SW_SHOWNA);	// SHOWNA = no focus steal from the file list
}

void CAudioPlayerDlg::OnPlayPause()
{
	if (xap_is_paused())
	{
		xap_resume();
		m_play_pause.SetWindowText("Pause");
	}
	else
	{
		// If nothing is playing (idle dialog left visible), the pause is a
		// no-op inside xap; the button label stays at "Play" since the
		// timer-driven UI sync below will catch up next tick.
		xap_pause();
		m_play_pause.SetWindowText("Play");
	}
}

void CAudioPlayerDlg::OnStop()
{
	xap_stop();
	ShowWindow(SW_HIDE);
}

void CAudioPlayerDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar && pScrollBar->GetSafeHwnd() == m_seek.GetSafeHwnd())
	{
		// Suppress the polling timer's slider writes while the user is
		// scrubbing — otherwise the thumb fights between the user's drag
		// position and the playback cursor's position.
		if (nSBCode == TB_THUMBTRACK)
		{
			m_user_dragging_seek = true;
			// Live-preview the seek target in the time label so the user
			// sees where they're scrubbing to before releasing. The actual
			// xap_seek only commits on release to avoid hammering the
			// DirectSound buffer with mid-drag stop/seek/play cycles.
			const int pos = m_seek.GetPos();
			const double progress = static_cast<double>(pos) /
				static_cast<double>(SLIDER_MAX);
			const double duration = xap_get_duration();
			if (duration > 0.0)
				update_time_label(progress * duration, duration);
			return;
		}
		// On release (TB_ENDTRACK / TB_THUMBPOSITION), commit the seek.
		const int pos = m_seek.GetPos();
		const double progress = static_cast<double>(pos) /
			static_cast<double>(SLIDER_MAX);
		xap_seek(progress);
		m_user_dragging_seek = false;
		return;
	}
	CDialog::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CAudioPlayerDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != TIMER_POLL)
	{
		CDialog::OnTimer(nIDEvent);
		return;
	}
	const double progress = xap_get_progress();
	const double duration = xap_get_duration();
	if (progress < 0.0 || duration <= 0.0)
	{
		// Idle — playback ended (naturally or via Stop). Auto-hide so the
		// user doesn't have a stale dialog floating after the audio ends.
		// Reuse on next play just calls on_playback_started.
		if (IsWindowVisible())
			ShowWindow(SW_HIDE);
		update_time_label(0, -1);
		m_seek.SetPos(0);
		return;
	}
	if (!m_user_dragging_seek)
		m_seek.SetPos(static_cast<int>(progress * SLIDER_MAX + 0.5));
	update_time_label(progress * duration, duration);
	// Sync button label to actual paused state in case Space-toggled it
	// from outside the dialog.
	CString cur;
	m_play_pause.GetWindowText(cur);
	const char* want = xap_is_paused() ? "Play" : "Pause";
	if (cur != want)
		m_play_pause.SetWindowText(want);
}

HBRUSH CAudioPlayerDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(),
		pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CAudioPlayerDlg::update_time_label(double pos_seconds, double dur_seconds)
{
	std::string s = format_seconds(pos_seconds);
	if (dur_seconds > 0.0)
	{
		s += " / ";
		s += format_seconds(dur_seconds);
	}
	m_time.SetWindowText(s.c_str());
}

std::string CAudioPlayerDlg::format_seconds(double s)
{
	if (s < 0.0) s = 0.0;
	const int total = static_cast<int>(std::floor(s + 0.5));
	const int m = total / 60;
	const int sec = total % 60;
	char buf[32];
	std::snprintf(buf, sizeof buf, "%d:%02d", m, sec);
	return buf;
}
