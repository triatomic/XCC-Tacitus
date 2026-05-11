#pragma once

#include "resource.h"

// Modal capture dialog. Two modes:
//   keyboard: records vk + modifier flags from PreTranslateMessage WM_KEYDOWN.
//   mouse:    records EMouseBtn + modifiers from WM_*BUTTONDOWN / WM_MOUSEWHEEL.
class CKeyCaptureDlg : public CDialog
{
public:
	CKeyCaptureDlg(bool mouse_mode, CWnd* pParent = NULL);

	enum { IDD = IDD_KEY_CAPTURE };

	// Keyboard result (mouse_mode == false).
	BYTE m_vk = 0;
	BYTE m_key_mods = 0;

	// Mouse result (mouse_mode == true).
	BYTE m_btn = 0;    // keybinds::EMouseBtn
	BYTE m_mouse_mods = 0;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	void update_preview();
	bool m_mouse_mode;
	CStatic m_preview;
};
