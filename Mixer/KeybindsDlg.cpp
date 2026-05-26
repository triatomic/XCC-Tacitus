#include "stdafx.h"
#include "KeybindsDlg.h"
#include "KeyCaptureDlg.h"
#include "theme.h"

#include <shellapi.h>
#include <shlobj.h>

CKeybindsDlg::CKeybindsDlg(CWnd* pParent)
	: ETSLayoutDialog(CKeybindsDlg::IDD, pParent, "keybinds_dlg")
{
}

void CKeybindsDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_KEYBINDS_LIST, m_list);
	DDX_Control(pDX, IDC_KEYBINDS_FILTER, m_filter);
	DDX_Control(pDX, IDC_KEYBINDS_CHANGE_KEY, m_change_key);
	DDX_Control(pDX, IDC_KEYBINDS_CHANGE_MOUSE, m_change_mouse);
	DDX_Control(pDX, IDC_KEYBINDS_CLEAR_KEY, m_clear_key);
	DDX_Control(pDX, IDC_KEYBINDS_CLEAR_MOUSE, m_clear_mouse);
	DDX_Control(pDX, IDC_KEYBINDS_RESET, m_reset);
	DDX_Control(pDX, IDC_KEYBINDS_RESET_ALL, m_reset_all);
	DDX_Control(pDX, IDC_KEYBINDS_OPEN_INI, m_open_ini);
}

BEGIN_MESSAGE_MAP(CKeybindsDlg, ETSLayoutDialog)
	ON_BN_CLICKED(IDC_KEYBINDS_CHANGE_KEY, OnChangeKey)
	ON_BN_CLICKED(IDC_KEYBINDS_CHANGE_MOUSE, OnChangeMouse)
	ON_BN_CLICKED(IDC_KEYBINDS_CLEAR_KEY, OnClearKey)
	ON_BN_CLICKED(IDC_KEYBINDS_CLEAR_MOUSE, OnClearMouse)
	ON_BN_CLICKED(IDC_KEYBINDS_RESET, OnReset)
	ON_BN_CLICKED(IDC_KEYBINDS_RESET_ALL, OnResetAll)
	ON_BN_CLICKED(IDC_KEYBINDS_OPEN_INI, OnOpenIni)
	ON_NOTIFY(NM_DBLCLK, IDC_KEYBINDS_LIST, OnDblclkList)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_KEYBINDS_LIST, OnItemchangedList)
	ON_EN_CHANGE(IDC_KEYBINDS_FILTER, OnFilterChange)
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CKeybindsDlg::OnInitDialog()
{
	CreateRoot(VERTICAL)
		<< (pane(VERTICAL, GREEDY)
			<< item(IDC_STATIC, ABSOLUTE_VERT)
			<< item(IDC_KEYBINDS_FILTER, ABSOLUTE_VERT)
			<< item(IDC_KEYBINDS_LIST, GREEDY)
			)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_KEYBINDS_CHANGE_KEY, NORESIZE)
			<< item(IDC_KEYBINDS_CHANGE_MOUSE, NORESIZE)
			<< item(IDC_KEYBINDS_CLEAR_KEY, NORESIZE)
			<< item(IDC_KEYBINDS_CLEAR_MOUSE, NORESIZE)
			<< item(IDC_KEYBINDS_RESET, NORESIZE)
			<< item(IDC_KEYBINDS_RESET_ALL, NORESIZE)
			<< item(IDC_KEYBINDS_OPEN_INI, NORESIZE)
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();

	m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	m_list.InsertColumn(0, "Command",  LVCFMT_LEFT, 220);
	m_list.InsertColumn(1, "Keyboard", LVCFMT_LEFT, 130);
	m_list.InsertColumn(2, "Mouse",    LVCFMT_LEFT, 130);
	m_list.InsertColumn(3, "Scope",    LVCFMT_LEFT, 80);

	// Cue banner in the filter edit (faded until the user types).
	::SendMessageW(m_filter.GetSafeHwnd(), EM_SETCUEBANNER, TRUE,
		reinterpret_cast<LPARAM>(L"Filter commands..."));

	m_working = keybinds::current();
	rebuild_list();
	if (!m_working.empty())
	{
		m_list.SetItemState(0, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		m_list.EnsureVisible(0, FALSE);
	}
	update_buttons();
	resize_columns();

	theme::apply_dialog(GetSafeHwnd());
	return TRUE;
}

HBRUSH CKeybindsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CKeybindsDlg::OnSize(UINT nType, int cx, int cy)
{
	ETSLayoutDialog::OnSize(nType, cx, cy);
	resize_columns();
}

