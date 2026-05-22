#include "stdafx.h"
#include "LoadPalDlg.h"
#include "MainFrm.h"
#include "XCCFileView.h"
#include "theme.h"

#include <fname.h>
#include <mix_file.h>
#include <pal_file.h>
#include <virtual_binary.h>

#include <algorithm>
#include <cctype>

using std::string;

namespace
{
	// Match-score column labels. Kept short so the column doesn't dominate
	// the row width on narrow listviews.
	const char* kMatchLabel[3] = { "exact", "prefix", "" };

	static string lower(const string& s)
	{
		string r = s;
		for (auto& c : r)
			c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
		return r;
	}

	// Score a candidate pal stem against the current file's stem. Mirrors
	// the rule in XCCFileView::OnLoadPal (pre-refactor) and in
	// try_auto_load_paired_pal: exact stem is best, 4-char prefix is the
	// Westwood cluster heuristic (flashmuz <-> flashbeam, tibtree <-> tibsnow).
	static int score_pal(const string& file_base_lc, const string& pal_name_with_ext)
	{
		if (file_base_lc.empty() || pal_name_with_ext.size() < 5)
			return 2;
		string lc = lower(pal_name_with_ext);
		if (lc.compare(lc.size() - 4, 4, ".pal") != 0)
			return 2;
		string pal_base = lc.substr(0, lc.size() - 4);
		if (pal_base == file_base_lc)
			return 0;
		if (file_base_lc.size() >= 4 && pal_base.size() >= 4 &&
			file_base_lc.compare(0, 4, pal_base, 0, 4) == 0)
			return 1;
		return 2;
	}
}

CLoadPalDlg::CLoadPalDlg(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(CLoadPalDlg::IDD, pParent, "load_pal_dlg")
{
	m_sort_column = AfxGetApp()->GetProfileInt("load_pal_dlg", "sort_col", -1);
	if (m_sort_column < -1 || m_sort_column > 2)
		m_sort_column = -1;
	m_sort_descending = AfxGetApp()->GetProfileInt("load_pal_dlg", "sort_desc", 0) != 0;
}

void CLoadPalDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LOADPAL_LIST, m_list);
	DDX_Control(pDX, IDC_LOADPAL_FILTER, m_filter_edit);
	DDX_Text(pDX, IDC_LOADPAL_FILTER, m_filter);
}

BEGIN_MESSAGE_MAP(CLoadPalDlg, ETSLayoutDialog)
	ON_EN_CHANGE(IDC_LOADPAL_FILTER, OnFilterChange)
	ON_BN_CLICKED(IDC_LOADPAL_BROWSE, OnBrowse)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LOADPAL_LIST, OnItemchangedList)
	ON_NOTIFY(NM_DBLCLK, IDC_LOADPAL_LIST, OnDblclkList)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LOADPAL_LIST, OnColumnclickList)
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LOADPAL_LIST, OnGetdispinfoList)
	ON_WM_DESTROY()
	ON_WM_CTLCOLOR()
	ON_WM_SHOWWINDOW()
END_MESSAGE_MAP()

HBRUSH CLoadPalDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(),
		pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CLoadPalDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	// Final flash insurance: re-apply the DWM dark titlebar attribute at
	// the moment the window is about to be shown. Even with the
	// OnInitDialog apply_titlebar + SWP_FRAMECHANGED, some Windows builds
	// still composed one light frame before the attribute took effect.
	// Setting it again here gives DWM the value during the very show
	// transition. Cheap (one DwmSetWindowAttribute call) and idempotent.
	if (bShow)
		theme::apply_titlebar(GetSafeHwnd());
	ETSLayoutDialog::OnShowWindow(bShow, nStatus);
}

void CLoadPalDlg::set(CMainFrame* main_frame, CXCCFileView* view,
	Cmix_file* source_mix, const string& fname)
{
	m_main_frame = main_frame;
	m_view = view;
	m_source_mix = source_mix;
	Cfname fn(fname);
	m_file_base = lower(fn.get_ftitle());
	m_file_display = fn.get_fname();
}

