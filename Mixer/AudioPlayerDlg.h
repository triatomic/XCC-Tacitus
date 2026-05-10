#pragma once

#include "resource.h"

#include <string>

// Modeless mini-player that floats over the main window when XCC is playing
// audio (.aud / .ogg / .voc / .wav). Driven by the global xap module: the
// dialog never owns the DirectSound buffer, it just polls the xap state via
// xap_get_progress() / xap_get_duration() and sends transport commands via
// xap_pause() / xap_resume() / xap_stop() / xap_seek().
//
// Lifetime: created on first play (CXCCMixerView::ensure_audio_dlg), reused
// across subsequent plays (just rebinds filename + duration). Auto-hides
// when playback ends naturally; the user can also close it explicitly via
// the Stop button or the system close box.
class CAudioPlayerDlg : public CDialog
{
public:
	CAudioPlayerDlg(CWnd* pParent = NULL);

	enum { IDD = IDD_AUDIO_PLAYER };

	// Called from the parent view after a successful xap_play. Updates the
	// filename label, restarts the polling timer, and shows the dialog if
	// it was hidden. duration_seconds is supplied separately because
	// xap_get_duration() may briefly return -1 between thread handoff.
	void on_playback_started(const std::string& filename);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnCancel();	// system close => hide + stop, don't destroy

	afx_msg void OnPlayPause();
	afx_msg void OnStop();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	DECLARE_MESSAGE_MAP()

private:
	void update_time_label(double pos_seconds, double dur_seconds);
	static std::string format_seconds(double s);

	CButton  m_play_pause;
	CButton  m_stop;
	CSliderCtrl m_seek;
	CStatic  m_time;
	CStatic  m_filename;
	bool m_user_dragging_seek = false;
};
