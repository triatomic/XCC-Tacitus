#include "stdafx.h"
#include "MainFrm.h"
#include "SearchFileDlg.h"
#include "string_conversion.h"
#include "theme.h"
#include <algorithm>
#include <cc_file.h>
#include <fname.h>
#include <id_log.h>

// Mirror of CXCCMixerView's free helper; redeclared here so we don't have to
// expose it in a public header.
extern string totalSize(size_t i);

CSearchFileDlg::CSearchFileDlg(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(CSearchFileDlg::IDD, pParent, "find_file_dlg")
{
	m_reg_key = "find_file_dlg";
	//{{AFX_DATA_INIT(CSearchFileDlg)
	//}}AFX_DATA_INIT
	m_filename = AfxGetApp()->GetProfileString(m_reg_key, "file_name");
	m_include_game_mixes = AfxGetApp()->GetProfileInt(m_reg_key, "include_game_mixes", 1) != 0;
}

void CSearchFileDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CSearchFileDlg)
	DDX_Control(pDX, IDC_LIST, m_list);
	DDX_Text(pDX, IDC_FILENAME, m_filename);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CSearchFileDlg, ETSLayoutDialog)
	//{{AFX_MSG_MAP(CSearchFileDlg)
	ON_BN_CLICKED(IDOK, OnFind)
	ON_BN_CLICKED(IDC_EXTRACT, OnExtract)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST, OnDblclkList)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST, OnColumnclickList)
	ON_WM_DESTROY()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST, OnGetdispinfoList)
	ON_WM_CTLCOLOR()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

HBRUSH CSearchFileDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSearchFileDlg::set(CMainFrame* main_frame, bool prefer_right)
{
	m_main_frame = main_frame;
	m_prefer_right = prefer_right;
}

BOOL CSearchFileDlg::OnInitDialog() 
{
	CreateRoot(VERTICAL)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_FILENAME_STATIC, NORESIZE)
			<< item(IDC_FILENAME, GREEDY)
			)
		<< item(IDC_LIST, GREEDY)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< (pane(VERTICAL, ABSOLUTE_HORZ)
				<< item(IDC_PRESERVE_STRUCTURE, NORESIZE)
				<< item(IDC_INCLUDE_GAME_MIXES, NORESIZE)
				)
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDC_EXTRACT, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	CheckDlgButton(IDC_INCLUDE_GAME_MIXES, m_include_game_mixes ? BST_CHECKED : BST_UNCHECKED);
	// Two-column layout: Source = the MIX chain ("top.mix (1) - sub.mix"),
	// File = the leaf entry inside it. Split is done at render time in
	// OnGetdispinfoList; t_map_entry::name continues to hold the full
	// "<chain> - <file>" string for back-compat with code that reads it.
	// Source is intentionally not a column — promoted to LVS group header
	// (one group per source chain) so rows from the same MIX cluster under
	// a collapsible heading. Group title bar is rendered by the system in
	// its own colors (light blue band even in dark mode); not recolorable
	// via NM_CUSTOMDRAW. Accepted as-is.
	m_list.InsertColumn(0, "File");
	m_list.InsertColumn(1, "Size");
	m_list.InsertColumn(2, "Path");
	m_list.set_size(0);
	m_list.EnableGroupView(TRUE);
	theme::apply_dialog(GetSafeHwnd());
	return true;
}

void CSearchFileDlg::find(Cmix_file& f, string file_name, string mix_name, int mix_id, int sub_mix_id, const string& top_mix_path, bool predefined)
{
	for (int i = 0; i < f.get_c_files(); i++)
	{
		const int id = f.get_id(i);
		string name = f.get_name(id);
		const long long sz = f.get_size(id);
		if (name.empty())
		{
			name = nh(8, id);
			if (Cmix_file::get_id(f.get_game(), file_name) == id)
				add(mix_name + " - " + name, mix_id, id, sub_mix_id, top_mix_path, sz, predefined);
		}
		else if (fname_filter(name, file_name))
			add(mix_name + " - " + name, mix_id, id, sub_mix_id, top_mix_path, sz, predefined);
		if (f.get_type(id) == ft_mix)
		{
			Cmix_file fg;
			if (!fg.open(id, f))
			{
				find(fg, file_name, mix_name + " - " + name, mix_id, i, top_mix_path, predefined);
			}
		}
	}
	for (auto& i : m_main_frame->mix_map_list())
	{
		if (i.second.parent != mix_id)
			continue;
		Cmix_file g;
		if (!g.open(i.second.id, f))
			find(g, file_name, mix_name + " - " + (i.second.name.empty() ? nh(8, i.second.id) : i.second.name), i.first, -1, top_mix_path, predefined);
	}
}