BOOL CLoadPalDlg::OnInitDialog()
{
	// Paint the dark titlebar via DWM BEFORE anything else paints. DWM
	// immersive dark mode latches per-frame; setting it after the first
	// paint produces a light-titlebar flash. Same reason child theming
	// happens under SetRedraw(FALSE) further down. See feedback on
	// dialog flashbang.
	theme::apply_titlebar(GetSafeHwnd());
	// Suppress all paint while we build out the dialog (layout + child
	// theming + listview populate). Final SetRedraw(TRUE) + RedrawWindow
	// at the bottom flushes one cohesive themed paint instead of the
	// light->dark flash the previous (theme-after-populate) order
	// produced.
	SetRedraw(FALSE);

	CreateRoot(VERTICAL)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_LOADPAL_FILTER_LABEL, NORESIZE)
			<< item(IDC_LOADPAL_FILTER, GREEDY)
			)
		<< item(IDC_LOADPAL_LIST, GREEDY)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_LOADPAL_BROWSE, NORESIZE)
			<< item(IDOK, NORESIZE | ALIGN_RIGHT)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();

	m_list.InsertColumn(0, "Name");
	m_list.InsertColumn(1, "Source");
	m_list.InsertColumn(2, "Match");
	m_list.set_size(0);

	// Theme the dialog (and listview header) BEFORE the listview is
	// populated. apply_dialog walks children and runs uxtheme calls that
	// override class-default colors; doing it after a populated paint
	// produced a visible light-mode flash. With SetRedraw(FALSE) above,
	// neither this nor the populate trigger an intermediate paint.
	theme::apply_dialog(GetSafeHwnd());
	theme::apply_column_headers(m_list.GetSafeHwnd());
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "load_pal_dlg_v1");

	// Snapshot the active palette index so OnCancel can revert. Captured
	// before build_rows() so even if construction somehow flipped state
	// (it shouldn't), we have the user's pre-dialog selection.
	if (m_main_frame)
		m_original_palette_i = m_main_frame->get_palette();

	build_rows();
	apply_filter_and_sort();
	repopulate_list();

	// Initial selection: row 0 (best match by default ranking, or first
	// alphabetical if no candidates score < 2). The LVN_ITEMCHANGED will
	// fire on this and trigger live preview — wanted behavior, but only if
	// the user opened the dialog with intent to switch. Skip the auto-fire
	// by setting the guard before SetItemState; the user's first manual
	// click will then start previewing.
	if (!m_visible.empty())
	{
		m_in_live_apply = true;
		m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED);
		m_in_live_apply = false;
	}

	m_list.SetColumnWidth(0, 180);
	m_list.SetColumnWidth(1, 220);
	m_list.SetColumnWidth(2, 60);

	// Show which file the palette will apply to. The basename alone keeps
	// the title bar short (full paths in MIX entries get long fast); empty
	// fallback leaves the resource default ("Load palette") untouched.
	if (!m_file_display.empty())
	{
		std::string title = "Load palette for " + m_file_display;
		SetWindowText(title.c_str());
	}

	// Focus the filter edit so the user can start typing immediately.
	m_filter_edit.SetFocus();

	// Release the redraw lock and force a single flush so DoModal's
	// first show paints fully themed instead of producing a light->dark
	// flash. RDW_ALLCHILDREN reaches the listview / edit / buttons in
	// one pass.
	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	return FALSE;
}

