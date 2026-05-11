#include "stdafx.h"
#include "KeyCaptureDlg.h"
#include "keybinds.h"
#include "theme.h"

CKeyCaptureDlg::CKeyCaptureDlg(bool mouse_mode, CWnd* pParent)
	: CDialog(CKeyCaptureDlg::IDD, pParent), m_mouse_mode(mouse_mode)
{
}

void CKeyCaptureDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_KEYCAP_PREVIEW, m_preview);
}

BEGIN_MESSAGE_MAP(CKeyCaptureDlg, CDialog)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CKeyCaptureDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	if (CWnd* p = GetDlgItem(IDC_KEYCAP_PROMPT))
	{
		p->SetWindowText(m_mouse_mode
			? "Click in this dialog (or roll wheel) with optional Ctrl/Shift/Alt held. Esc cancels."
			: "Press the key combination to assign. Esc cancels.");
	}
	SetWindowText(m_mouse_mode ? "Capture mouse" : "Capture key");
	update_preview();
	theme::apply_dialog(GetSafeHwnd());
	return TRUE;
}

HBRUSH CKeyCaptureDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CKeyCaptureDlg::PreTranslateMessage(MSG* pMsg)
{
	const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
	const bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

	if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN)
	{
		UINT vk = (UINT)pMsg->wParam;
		// Esc cancels.
		if (vk == VK_ESCAPE)
		{
			EndDialog(IDCANCEL);
			return TRUE;
		}
		// Allow Tab to navigate when no modifier; allow Enter on a button.
		if (vk == VK_TAB && !ctrl && !shift && !alt)
			return CDialog::PreTranslateMessage(pMsg);
		if (vk == VK_RETURN && !ctrl && !shift && !alt)
		{
			HWND hf = ::GetFocus();
			char cls[32] = {0};
			::GetClassNameA(hf, cls, sizeof(cls));
			if (_stricmp(cls, "Button") == 0)
				return CDialog::PreTranslateMessage(pMsg);
		}
		if (m_mouse_mode)
		{
			// In mouse capture, modifiers alone are part of the gesture; eat
			// other key events so they don't navigate or close.
			return TRUE;
		}
		// Ignore standalone modifiers in keyboard mode.
		if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU ||
			vk == VK_LCONTROL || vk == VK_RCONTROL ||
			vk == VK_LSHIFT || vk == VK_RSHIFT ||
			vk == VK_LMENU || vk == VK_RMENU ||
			vk == VK_LWIN || vk == VK_RWIN)
		{
			return CDialog::PreTranslateMessage(pMsg);
		}
		m_vk = (BYTE)vk;
		m_key_mods = 0;
		if (ctrl)  m_key_mods |= FCONTROL;
		if (shift) m_key_mods |= FSHIFT;
		if (alt)   m_key_mods |= FALT;
		update_preview();
		return TRUE;
	}

	if (m_mouse_mode)
	{
		// Only accept clicks that landed on the dialog body or our preview
		// label, not on the OK/Cancel buttons (the user needs those to commit).
		auto in_capture_area = [&](HWND h)
		{
			if (!h) return false;
			if (h == GetSafeHwnd()) return true;
			if (h == m_preview.GetSafeHwnd()) return true;
			return false;
		};

		BYTE btn = 0;
		if      (pMsg->message == WM_LBUTTONDOWN && in_capture_area(pMsg->hwnd)) btn = keybinds::mb_left;
		else if (pMsg->message == WM_RBUTTONDOWN && in_capture_area(pMsg->hwnd)) btn = keybinds::mb_right;
		else if (pMsg->message == WM_MBUTTONDOWN && in_capture_area(pMsg->hwnd)) btn = keybinds::mb_middle;
		else if (pMsg->message == WM_XBUTTONDOWN && in_capture_area(pMsg->hwnd))
			btn = (HIWORD(pMsg->wParam) == XBUTTON1) ? keybinds::mb_x1 : keybinds::mb_x2;
		else if (pMsg->message == WM_MOUSEWHEEL)
		{
			short delta = (short)HIWORD(pMsg->wParam);
			btn = delta > 0 ? keybinds::mb_wheel_up : keybinds::mb_wheel_down;
		}

		if (btn)
		{
			m_btn = btn;
			m_mouse_mods = 0;
			if (ctrl)  m_mouse_mods |= FCONTROL;
			if (shift) m_mouse_mods |= FSHIFT;
			if (alt)   m_mouse_mods |= FALT;
			update_preview();
			return TRUE;
		}
	}

	return CDialog::PreTranslateMessage(pMsg);
}

void CKeyCaptureDlg::update_preview()
{
	std::string s;
	if (m_mouse_mode)
		s = m_btn ? keybinds::format_mouse(m_btn, m_mouse_mods) : std::string("(click or roll wheel)");
	else
		s = m_vk  ? keybinds::format_shortcut(m_vk, m_key_mods)  : std::string("(press a key)");
	if (m_preview.GetSafeHwnd())
		m_preview.SetWindowText(s.c_str());
}