// Walk every predefined-game MIX registered by CMainFrame::find_mixs (i.e.
// every t_mix_map_list entry whose fname names an on-disk file — the top-
// level RA2/TS/etc. archives). For each such root, the existing
// find(Cmix_file&,...) overload recurses into nested MIXes via mix_map_list
// using parent==mix_id, so we only kick off the roots here. predefined=true
// is propagated so add() tags the rows; OnDblclkList then routes them
// through the iterator-based open_location_mix overload (which walks
// parents back to the on-disk fname) instead of the pane t_index_list path.
void CSearchFileDlg::find_predefined()
{
	for (auto& i : m_main_frame->mix_map_list())
	{
		if (i.second.fname.empty())
			continue;
		Cmix_file f;
		if (f.open(i.second.fname))
			continue;
		const auto& parent = find_ref(m_main_frame->mix_map_list(), i.second.parent);
		find(f, get_filename(), parent.name + " - " + i.second.name, i.first, -1, i.second.fname, true);
	}
}

void CSearchFileDlg::find(const map<int, t_index_entry>& t_map, const string& post, const string& dir)
{
	for (auto& i : t_map)
	{
		if (i.second.name.empty())
			continue;
		string fname = dir.rfind('\\') == string::npos ? (dir + '\\' + i.second.name) : (dir + i.second.name);
		Cmix_file f;
		if (!f.open(fname))
		{
			find(f, get_filename(), i.second.name + post, i.first, -1, fname);
		}
		else if (i.second.ft == ft_mix)
		{
			Cmix_file_rd f_rd;
			if (!f_rd.open(fname))
			{
				find(f_rd, get_filename(), i.second.name + post, i.first, -1, fname);
			}
		}
	}
}

// e.name format is "<chain> - <leaf>". Returns the chain (Source) part.
static std::string source_part(const std::string& full)
{
	auto sep = full.rfind(" - ");
	return (sep == std::string::npos) ? std::string() : full.substr(0, sep);
}
static std::string file_part(const std::string& full)
{
	auto sep = full.rfind(" - ");
	return (sep == std::string::npos) ? full : full.substr(sep + 3);
}