void CLoadPalDlg::build_rows()
{
	m_rows.clear();

	// Track MIX entries we've covered via m_pal_list (by case-insensitive
	// name + identical bytes) so we don't list a stock palette twice when
	// it's both in m_pal_list and reachable via m_source_mix.
	std::vector<int> covered_mix_ids;

	if (m_main_frame)
	{
		const auto& pl = m_main_frame->pal_list_mut();
		for (size_t i = 0; i < pl.size(); i++)
		{
			pal_row r;
			r.name = pl[i].name;
			r.source = compute_source_label(static_cast<int>(i));
			r.match_score = score_pal(m_file_base, r.name);
			r.kind = pal_row::kind_pal_list;
			r.pal_list_idx = static_cast<int>(i);
			m_rows.push_back(std::move(r));
		}
	}

	// Add MIX entries not already represented. Cheap pre-check by name
	// equality; only fall back to get_vdata + memcmp on a collision.
	if (m_source_mix)
	{
		const int n = m_source_mix->get_c_files();
		for (int i = 0; i < n; i++)
		{
			const int id = m_source_mix->get_id(i);
			string name = m_source_mix->get_name(id);
			if (name.size() < 5)
				continue;
			string lc = lower(name);
			if (lc.compare(lc.size() - 4, 4, ".pal") != 0)
				continue;

			// Dedup: skip if any pal_list row has identical bytes AND
			// matches case-insensitively on display name. Different MIXes
			// can ship same-named PALs with different colors, so the byte
			// check matters.
			bool dup = false;
			if (m_main_frame)
			{
				const auto& pl = m_main_frame->pal_list_mut();
				Cvirtual_binary buf;
				bool buf_loaded = false;
				for (size_t k = 0; k < pl.size(); k++)
				{
					if (_stricmp(pl[k].name.c_str(), name.c_str()) != 0)
						continue;
					if (!buf_loaded)
					{
						buf = m_source_mix->get_vdata(id);
						buf_loaded = true;
						if (buf.size() < sizeof(t_palette))
							break;
					}
					if (memcmp(pl[k].palette, buf.data(),
						sizeof(t_palette)) == 0)
					{
						dup = true;
						break;
					}
				}
			}
			if (dup)
				continue;

			pal_row r;
			r.name = name;
			r.source = "MIX (current)";
			r.match_score = score_pal(m_file_base, name);
			r.kind = pal_row::kind_mix_entry;
			r.mix_entry_id = id;
			m_rows.push_back(std::move(r));
		}
	}
}

std::string CLoadPalDlg::compute_source_label(int pal_list_idx) const
{
	if (!m_main_frame)
		return std::string();
	const auto& pl = m_main_frame->pal_list_mut();
	if (pal_list_idx < 0 || pal_list_idx >= static_cast<int>(pl.size()))
		return std::string();
	auto& pml = m_main_frame->pal_map_list_mut();
	int parent = pl[pal_list_idx].parent;
	// Walk up at most 2 levels for a compact "root / child" label. Avoids
	// "tree / level1 / level2 / level3 / leaf" path noise common in deep
	// PalPaths imports.
	std::vector<std::string> chain;
	int hops = 0;
	while (parent != -1 && hops < 3)
	{
		auto it = pml.find(parent);
		if (it == pml.end())
			break;
		chain.push_back(it->second.name);
		parent = it->second.parent;
		hops++;
	}
	if (chain.empty())
		return std::string();
	std::string out;
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
	{
		if (!out.empty()) out += " / ";
		out += *it;
	}
	return out;
}

void CLoadPalDlg::apply_filter_and_sort()
{
	m_visible.clear();

	std::string flt;
	{
		CString s = m_filter;
		s.MakeLower();
		flt = static_cast<const char*>(s);
	}

	// Theme > Hide Results When Search Is Empty: hide the whole list while
	// the filter is empty. Default off keeps the ranked-by-relevance
	// browse-all default the dialog shipped with.
	const bool gate_on_empty = theme::hide_empty_results() && flt.empty();
	if (!gate_on_empty)
	{
		m_visible.reserve(m_rows.size());
		for (size_t i = 0; i < m_rows.size(); i++)
		{
			if (!flt.empty())
			{
				std::string name_lc = lower(m_rows[i].name);
				if (name_lc.find(flt) == std::string::npos)
					continue;
			}
			m_visible.push_back(static_cast<int>(i));
		}
	}

	auto by_default = [this](int ia, int ib) {
		const pal_row& a = m_rows[ia];
		const pal_row& b = m_rows[ib];
		if (a.match_score != b.match_score)
			return a.match_score < b.match_score;
		return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
	};
	auto by_col = [this](int ia, int ib) {
		const pal_row& a = m_rows[ia];
		const pal_row& b = m_rows[ib];
		int c = 0;
		switch (m_sort_column)
		{
		case 0: c = _stricmp(a.name.c_str(), b.name.c_str()); break;
		case 1: c = _stricmp(a.source.c_str(), b.source.c_str()); break;
		case 2: c = (a.match_score - b.match_score); break;
		default: c = 0; break;
		}
		if (c == 0)
			c = _stricmp(a.name.c_str(), b.name.c_str());
		return m_sort_descending ? (c > 0) : (c < 0);
	};
	if (m_sort_column < 0)
		std::stable_sort(m_visible.begin(), m_visible.end(), by_default);
	else
		std::stable_sort(m_visible.begin(), m_visible.end(), by_col);
}

