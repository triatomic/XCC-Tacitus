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
	ON_BN_CLICKED(IDOK, OnFind)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST, OnDblclkList)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST, OnColumnclickList)
	ON_WM_DESTROY()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST, OnGetdispinfoList)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

HBRUSH CSearchInPaneDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSearchInPaneDlg::set(CXCCMixerView* pane)
{
	m_pane = pane;
}

BOOL CSearchInPaneDlg::OnInitDialog()
{
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
	m_list.InsertColumn(0, "Name");
	m_list.InsertColumn(1, "Size");
	m_list.set_size(0);
	theme::apply_dialog(GetSafeHwnd());
	theme::apply_column_headers(m_list.GetSafeHwnd());
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_in_pane");
	return true;
}

bool CSearchInPaneDlg::match_one(const string& name, const string& filter)
{
	if (filter.empty())
		return true;
	return fname_filter(name, filter);
}

void CSearchInPaneDlg::OnFind()
{
	if (!UpdateData(true) || !m_pane)
		return;
	CWaitCursor wait;
	m_list.DeleteAllItems();
	m_matches.clear();
	string filter = static_cast<string>(m_filename);
	const auto& idx = m_pane->t_index_list();
	for (auto& i : idx)
	{
		const t_index_entry& e = i.second;
		if (e.name.empty() || e.name == ".." || e.name == "Browse...")
			continue;
		if (!match_one(e.name, filter))
			continue;
		t_match m;
		m.name = e.name;
		m.id = i.first;
		// e.size_bytes is -1 for directory rows; stash 0 so the size column
		// stays blank for those instead of rendering as "-1 B".
		m.size_bytes = e.size_bytes < 0 ? 0 : e.size_bytes;
		m_matches.push_back(m);
	}
	apply_sort();
	repopulate_list();
	// Fit each column to the wider of its content / header. Empty list still
	// gets header-width via AUTOSIZE_USEHEADER.
	auto fit_column = [&](int col) {
		m_list.SetColumnWidth(col, LVSCW_AUTOSIZE);
		int content_w = m_list.GetColumnWidth(col);
		m_list.SetColumnWidth(col, LVSCW_AUTOSIZE_USEHEADER);
		int header_w = m_list.GetColumnWidth(col);
		m_list.SetColumnWidth(col, max(content_w, header_w));
	};
	fit_column(0);
	fit_column(1);
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_in_pane");
}

void CSearchInPaneDlg::apply_sort()
{
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
	auto cmp = [this, &key_less](const t_match& a, const t_match& b) {
		return m_sort_descending ? key_less(b, a) : key_less(a, b);
	};
	std::stable_sort(m_matches.begin(), m_matches.end(), cmp);
}

void CSearchInPaneDlg::repopulate_list()
{
	// Same trick as CSearchFileDlg: defer paint, and when the row count
	// hasn't changed (sort-only), rewrite lParams in place instead of doing
	// a full DeleteAllItems + InsertItemData rebuild.
	m_list.SetRedraw(FALSE);
	const int rows = m_list.GetItemCount();
	const int want = static_cast<int>(m_matches.size());
	if (rows == want)
	{
		for (int i = 0; i < want; i++)
			m_list.SetItemData(i, static_cast<DWORD>(i));
	}
	else
	{
		m_list.DeleteAllItems();
		for (size_t i = 0; i < m_matches.size(); i++)
			m_list.InsertItemData(static_cast<DWORD>(i));
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
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
	apply_sort();
	repopulate_list();
	*pResult = 0;
}

void CSearchInPaneDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (index != -1 && m_pane)
	{
		int data = m_list.GetItemData(index);
		if (data >= 0 && data < static_cast<int>(m_matches.size()))
		{
			int id = m_matches[data].id;
			CListCtrl& lc = m_pane->GetListCtrl();
			LVFINDINFO lvf;
			lvf.flags = LVFI_PARAM;
			lvf.lParam = id;
			int li = lc.FindItem(&lvf, -1);
			if (li != -1)
			{
				lc.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				lc.SetItemState(li, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
				lc.EnsureVisible(li, false);
			}
			EndDialog(IDCANCEL);
		}
	}
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
	if (data >= 0 && data < static_cast<int>(m_matches.size()))
	{
		const t_match& m = m_matches[data];
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