void CSearchFileDlg::OnFind()
{
	if (UpdateData(true))
	{
		CWaitCursor wait;
		m_list.DeleteAllItems();
		m_map.clear();
		// "" suffix: when Source was a column, " (1)" / " (2)" disambiguated
		// which pane the row came from. With group headers + a Path column
		// that's redundant noise. m_sepindex still partitions left vs right
		// by m_map insertion order so open_mix() routes correctly.
		m_include_game_mixes = IsDlgButtonChecked(IDC_INCLUDE_GAME_MIXES) == BST_CHECKED;
		find(m_main_frame->left_mix_pane()->t_index_list(), "", m_main_frame->left_mix_pane()->current_dir());
		m_sepindex = m_map.size();
		find(m_main_frame->right_mix_pane()->t_index_list(), "", m_main_frame->right_mix_pane()->current_dir());
		if (m_include_game_mixes)
			find_predefined();

		// Assign one LVGROUP per unique source chain. Group ids are
		// first-seen-first so group ordering matches search-encounter order.
		m_list.RemoveAllGroups();
		std::map<std::string, int> chain_to_gid;
		int next_gid = 0;
		for (auto& i : m_map)
		{
			t_map_entry& e = i.second;
			std::string chain = source_part(e.name);
			auto it = chain_to_gid.find(chain);
			if (it == chain_to_gid.end())
			{
				int gid = next_gid++;
				chain_to_gid.emplace(chain, gid);
				e.group_id = gid;
				LVGROUP g = {};
				g.cbSize = sizeof(g);
				g.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
				g.iGroupId = gid;
				g.state = LVGS_COLLAPSIBLE | LVGS_NORMAL;
				g.stateMask = LVGS_COLLAPSIBLE | LVGS_NORMAL | LVGS_COLLAPSED;
				std::wstring whdr(chain.begin(), chain.end());
				g.pszHeader = const_cast<LPWSTR>(whdr.c_str());
				g.cchHeader = static_cast<int>(whdr.size());
				m_list.InsertGroup(-1, &g);
			}
			else
			{
				e.group_id = it->second;
			}
		}

		m_order.clear();
		m_order.reserve(m_map.size());
		for (auto& i : m_map)
			m_order.push_back(i.first);
		apply_sort();
		repopulate_list();
		// Size each column to max(content, header). When the list is empty
		// LVSCW_AUTOSIZE collapses to 0, so we always fall through to
		// AUTOSIZE_USEHEADER as the floor.
		auto fit_column = [&](int col) {
			m_list.SetColumnWidth(col, LVSCW_AUTOSIZE);
			int content_w = m_list.GetColumnWidth(col);
			m_list.SetColumnWidth(col, LVSCW_AUTOSIZE_USEHEADER);
			int header_w = m_list.GetColumnWidth(col);
			m_list.SetColumnWidth(col, max(content_w, header_w));
		};
		fit_column(0);
		fit_column(1);
		fit_column(2);
	}
}

void CSearchFileDlg::apply_sort()
{
	// Group view sorts items within each group; group ordering itself is
	// fixed by group_id (search-encounter order). Primary key is group_id
	// so same-group rows stay clustered, then user-selected column.
	auto cmp = [this](int ka, int kb) {
		const t_map_entry& a = m_map.find(ka)->second;
		const t_map_entry& b = m_map.find(kb)->second;
		if (a.group_id != b.group_id)
			return a.group_id < b.group_id;
		bool less = false;
		switch (m_sort_column)
		{
		case 0: // File
			less = _stricmp(file_part(a.name).c_str(), file_part(b.name).c_str()) < 0;
			break;
		case 1: // Size
			less = a.size_bytes < b.size_bytes;
			if (a.size_bytes == b.size_bytes)
				less = _stricmp(file_part(a.name).c_str(), file_part(b.name).c_str()) < 0;
			break;
		case 2: // Path
			less = _stricmp(a.top_mix_path.c_str(), b.top_mix_path.c_str()) < 0;
			if (!less && _stricmp(a.top_mix_path.c_str(), b.top_mix_path.c_str()) == 0)
				less = _stricmp(file_part(a.name).c_str(), file_part(b.name).c_str()) < 0;
			break;
		}
		return m_sort_descending ? !less : less;
	};
	std::stable_sort(m_order.begin(), m_order.end(), cmp);
}

void CSearchFileDlg::repopulate_list()
{
	// SetRedraw(FALSE) defers the listview paint until we're done; without it
	// every InsertItemData / SetItemData triggered an incremental repaint.
	m_list.SetRedraw(FALSE);
	const int rows = m_list.GetItemCount();
	const int want = static_cast<int>(m_order.size());
	if (rows == want)
	{
		// Same row count as before — sort just reordered things. Rewrite the
		// lParam (and iGroupId) on each existing row instead of delete-and-
		// reinsert; no rows added/removed, no per-row WM_NOTIFY churn,
		// selection persists.
		for (int i = 0; i < want; i++)
		{
			int key = m_order[i];
			m_list.SetItemData(i, key);
			LVITEM lv = {};
			lv.mask = LVIF_GROUPID;
			lv.iItem = i;
			lv.iGroupId = m_map.find(key)->second.group_id;
			m_list.SetItem(&lv);
		}
	}
	else
	{
		// First populate after a search, or row count changed for any other
		// reason: rebuild from scratch.
		m_list.DeleteAllItems();
		for (size_t i = 0; i < m_order.size(); i++)
		{
			int key = m_order[i];
			LVITEM lv = {};
			lv.mask = LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID;
			lv.iItem = static_cast<int>(i);
			lv.pszText = LPSTR_TEXTCALLBACK;
			lv.lParam = key;
			lv.iGroupId = m_map.find(key)->second.group_id;
			m_list.InsertItem(&lv);
		}
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
}

void CSearchFileDlg::OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	int col = p->iSubItem;
	if (col == m_sort_column)
		m_sort_descending = !m_sort_descending;
	else
	{
		m_sort_column = col;
		// Size defaults to biggest-first; text columns default to A->Z.
		m_sort_descending = (col == 1);
	}
	apply_sort();
	repopulate_list();
	*pResult = 0;
}