void CLoadPalDlg::repopulate_list()
{
	m_in_live_apply = true; // suppress LVN_ITEMCHANGED preview during rebuild
	m_list.SetRedraw(FALSE);
	const int have = m_list.GetItemCount();
	const int want = static_cast<int>(m_visible.size());
	if (have == want)
	{
		for (int i = 0; i < want; i++)
			m_list.SetItemData(i, m_visible[i]);
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
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
	m_in_live_apply = false;
}

void CLoadPalDlg::OnFilterChange()
{
	UpdateData(TRUE);
	apply_filter_and_sort();
	repopulate_list();
	// Re-select row 0 silently (no live preview) — gives the user a visible
	// caret to act on with Enter / arrow keys after typing.
	if (!m_visible.empty())
	{
		m_in_live_apply = true;
		m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED);
		m_in_live_apply = false;
	}
}

void CLoadPalDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
	int row_idx = static_cast<int>(pDispInfo->item.lParam);
	if (row_idx < 0 || row_idx >= static_cast<int>(m_rows.size()))
	{
		pDispInfo->item.pszText = const_cast<char*>("");
		*pResult = 0;
		return;
	}
	const pal_row& r = m_rows[row_idx];
	std::string& buffer = m_list.get_buffer();
	switch (pDispInfo->item.iSubItem)
	{
	case 0: buffer = r.name; break;
	case 1: buffer = r.source; break;
	case 2:
		buffer = (r.match_score >= 0 && r.match_score < 3)
			? kMatchLabel[r.match_score] : std::string();
		break;
	default: buffer.clear(); break;
	}
	pDispInfo->item.pszText = const_cast<char*>(buffer.c_str());
	*pResult = 0;
}

void CLoadPalDlg::OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	*pResult = 0;
	if (m_in_live_apply)
		return;
	if (!(p->uChanged & LVIF_STATE))
		return;
	// Fire on the gained-selection transition only.
	if (!((p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED)))
		return;
	apply_row_live(p->iItem);
}

void CLoadPalDlg::apply_row_live(int row_visible)
{
	if (row_visible < 0 || row_visible >= static_cast<int>(m_visible.size()))
		return;
	const pal_row& r = m_rows[m_visible[row_visible]];
	if (r.kind == pal_row::kind_pal_list)
	{
		if (!m_main_frame || r.pal_list_idx < 0)
			return;
		m_main_frame->set_palette(r.pal_list_idx);
	}
	else if (r.kind == pal_row::kind_mix_entry)
	{
		apply_mix_entry(r.mix_entry_id, r.name);
	}
}

bool CLoadPalDlg::apply_mix_entry(int mix_entry_id, const std::string& name)
{
	if (!m_view || !m_source_mix)
		return false;
	Cvirtual_binary data = m_source_mix->get_vdata(mix_entry_id);
	if (data.size() == 0)
		return false;
	return m_view->apply_loaded_pal(data, name);
}

