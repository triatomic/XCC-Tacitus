#include "stdafx.h"
#include "SearchInPaneDlg.h"
#include "XCC MixerView.h"
#include "string_conversion.h"
#include "theme.h"
#include <algorithm>

// Mirror of CXCCMixerView's free helper; redeclared so we don't have to
// expose it in a public header.
extern string totalSize(size_t i);

CSearchInPaneDlg::CSearchInPaneDlg(CWnd* pParent)
	: ETSLayoutDialog(CSearchInPaneDlg::IDD, pParent, "search_in_pane_dlg")
{
	m_reg_key = "search_in_pane_dlg";
	m_filename = AfxGetApp()->GetProfileString(m_reg_key, "file_name");
}

void CSearchInPaneDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST, m_list);
	DDX_Text(pDX, IDC_FILENAME, m_filename);
}

BEGIN_MESSAGE_MAP(CSearchInPaneDlg, ETSLayoutDialog)
	ON_EN_CHANGE(IDC_FILENAME, OnFilterChange)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST, OnDblclkList)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST, OnColumnclickList)
	ON_WM_DESTROY()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST, OnGetdispinfoList)
	ON_WM_CTLCOLOR()
	ON_WM_SHOWWINDOW()
END_MESSAGE_MAP()

HBRUSH CSearchInPaneDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSearchInPaneDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	// Re-assert DWM dark titlebar during the show transition. See
	// CLoadPalDlg::OnShowWindow for the rationale.
	if (bShow)
		theme::apply_titlebar(GetSafeHwnd());
	ETSLayoutDialog::OnShowWindow(bShow, nStatus);
}

void CSearchInPaneDlg::set(CXCCMixerView* pane)
{
	m_pane = pane;
}

BOOL CSearchInPaneDlg::OnInitDialog()
{
	// Apply dark titlebar via DWM and suppress paint until everything is
	// laid out + themed + populated. Same flash mitigation as CLoadPalDlg
	// — without these the first paint shows light defaults for one frame
	// before apply_dialog's repaint catches up.
	theme::apply_titlebar(GetSafeHwnd());
	SetRedraw(FALSE);

	CreateRoot(VERTICAL)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_FILENAME_STATIC, NORESIZE)
			<< item(IDC_FILENAME, GREEDY)
			)
		<< item(IDC_LIST, GREEDY)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	// Theme the listview background before columns/populate so its first paint
	// is already dark (avoids a one-frame white SysListView32 erase). Idempotent.
	theme::apply_listview(m_list.GetSafeHwnd());
	m_list.InsertColumn(0, "Name");
	m_list.InsertColumn(1, "Size");
	m_list.set_size(0);

	// Theme before populate so the listview's rows arrive into a
	// dark-themed control. Order matters for the flash fix — see
	// CLoadPalDlg::OnInitDialog comment.
	theme::apply_dialog(GetSafeHwnd());
	theme::apply_column_headers(m_list.GetSafeHwnd());
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_in_pane");

	// LoadPalDlg-style: pre-populate every entry in the pane on open, then
	// narrow live as the user types in the filter box. The previous
	// type-then-Search flow forced an extra Enter/click for the common case
	// of "show me everything that contains X".
	populate_all();
	apply_filter_and_sort();
	repopulate_list();

	// Header autosize. Per-content autosize was too expensive on huge MIXes
	// (matched CSearchFileDlg's v10.5 fix); header width plus drag-to-resize
	// + persisted widths cover the same ground.
	m_list.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(1, LVSCW_AUTOSIZE_USEHEADER);

	// The OK button used to drive the search; with live filtering there's
	// nothing for it to do, so hide it. ESC / Close still works via IDCANCEL.
	if (CWnd* ok = GetDlgItem(IDOK))
		ok->ShowWindow(SW_HIDE);

	// Focus the filter so the user can start typing immediately. If a prior
	// filter was persisted we re-apply it; SetSel(0,-1) lets the next
	// keystroke replace the whole thing.
	if (CEdit* edit = static_cast<CEdit*>(GetDlgItem(IDC_FILENAME)))
	{
		edit->SetFocus();
		edit->SetSel(0, -1);
	}

	// Release redraw + flush one fully-themed paint.
	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	return FALSE; // we set focus ourselves
}

