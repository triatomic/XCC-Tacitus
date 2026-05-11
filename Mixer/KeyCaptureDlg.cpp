#include "stdafx.h"
#include "KeyCaptureDlg.h"
#include "keybinds.h"
#include "theme.h"

CKeyCaptureDlg* CKeyCaptureDlg::s_active = nullptr;

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
	if (m_mouse_mode)
		install_mouse_hook();
	return TRUE;
}

void CKeyCaptureDlg::OnCancel()
{
	remove_mouse_hook();
	CDialog::OnCancel();
}

void CKeyCaptureDlg::OnOK()
{
	remove_mouse_hook();
	CDialog::OnOK();
}

void CKeyCaptureDlg::install_mouse_hook()
{
	if (m_mouse_hook) return;
	s_active = this;
	m_mouse_hook = SetWindowsHookEx(WH_MOUSE, MouseHookProc, NULL, GetCurrentThreadId());
}

void CKeyCaptureDlg::remove_mouse_hook()
{
	if (m_mouse_hook)
	{
		UnhookWindowsHookEx(m_mouse_hook);
		m_mouse_hook = NULL;
	}
	if (s_active == this)
		s_active = nullptr;
}

LRESULT CALLBACK CKeyCaptureDlg::MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION && s_active)
	{
		MOUSEHOOKSTRUCT* mhs = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
		// Build a synthetic wParam for X-button / wheel. For X-button the
		// real WM has button-index in HIWORD; the hook delivers it via the
		// extra info we read with GetMessageExtraInfo on the next pump.
		// Easier: re-query via the hwnd's queue is not safe here. The
		// MOUSEHOOKSTRUCTEX flavor carries mouseData (button index for X,
		// wheel delta for MOUSEWHEEL) so cast and read it.
		MOUSEHOOKSTRUCTEX* mhex = reinterpret_cast<MOUSEHOOKSTRUCTEX*>(lParam);
		WPARAM synth_w = 0;
		switch (wParam)
		{
		case WM_XBUTTONDOWN:
			synth_w = MAKEWPARAM(0, HIWORD(mhex->mouseData));
			break;
		case WM_MOUSEWHEEL:
			synth_w = MAKEWPARAM(0, HIWORD(mhex->mouseData));
			break;
		default:
			break;
		}
		if (s_active->handle_mouse_msg((UINT)wParam, synth_w, mhs->hwnd))
			return 1; // swallow
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

bool CKeyCaptureDlg::handle_mouse_msg(UINT msg, WPARAM wParam, HWND hit)
{
	const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
	const bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

	auto is_ok_cancel_button = [&](HWND h)
	{
		if (!h) return false;
		const int id = ::GetDlgCtrlID(h);
		return id == IDOK || id == IDCANCEL;
	};
	auto in_capture_area = [&](HWND h)
	{
		if (!h) return false;
		if (h != GetSafeHwnd() && !::IsChild(GetSafeHwnd(), h))
			return false;
		return !is_ok_cancel_button(h);
	};

	BYTE btn = 0;
	bool is_left = false;
	switch (msg)
	{
	case WM_LBUTTONDOWN:
		if (in_capture_area(hit)) { btn = keybinds::mb_left; is_left = true; }
		break;
	case WM_RBUTTONDOWN:
		if (in_capture_area(hit)) btn = keybinds::mb_right;
		break;
	case WM_MBUTTONDOWN:
		if (in_capture_area(hit)) btn = keybinds::mb_middle;
		break;
	case WM_XBUTTONDOWN:
		if (in_capture_area(hit))
			btn = (HIWORD(wParam) == XBUTTON1) ? keybinds::mb_x1 : keybinds::mb_x2;
		break;
	case WM_MOUSEWHEEL:
		{
			// hook delivers wheel only when cursor is over our window's tree
			if (!in_capture_area(hit)) break;
			short delta = (short)HIWORD(wParam);
			btn = delta > 0 ? keybinds::mb_wheel_up : keybinds::mb_wheel_down;
		}
		break;
	default:
		return false;
	}
	if (!btn)
		return false;
	// Left-click on a regular control (e.g. a non-OK/Cancel button) shouldn't
	// be swallowed if it's also the user's way to focus something — but in
	// this dialog there is nothing else useful to left-click. Capture it.
	(void)is_left;
	m_btn = btn;
	m_mouse_mods = 0;
	if (ctrl)  m_mouse_mods |= FCONTROL;
	if (shift) m_mouse_mods |= FSHIFT;
	if (alt)   m_mouse_mods |= FALT;
	update_preview();
	return true;
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

	// Mouse messages are intercepted by MouseHookProc — that path catches
	// middle/X-button clicks that don't reliably surface here.

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