int CLoadPalDlg::find_pal_list_idx_by_name(const std::string& name) const
{
	if (!m_main_frame)
		return -1;
	const auto& pl = m_main_frame->pal_list_mut();
	// Walk in reverse so the most-recently-appended "Loaded" entry wins
	// when names collide — matches the behavior of apply_loaded_pal which
	// always appends/dedups under the synthetic Loaded root.
	for (int i = static_cast<int>(pl.size()) - 1; i >= 0; i--)
	{
		if (_stricmp(pl[i].name.c_str(), name.c_str()) == 0)
			return i;
	}
	return -1;
}

void CLoadPalDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	int sel = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (sel < 0)
		return;
	apply_row_live(sel);
	EndDialog(IDOK);
}

void CLoadPalDlg::OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	int col = p->iSubItem;
	if (col == m_sort_column)
		m_sort_descending = !m_sort_descending;
	else
	{
		m_sort_column = col;
		m_sort_descending = false;
	}
	apply_filter_and_sort();
	repopulate_list();
	*pResult = 0;
}

void CLoadPalDlg::OnBrowse()
{
	CFileDialog dlg(TRUE, "pal", NULL,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
		"PAL files (*.pal)|*.pal|All files (*.*)|*.*||", this);
	if (dlg.DoModal() != IDOK)
		return;
	const string path = static_cast<const char*>(dlg.GetPathName());
	Cvirtual_binary data;
	if (data.load(path) || data.size() == 0)
	{
		AfxMessageBox("Could not read the selected PAL file.", MB_ICONERROR);
		return;
	}
	string label = static_cast<Cfname>(path).get_fname();
	if (!m_view || !m_view->apply_loaded_pal(data, label))
	{
		AfxMessageBox("File is not a valid PAL.", MB_ICONERROR);
		return;
	}
	// apply_loaded_pal appended (or re-selected) under the Loaded root.
	// Refresh the list so the new entry is visible + selected.
	build_rows();
	apply_filter_and_sort();
	repopulate_list();
	// Hunt for the row we just added and select it. find_pal_list_idx_by_name
	// returns the m_pal_list index; we need to find the matching m_rows entry.
	int target_pal_list_idx = find_pal_list_idx_by_name(label);
	if (target_pal_list_idx >= 0)
	{
		for (size_t i = 0; i < m_visible.size(); i++)
		{
			const pal_row& r = m_rows[m_visible[i]];
			if (r.kind == pal_row::kind_pal_list &&
				r.pal_list_idx == target_pal_list_idx)
			{
				m_in_live_apply = true; // palette already applied by apply_loaded_pal
				m_list.SetItemState(static_cast<int>(i),
					LVIS_SELECTED | LVIS_FOCUSED,
					LVIS_SELECTED | LVIS_FOCUSED);
				m_list.EnsureVisible(static_cast<int>(i), FALSE);
				m_in_live_apply = false;
				break;
			}
		}
	}
}

void CLoadPalDlg::OnOK()
{
	// The selected palette (if any) is already live; just close.
	EndDialog(IDOK);
}

void CLoadPalDlg::OnCancel()
{
	// Revert to whatever palette was active when the dialog opened. If the
	// original was -1 (default), set_palette(-1) is a no-op — guard so we
	// don't index past m_pal_list. Going through CMainFrame::set_palette
	// also fires the view's notify_palette_changed() in player mode so the
	// BGRA cache rebuilds.
	if (m_main_frame && m_main_frame->get_palette() != m_original_palette_i)
	{
		if (m_original_palette_i >= 0)
			m_main_frame->set_palette(m_original_palette_i);
		// If original was -1 there's currently no public way to clear back
		// to "default" through set_palette (signature takes int id). Leave
		// the current selection in place — best-effort revert.
	}
	EndDialog(IDCANCEL);
}

void CLoadPalDlg::OnDestroy()
{
	ETSLayoutDialog::OnDestroy();
	AfxGetApp()->WriteProfileInt("load_pal_dlg", "sort_col", m_sort_column);
	AfxGetApp()->WriteProfileInt("load_pal_dlg", "sort_desc",
		m_sort_descending ? 1 : 0);
}