void CKeybindsDlg::resize_columns()
{
	if (!m_list.GetSafeHwnd() || !m_list.GetHeaderCtrl())
		return;
	if (m_list.GetHeaderCtrl()->GetItemCount() < 4)
		return;
	CRect rc;
	m_list.GetClientRect(&rc);
	int col1 = m_list.GetColumnWidth(1);
	int col2 = m_list.GetColumnWidth(2);
	int col3 = m_list.GetColumnWidth(3);
	int rest = rc.Width() - col1 - col2 - col3;
	if (rest < 120) rest = 120;
	m_list.SetColumnWidth(0, rest);
}

// Case-insensitive substring match. Used by the filter against the command
// label, shortcut text, mouse text, and scope name (so "ctrl+f" matches by
// shortcut and "view" matches by scope).
static bool needle_in(const std::string& needle, const char* hay)
{
	if (needle.empty() || !hay) return true;
	std::string n; n.reserve(needle.size());
	for (char c : needle) n += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
	std::string h;
	for (const char* p = hay; *p; ++p)
		h += static_cast<char>(::tolower(static_cast<unsigned char>(*p)));
	return h.find(n) != std::string::npos;
}

void CKeybindsDlg::rebuild_list()
{
	CString cs;
	if (m_filter.GetSafeHwnd())
		m_filter.GetWindowText(cs);
	const std::string needle = static_cast<const char*>(cs);

	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	int row = 0;
	for (size_t i = 0; i < m_working.size(); i++)
	{
		const keybinds::Binding& b = m_working[i];
		std::string sc = keybinds::format_shortcut(b.vk, b.key_mods);
		std::string mb = keybinds::format_mouse(b.btn, b.mouse_mods);
		std::string sn = keybinds::scope_name(b.scope);
		// Match against any of label / shortcut / mouse / scope so users can
		// search by what they remember (command name OR the key combo OR scope).
		if (!needle.empty()
			&& !needle_in(needle, b.label)
			&& !needle_in(needle, sc.c_str())
			&& !needle_in(needle, mb.c_str())
			&& !needle_in(needle, sn.c_str()))
			continue;
		m_list.InsertItem(row, b.label);
		m_list.SetItemData(row, static_cast<DWORD_PTR>(i));  // real m_working idx
		m_list.SetItemText(row, 1, sc.c_str());
		m_list.SetItemText(row, 2, mb.c_str());
		m_list.SetItemText(row, 3, sn.c_str());
		row++;
	}
	m_list.SetRedraw(TRUE);
}

// `wi` is a m_working index, NOT a list row. Look up the row showing it (may be
// hidden by the filter) before rewriting columns.
void CKeybindsDlg::update_row(int wi)
{
	if (wi < 0 || wi >= (int)m_working.size())
		return;
	int row = row_for_working(wi);
	if (row < 0)
		return;
	const keybinds::Binding& b = m_working[wi];
	m_list.SetItemText(row, 1, keybinds::format_shortcut(b.vk, b.key_mods).c_str());
	m_list.SetItemText(row, 2, keybinds::format_mouse(b.btn, b.mouse_mods).c_str());
}

int CKeybindsDlg::working_index_at(int row) const
{
	if (row < 0 || row >= m_list.GetItemCount())
		return -1;
	return static_cast<int>(m_list.GetItemData(row));
}

int CKeybindsDlg::row_for_working(int wi) const
{
	for (int r = 0; r < m_list.GetItemCount(); r++)
		if (static_cast<int>(m_list.GetItemData(r)) == wi)
			return r;
	return -1;
}

int CKeybindsDlg::selected_index() const
{
	POSITION pos = m_list.GetFirstSelectedItemPosition();
	if (!pos)
		return -1;
	int row = m_list.GetNextSelectedItem(pos);
	return working_index_at(row);
}

void CKeybindsDlg::update_buttons()
{
	int i = selected_index();
	bool any = i >= 0;
	m_change_key.EnableWindow(any);
	m_change_mouse.EnableWindow(any);
	m_clear_key.EnableWindow(any);
	m_clear_mouse.EnableWindow(any);
	m_reset.EnableWindow(any);
}

void CKeybindsDlg::OnItemchangedList(NMHDR*, LRESULT* pResult)
{
	update_buttons();
	*pResult = 0;
}

void CKeybindsDlg::OnFilterChange()
{
	// Preserve the currently selected command across the rebuild so typing
	// doesn't keep stealing the selection.
	int wi = selected_index();
	rebuild_list();
	int row = (wi >= 0) ? row_for_working(wi) : -1;
	if (row < 0 && m_list.GetItemCount() > 0)
		row = 0;
	if (row >= 0)
	{
		m_list.SetItemState(row, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		m_list.EnsureVisible(row, FALSE);
	}
	update_buttons();
}

void CKeybindsDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	// Use the clicked sub-item to decide which slot to capture. Falls back to
	// keyboard for the command-label column.
	NMITEMACTIVATE* nia = (NMITEMACTIVATE*)pNMHDR;
	bool mouse = (nia && nia->iSubItem == 2);
	change_selected(mouse);
	*pResult = 0;
}