void CSearchFileDlg::add(string name, int mix_id, int file_id, int sub_mix_id, const string& top_mix_path, long long size_bytes, bool predefined)
{
	t_map_entry& e = m_map[m_map.size()];
	e.name = name;
	e.id = file_id;
	e.parent = mix_id;
	e.parent_parent = sub_mix_id;
	e.top_mix_path = top_mix_path;
	e.size_bytes = size_bytes;
	e.predefined = predefined;
}

void CSearchFileDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	int index = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (index != -1)
		open_mix(m_list.GetItemData(index));
	*pResult = 0;
}

void CSearchFileDlg::open_mix(int id)
{
	const t_map_entry& e = m_map[id];
	if (e.predefined)
	{
		// Predefined-game row: e.parent is a mix_map_list id. The iterator-
		// based open_location_mix walks parents to the on-disk fname and
		// re-descends. Route into whichever pane was active when the search
		// dialog was opened (m_prefer_right captured at construction).
		CXCCMixerView* target = m_prefer_right
			? m_main_frame->right_mix_pane()
			: m_main_frame->left_mix_pane();
		target->open_location_mix(m_main_frame->mix_map_list().find(e.parent), e.id);
	}
	else if (id < m_sepindex) // left
	{
		m_main_frame->left_mix_pane()->open_location_mix(e.parent, e.parent_parent, e.id);
	}
	else
	{
		m_main_frame->right_mix_pane()->open_location_mix(e.parent, e.parent_parent, e.id);
	}
	EndDialog(IDCANCEL);
}