BOOL CSearchInPaneDlg::PreTranslateMessage(MSG* pMsg)
{
	// Enter in the filter edit activates the focused listview row (open it
	// in the pane) instead of dismissing the dialog. Matches the
	// fuzzy-finder convention the LoadPalDlg picker established.
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
	{
		HWND focus = ::GetFocus();
		HWND edit = GetDlgItem(IDC_FILENAME) ? GetDlgItem(IDC_FILENAME)->GetSafeHwnd() : NULL;
		HWND list = m_list.GetSafeHwnd();
		if (focus == edit || focus == list)
		{
			activate_selected();
			return TRUE;
		}
	}
	// Down/Up in the filter scroll the list selection so the user can
	// arrow-key through results without leaving the edit.
	if (pMsg->message == WM_KEYDOWN &&
		(pMsg->wParam == VK_DOWN || pMsg->wParam == VK_UP ||
		 pMsg->wParam == VK_PRIOR || pMsg->wParam == VK_NEXT))
	{
		HWND focus = ::GetFocus();
		HWND edit = GetDlgItem(IDC_FILENAME) ? GetDlgItem(IDC_FILENAME)->GetSafeHwnd() : NULL;
		if (focus == edit && m_list.GetItemCount() > 0)
		{
			int sel = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
			int next = sel;
			const int n = m_list.GetItemCount();
			if (pMsg->wParam == VK_DOWN)         next = (sel < 0) ? 0 : std::min(sel + 1, n - 1);
			else if (pMsg->wParam == VK_UP)      next = (sel < 0) ? 0 : std::max(sel - 1, 0);
			else if (pMsg->wParam == VK_NEXT)    next = (sel < 0) ? 0 : std::min(sel + 10, n - 1);
			else if (pMsg->wParam == VK_PRIOR)   next = (sel < 0) ? 0 : std::max(sel - 10, 0);
			if (next != sel)
			{
				m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				m_list.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED,
					LVIS_SELECTED | LVIS_FOCUSED);
				m_list.EnsureVisible(next, FALSE);
			}
			return TRUE;
		}
	}
	return ETSLayoutDialog::PreTranslateMessage(pMsg);
}

void CSearchInPaneDlg::populate_all()
{
	m_all.clear();
	if (!m_pane)
		return;
	const auto& idx = m_pane->t_index_list();
	m_all.reserve(idx.size());
	for (auto& i : idx)
	{
		const t_index_entry& e = i.second;
		if (e.name.empty() || e.name == ".." || e.name == "Browse...")
			continue;
		t_match m;
		m.name = e.name;
		m.id = i.first;
		// e.size_bytes is -1 for directory rows; stash 0 so the size column
		// stays blank for those instead of rendering as "-1 B".
		m.size_bytes = e.size_bytes < 0 ? 0 : e.size_bytes;
		m_all.push_back(std::move(m));
	}
}

void CSearchInPaneDlg::apply_filter_and_sort()
{
	UpdateData(TRUE);
	string filter = static_cast<string>(m_filename);

	m_visible.clear();
	// Theme > Hide Results When Search Is Empty: when on, show nothing
	// until the user types something. The default off keeps the
	// populate-everything-on-open ergonomics from v10.71.
	const bool gate_on_empty = theme::hide_empty_results() && filter.empty();
	m_visible.reserve(gate_on_empty ? 0 : m_all.size());
	if (!gate_on_empty)
	{
		for (size_t i = 0; i < m_all.size(); i++)
		{
			if (!filter.empty() && !fname_filter(m_all[i].name, filter))
				continue;
			m_visible.push_back(static_cast<int>(i));
		}
	}

	auto key_less = [this](const t_match& a, const t_match& b) {
		switch (m_sort_column)
		{
		case 1:
			if (a.size_bytes != b.size_bytes)
				return a.size_bytes < b.size_bytes;
			return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
		case 0:
		default:
			return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
		}
	};
	auto cmp = [this, &key_less](int ia, int ib) {
		const t_match& a = m_all[ia];
		const t_match& b = m_all[ib];
		return m_sort_descending ? key_less(b, a) : key_less(a, b);
	};
	std::stable_sort(m_visible.begin(), m_visible.end(), cmp);
}