void CKeybindsDlg::OnChangeKey()   { change_selected(false); }
void CKeybindsDlg::OnChangeMouse() { change_selected(true); }

void CKeybindsDlg::change_selected(bool mouse)
{
	int i = selected_index();
	if (i < 0) return;
	CKeyCaptureDlg dlg(mouse, this);
	if (dlg.DoModal() != IDOK)
		return;
	if (mouse)
	{
		if (dlg.m_btn == 0) return;
		m_working[i].btn = dlg.m_btn;
		m_working[i].mouse_mods = dlg.m_mouse_mods;
	}
	else
	{
		if (dlg.m_vk == 0) return;
		m_working[i].vk = dlg.m_vk;
		m_working[i].key_mods = dlg.m_key_mods;
	}
	update_row(i);
}

void CKeybindsDlg::OnClearKey()
{
	int i = selected_index();
	if (i < 0) return;
	m_working[i].vk = 0;
	m_working[i].key_mods = 0;
	update_row(i);
}

void CKeybindsDlg::OnClearMouse()
{
	int i = selected_index();
	if (i < 0) return;
	m_working[i].btn = keybinds::mb_none;
	m_working[i].mouse_mods = 0;
	update_row(i);
}

void CKeybindsDlg::OnReset()
{
	int i = selected_index();
	if (i < 0) return;
	const auto& defs = keybinds::defaults();
	if (i >= (int)defs.size()) return;
	m_working[i] = defs[i];
	update_row(i);
}

void CKeybindsDlg::OnOpenIni()
{
	// Open the INI's parent folder in Explorer with the file pre-selected.
	// AfxGetApp()->m_pszProfileName is the absolute path we set in
	// CXCCMixerApp::InitInstance. If for any reason it's null or a bare
	// name (registry mode), fall back to opening %APPDATA%\XCC\Mixer.
	const char* ini = AfxGetApp()->m_pszProfileName;
	if (ini && *ini && strchr(ini, '\\'))
	{
		std::string args = "/select,\"";
		args += ini;
		args += "\"";
		ShellExecuteA(GetSafeHwnd(), "open", "explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
		return;
	}
	char appdata[MAX_PATH] = {0};
	::SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
	std::string dir = std::string(appdata) + "\\XCC\\Mixer";
	ShellExecuteA(GetSafeHwnd(), "open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void CKeybindsDlg::OnResetAll()
{
	if (AfxMessageBox("Reset all keybindings to defaults?", MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	m_working = keybinds::defaults();
	rebuild_list();
	update_buttons();
}

bool CKeybindsDlg::has_conflict(int& a_out, int& b_out, bool& is_mouse_out) const
{
	for (size_t i = 0; i < m_working.size(); i++)
	{
		const auto& bi = m_working[i];
		for (size_t j = i + 1; j < m_working.size(); j++)
		{
			const auto& bj = m_working[j];
			if (bi.scope != bj.scope) continue;
			if (bi.vk != 0 && bi.vk == bj.vk && bi.key_mods == bj.key_mods)
			{
				a_out = (int)i; b_out = (int)j; is_mouse_out = false;
				return true;
			}
			if (bi.btn != keybinds::mb_none && bi.btn == bj.btn && bi.mouse_mods == bj.mouse_mods)
			{
				a_out = (int)i; b_out = (int)j; is_mouse_out = true;
				return true;
			}
		}
	}
	return false;
}

void CKeybindsDlg::OnOK()
{
	int a = -1, b = -1;
	bool is_mouse = false;
	if (has_conflict(a, b, is_mouse))
	{
		std::string msg = is_mouse ? "Mouse conflict: \"" : "Key conflict: \"";
		msg += m_working[a].label;
		msg += "\" and \"";
		msg += m_working[b].label;
		msg += "\" share the same ";
		msg += is_mouse ? "mouse gesture" : "key combination";
		msg += " in the same scope.\n\nPlease change one before saving.";
		AfxMessageBox(msg.c_str(), MB_OK | MB_ICONWARNING);
		m_list.SetItemState(a, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		m_list.EnsureVisible(a, FALSE);
		return;
	}
	for (size_t i = 0; i < m_working.size(); i++)
	{
		keybinds::set_key((int)i, m_working[i].vk, m_working[i].key_mods);
		keybinds::set_mouse((int)i, m_working[i].btn, m_working[i].mouse_mods);
	}
	keybinds::save_to_registry();
	ETSLayoutDialog::OnOK();
}