void CSearchFileDlg::OnExtract()
{
	int sel_count = m_list.GetSelectedCount();
	if (sel_count == 0)
	{
		AfxMessageBox("No files selected.", MB_OK | MB_ICONINFORMATION);
		return;
	}
	CFolderPickerDialog dlg(NULL, 0, this);
	if (dlg.DoModal() != IDOK)
		return;
	string out_dir = static_cast<string>(dlg.GetPathName());
	if (out_dir.empty())
		return;
	if (out_dir.back() != '\\' && out_dir.back() != '/')
		out_dir += '\\';

	bool preserve = IsDlgButtonChecked(IDC_PRESERVE_STRUCTURE) == BST_CHECKED;

	CWaitCursor wait;
	// Per-call MIX cache. Without this, every selected row re-opened the
	// top MIX (and any sub MIX) from scratch — re-running the per-file
	// type probe / LMD scan / blowfish decrypt — making N selected rows
	// cost N x full-archive-open instead of N x one-file-extract. Cache
	// keyed on path for the top MIX, and (top_path, sub_id) for nested.
	std::map<std::string, std::unique_ptr<Cmix_file>> top_cache;
	std::map<std::pair<std::string, int>, std::unique_ptr<Cmix_file>> sub_cache;

	int idx = -1;
	while ((idx = m_list.GetNextItem(idx, LVNI_ALL | LVNI_SELECTED)) != -1)
	{
		int data = m_list.GetItemData(idx);
		auto it = m_map.find(data);
		if (it == m_map.end())
			continue;
		const t_map_entry& e = it->second;
		if (e.top_mix_path.empty())
			continue;
		auto top_it = top_cache.find(e.top_mix_path);
		if (top_it == top_cache.end())
		{
			auto top_ptr = std::make_unique<Cmix_file>();
			if (top_ptr->open(e.top_mix_path))
				continue;
			top_it = top_cache.emplace(e.top_mix_path, std::move(top_ptr)).first;
		}
		Cmix_file* top = top_it->second.get();
		Cmix_file* container = top;
		if (e.parent_parent >= 0)
		{
			int sub_id = top->get_id(e.parent_parent);
			auto key = std::make_pair(e.top_mix_path, sub_id);
			auto sub_it = sub_cache.find(key);
			if (sub_it == sub_cache.end())
			{
				auto sub_ptr = std::make_unique<Cmix_file>();
				if (sub_ptr->open(sub_id, *top))
					continue;
				sub_it = sub_cache.emplace(key, std::move(sub_ptr)).first;
			}
			container = sub_it->second.get();
		}
		Ccc_file f(false);
		if (f.open(static_cast<unsigned int>(e.id), *container))
			continue;
		t_game game = container->get_game();
		string name = mix_database::get_name(game, e.id);
		if (name.empty())
			name = nh(8, e.id);
		for (size_t i = 0; i < name.size(); i++)
			if (name[i] == '\\' || name[i] == '/')
				name[i] = '_';

		string final_dir = out_dir;
		if (preserve)
		{
			// e.name format: "top.mix (1) - sub.mix - file.ext" or "top.mix (1) - file.ext".
			// Take all components except the last, strip " (N)" pane suffix
			// and the archive extension so the output dirs are clean ("top",
			// not "top.mix").
			string mix_chain;
			size_t pos = 0;
			while (pos < e.name.size())
			{
				size_t sep = e.name.find(" - ", pos);
				if (sep == string::npos)
					break;
				string part = e.name.substr(pos, sep - pos);
				size_t paren = part.rfind(" (");
				if (paren != string::npos && part.back() == ')')
					part.resize(paren);
				size_t dot = part.rfind('.');
				if (dot != string::npos && dot > 0)
					part.resize(dot);
				if (!mix_chain.empty())
					mix_chain += '\\';
				mix_chain += part;
				pos = sep + 3;
			}
			if (!mix_chain.empty())
			{
				create_deep_dir(out_dir, mix_chain + '\\');
				final_dir = out_dir + mix_chain + '\\';
			}
		}
		f.extract(final_dir + name);
	}
	AfxMessageBox("Extraction successful.", MB_OK | MB_ICONINFORMATION);
}

void CSearchFileDlg::OnDestroy()
{
	ETSLayoutDialog::OnDestroy();
	AfxGetApp()->WriteProfileString(m_reg_key, "file_name", m_filename);
	AfxGetApp()->WriteProfileInt(m_reg_key, "include_game_mixes", m_include_game_mixes ? 1 : 0);
}

void CSearchFileDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
	int id = pDispInfo->item.lParam;
	const t_map_entry& e = find_ref(m_map, id);
	string& buffer = m_list.get_buffer();
	// e.name is "<chain> - <leaf>" where chain is the MIX path
	// ("top.mix (1)" or "top.mix (1) - sub.mix"). Last " - " separates
	// chain from leaf. Subitem 0 = chain (Source), subitem 1 = leaf (File).
	const std::string& full = e.name;
	auto sep = full.rfind(" - ");
	switch (pDispInfo->item.iSubItem)
	{
	case 0:
		// File leaf (last segment of "<chain> - <leaf>").
		buffer = (sep == std::string::npos) ? full : full.substr(sep + 3);
		break;
	case 1:
		buffer = e.size_bytes > 0 ? theme::format_size(e.size_bytes) : std::string();
		break;
	case 2:
	{
		// Directory of the top MIX. e.top_mix_path holds the full file path
		// captured at search time (so it survives pane navigation). Strip the
		// leaf to get the containing folder.
		const std::string& p = e.top_mix_path;
		auto slash = p.find_last_of("\\/");
		buffer = (slash == std::string::npos) ? std::string() : p.substr(0, slash);
		break;
	}
	default:
		buffer.clear();
		break;
	}
	pDispInfo->item.pszText = const_cast<char*>(buffer.c_str());
	*pResult = 0;
}