void CSearchInPaneDlg::repopulate_list()
{
	// Same trick as CSearchFileDlg / CLoadPalDlg: defer paint, and when the
	// row count hasn't changed (sort-only), rewrite lParams in place
	// instead of doing a full DeleteAllItems + InsertItemData rebuild.
	m_list.SetRedraw(FALSE);
	const int rows = m_list.GetItemCount();
	const int want = static_cast<int>(m_visible.size());
	if (rows == want)
	{
		for (int i = 0; i < want; i++)
			m_list.SetItemData(i, static_cast<DWORD>(m_visible[i]));
	}
	else
	{
		m_list.DeleteAllItems();
		for (size_t i = 0; i < m_visible.size(); i++)
		{
			LVITEM lv = {};
			lv.mask = LVIF_TEXT | LVIF_PARAM;
			lv.iItem = static_cast<int>(i);
			lv.pszText = LPSTR_TEXTCALLBACK;
			lv.lParam = m_visible[i];
			m_list.InsertItem(&lv);
		}
	}
	// Keep a visible selection so Enter in the filter has something to act
	// on. If nothing was selected before (or selection went off-list after a
	// filter narrowed results), default to row 0.
	if (want > 0)
	{
		int sel = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
		if (sel < 0)
		{
			m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED,
				LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
}

void CSearchInPaneDlg::OnFilterChange()
{
	apply_filter_and_sort();
	repopulate_list();
}

void CSearchInPaneDlg::OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	int col = p->iSubItem;
	if (col == m_sort_column)
		m_sort_descending = !m_sort_descending;
	else
	{
		m_sort_column = col;
		// Size defaults to biggest-first; name defaults to A->Z.
		m_sort_descending = (col == 1);
	}
	apply_filter_and_sort();
	repopulate_list();
	*pResult = 0;
}

void CSearchInPaneDlg::activate_selected()
{
	int index = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (index < 0 || !m_pane)
		return;
	int data = m_list.GetItemData(index);
	if (data < 0 || data >= static_cast<int>(m_all.size()))
		return;
	int id = m_all[data].id;
	CListCtrl& lc = m_pane->GetListCtrl();
	LVFINDINFO lvf;
	lvf.flags = LVFI_PARAM;
	lvf.lParam = id;
	int li = lc.FindItem(&lvf, -1);
	if (li != -1)
	{
		lc.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		lc.SetItemState(li, LVIS_FOCUSED | LVIS_SELECTED,
			LVIS_FOCUSED | LVIS_SELECTED);
		lc.EnsureVisible(li, false);
	}
	EndDialog(IDCANCEL);
}

void CSearchInPaneDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	activate_selected();
	*pResult = 0;
}

void CSearchInPaneDlg::OnDestroy()
{
	ETSLayoutDialog::OnDestroy();
	AfxGetApp()->WriteProfileString(m_reg_key, "file_name", m_filename);
}

void CSearchInPaneDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFO* pDispInfo = reinterpret_cast<LV_DISPINFO*>(pNMHDR);
	int data = pDispInfo->item.lParam;
	string& buffer = m_list.get_buffer();
	if (data >= 0 && data < static_cast<int>(m_all.size()))
	{
		const t_match& m = m_all[data];
		switch (pDispInfo->item.iSubItem)
		{
		case 0: buffer = m.name; break;
		case 1: buffer = m.size_bytes ? theme::format_size(m.size_bytes) : string(); break;
		default: buffer.clear(); break;
		}
	}
	else
		buffer.clear();
	pDispInfo->item.pszText = const_cast<char*>(buffer.c_str());
	*pResult = 0;
}
