#include "stdafx.h"
#include "MainFrm.h"
#include "SearchFileDlg.h"
#include "string_conversion.h"
#include "theme.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <cc_file.h>
#include <fname.h>
#include <id_log.h>

// Mirror of CXCCMixerView's free helper; redeclared here so we don't have to
// expose it in a public header.
extern string totalSize(size_t i);

// Worker-thread <-> dialog message ids. WM_APP (0x8000) is the private app
// range; doesn't collide with Mixer's WM_USER+0x101..+0x103 usages.
#define WM_SEARCH_PROGRESS  (WM_APP + 1)
#define WM_SEARCH_DONE      (WM_APP + 2)

// One-shot timer that clears the "N results." progress text shortly after
// a successful search completes so it stops nagging once the user has the
// data. Cancel/error states keep their message until the next search.
#define TIMER_CLEAR_PROGRESS 1
#define PROGRESS_CLEAR_MS    850

// Heap-passed context for the search worker. The dialog snapshots its inputs
// before kicking the thread so the worker never touches MFC state that could
// mutate (e.g. user navigating panes mid-search). out_map is allocated on the
// UI thread, filled by the worker, and ownership transferred back via
// WM_SEARCH_DONE's lParam (deleted by the handler).
namespace
{
	struct search_ctx
	{
		CSearchFileDlg* dlg;       // raw — dialog outlives the worker; OnDestroy
		                            // sets cancel + bumps seq before destruction.
		HWND dlg_hwnd;
		UINT seq;
		std::atomic<bool>* cancel; // points into the dialog's m_cancel
		// Snapshots — the worker owns these:
		std::map<int, t_index_entry> left_index;
		std::string left_dir;
		std::map<int, t_index_entry> right_index;
		std::string right_dir;
		bool include_game_mixes;
		t_mix_map_list mix_map;    // copy of MainFrame::mix_map_list()
		int sepindex;              // m_map.size() after left walk
	};
}

CSearchFileDlg::CSearchFileDlg(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(CSearchFileDlg::IDD, pParent, "find_file_dlg")
{
	m_reg_key = "find_file_dlg";
	//{{AFX_DATA_INIT(CSearchFileDlg)
	//}}AFX_DATA_INIT
	m_filename = AfxGetApp()->GetProfileString(m_reg_key, "file_name");
	m_include_game_mixes = AfxGetApp()->GetProfileInt(m_reg_key, "include_game_mixes", 1) != 0;
	m_sort_column = AfxGetApp()->GetProfileInt(m_reg_key, "sort_col", 0);
	if (m_sort_column < 0 || m_sort_column > 2)
		m_sort_column = 0;
	m_sort_descending = AfxGetApp()->GetProfileInt(m_reg_key, "sort_desc", 0) != 0;
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
	ON_MESSAGE(WM_SEARCH_PROGRESS, &CSearchFileDlg::OnSearchProgress)
	ON_MESSAGE(WM_SEARCH_DONE, &CSearchFileDlg::OnSearchDone)
	ON_WM_TIMER()
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
			<< item(IDC_SEARCH_PROGRESS, GREEDY)
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
	// Bind the progress static so we can update its text from the
	// PROGRESS / DONE handlers. The control is empty until a search runs.
	m_progress.SubclassDlgItem(IDC_SEARCH_PROGRESS, this);
	theme::apply_dialog(GetSafeHwnd());
	theme::apply_column_headers(m_list.GetSafeHwnd());
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_file");
	// Overlay-paint group header bars in dark mode (system's default bar
	// renders dim red/purple text against the dark theme; CDDS_GROUP
	// clrText/clrTextBk is a no-op on Win10/11). Idempotent + no-op in
	// light mode.
	theme::apply_listview_groups(m_list.GetSafeHwnd());
	return true;
}

void CSearchFileDlg::find(Cmix_file& f, string file_name, string mix_name, int mix_id, const std::vector<int>& sub_mix_chain, const string& top_mix_path, bool predefined, std::atomic<bool>* cancel, const t_mix_map_list* mix_map)
{
	for (int i = 0; i < f.get_c_files(); i++)
	{
		if (cancel && cancel->load(std::memory_order_relaxed))
			return;
		const int id = f.get_id(i);
		string name = f.get_name(id);
		const long long sz = f.get_size(id);
		if (name.empty())
		{
			name = nh(8, id);
			if (Cmix_file::get_id(f.get_game(), file_name) == id)
				add(mix_name + " - " + name, mix_id, id, sub_mix_chain, top_mix_path, sz, predefined);
		}
		else if (fname_filter(name, file_name))
			add(mix_name + " - " + name, mix_id, id, sub_mix_chain, top_mix_path, sz, predefined);
		if (f.get_type(id) == ft_mix)
		{
			Cmix_file fg;
			if (!fg.open(id, f))
			{
				// Descend into the nested mix: append the in-mix index of this
				// sub-MIX to the chain so the opener can walk root -> sub_chain
				// to reach the file. Previously this overload took a single
				// `sub_mix_id` int that got overwritten on each recursion,
				// dropping intermediate links and landing on the wrong mix at
				// depth 3+ (the user-reported "goes to second mix, not file").
				std::vector<int> next_chain = sub_mix_chain;
				next_chain.push_back(i);
				find(fg, file_name, mix_name + " - " + name, mix_id, next_chain, top_mix_path, predefined, cancel, mix_map);
			}
		}
	}
	const t_mix_map_list& mm = mix_map ? *mix_map : m_main_frame->mix_map_list();
	for (auto& i : mm)
	{
		if (cancel && cancel->load(std::memory_order_relaxed))
			return;
		if (i.second.parent != mix_id)
			continue;
		Cmix_file g;
		if (!g.open(i.second.id, f))
			// New mix_map_list root: chain resets to empty (this nested mix is
			// opened via mix_map_list lookups, not by descending in-mix indices).
			find(g, file_name, mix_name + " - " + (i.second.name.empty() ? nh(8, i.second.id) : i.second.name), i.first, std::vector<int>{}, top_mix_path, predefined, cancel, mix_map);
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
void CSearchFileDlg::find_predefined(std::atomic<bool>* cancel, const t_mix_map_list* mix_map)
{
	const t_mix_map_list& mm = mix_map ? *mix_map : m_main_frame->mix_map_list();
	for (auto& i : mm)
	{
		if (cancel && cancel->load(std::memory_order_relaxed))
			return;
		if (i.second.fname.empty())
			continue;
		Cmix_file f;
		if (f.open(i.second.fname))
			continue;
		const auto& parent = find_ref(mm, i.second.parent);
		find(f, get_filename(), parent.name + " - " + i.second.name, i.first, std::vector<int>{}, i.second.fname, true, cancel, mix_map);
	}
}

void CSearchFileDlg::find(const map<int, t_index_entry>& t_map, const string& post, const string& dir, std::atomic<bool>* cancel, const t_mix_map_list* mix_map)
{
	for (auto& i : t_map)
	{
		if (cancel && cancel->load(std::memory_order_relaxed))
			return;
		if (i.second.name.empty())
			continue;
		string fname = dir.rfind('\\') == string::npos ? (dir + '\\' + i.second.name) : (dir + i.second.name);
		Cmix_file f;
		if (!f.open(fname))
		{
			find(f, get_filename(), i.second.name + post, i.first, std::vector<int>{}, fname, false, cancel, mix_map);
		}
		else if (i.second.ft == ft_mix)
		{
			Cmix_file_rd f_rd;
			if (!f_rd.open(fname))
			{
				find(f_rd, get_filename(), i.second.name + post, i.first, std::vector<int>{}, fname, false, cancel, mix_map);
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

// Search button: either start a new background search or cancel a running
// one. The walk runs on a CWinThread worker so the dialog stays drag/
// resize/close-able. UI thread only touches m_map / m_list in start_search()
// (clears) and finish_search() (populates).
void CSearchFileDlg::OnFind()
{
	if (m_searching)
	{
		// Second click during a running search = cancel. Worker checks the
		// flag at the top of every iteration and returns early.
		m_cancel.store(true, std::memory_order_relaxed);
		m_progress.SetWindowText("Cancelling...");
		return;
	}
	if (!UpdateData(true))
		return;
	start_search();
}

void CSearchFileDlg::start_search()
{
	// Kill any pending auto-clear from a previous search — the new search
	// will set its own progress text and arm a fresh timer on completion.
	KillTimer(TIMER_CLEAR_PROGRESS);
	m_include_game_mixes = IsDlgButtonChecked(IDC_INCLUDE_GAME_MIXES) == BST_CHECKED;
	m_list.DeleteAllItems();
	m_map.clear();
	m_order.clear();
	// Bump sequence so any late PROGRESS / DONE post from a prior worker
	// is dropped by the handler. Then allocate a fresh context owned by
	// the worker.
	++m_search_seq;
	m_cancel.store(false, std::memory_order_relaxed);
	auto ctx = std::make_unique<search_ctx>();
	ctx->dlg = this;
	ctx->dlg_hwnd = GetSafeHwnd();
	ctx->seq = m_search_seq;
	ctx->cancel = &m_cancel;
	ctx->left_index = m_main_frame->left_mix_pane()->t_index_list();
	ctx->left_dir = m_main_frame->left_mix_pane()->current_dir();
	ctx->right_index = m_main_frame->right_mix_pane()->t_index_list();
	ctx->right_dir = m_main_frame->right_mix_pane()->current_dir();
	ctx->include_game_mixes = m_include_game_mixes;
	ctx->mix_map = m_main_frame->mix_map_list();
	ctx->sepindex = 0;
	m_searching = true;
	m_progress.SetWindowText("Searching...");
	SetDlgItemText(IDOK, "Cancel");
	// CWinThread is auto-deleted when the worker returns. Pass raw ptr;
	// worker takes ownership.
	m_worker = AfxBeginThread(&CSearchFileDlg::search_thread_proc, ctx.release());
}

UINT AFX_CDECL CSearchFileDlg::search_thread_proc(LPVOID p)
{
	std::unique_ptr<search_ctx> ctx(static_cast<search_ctx*>(p));
	// The dialog pointer is stable for the lifetime of the worker — the
	// dialog is modal, OnDestroy sets cancel + bumps seq before MFC tears
	// down the C++ object, and the worker checks cancel at every iteration
	// boundary. We write directly into dlg->m_map via the existing
	// add() API rather than through a swapped heap map; the UI thread
	// doesn't read m_map between start_search() (which clears it) and
	// OnSearchDone (which reads it after our DONE post).
	CSearchFileDlg* dlg = ctx->dlg;

	auto post_progress = [&](size_t n) {
		::PostMessage(ctx->dlg_hwnd, WM_SEARCH_PROGRESS,
			static_cast<WPARAM>(ctx->seq), static_cast<LPARAM>(n));
	};

	// Left pane walk. mix_map snapshot is passed so the worker never reads
	// MainFrame's live container (which the UI thread can mutate).
	dlg->find(ctx->left_index, "", ctx->left_dir, ctx->cancel, &ctx->mix_map);
	post_progress(dlg->m_map.size());
	ctx->sepindex = static_cast<int>(dlg->m_map.size());

	if (!ctx->cancel->load(std::memory_order_relaxed))
	{
		dlg->find(ctx->right_index, "", ctx->right_dir, ctx->cancel, &ctx->mix_map);
		post_progress(dlg->m_map.size());
	}

	if (ctx->include_game_mixes && !ctx->cancel->load(std::memory_order_relaxed))
	{
		dlg->find_predefined(ctx->cancel, &ctx->mix_map);
		post_progress(dlg->m_map.size());
	}

	// DONE lParam carries sepindex; OnSearchDone reads m_map directly.
	::PostMessage(ctx->dlg_hwnd, WM_SEARCH_DONE,
		static_cast<WPARAM>(ctx->seq), static_cast<LPARAM>(ctx->sepindex));
	return 0;
}

LRESULT CSearchFileDlg::OnSearchProgress(WPARAM wp, LPARAM lp)
{
	if (static_cast<UINT>(wp) != m_search_seq)
		return 0; // stale post from a cancelled run
	char buf[64];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE,
		m_cancel.load(std::memory_order_relaxed) ? "Cancelling... (%zu found)" : "Found %zu - searching...",
		static_cast<size_t>(lp));
	m_progress.SetWindowText(buf);
	return 0;
}

LRESULT CSearchFileDlg::OnSearchDone(WPARAM wp, LPARAM lp)
{
	if (static_cast<UINT>(wp) != m_search_seq)
		return 0; // late post from a cancelled or stale run
	const bool was_cancelled = m_cancel.load(std::memory_order_relaxed);
	if (!was_cancelled)
	{
		// Worker has written results directly into m_map via add().
		m_sepindex = static_cast<int>(lp);
		finish_search();
	}
	else
	{
		// Cancelled — discard any partial results, leave the list empty.
		m_map.clear();
		m_sepindex = 0;
		m_order.clear();
		m_list.DeleteAllItems();
		m_list.RemoveAllGroups();
	}
	m_searching = false;
	m_cancel.store(false, std::memory_order_relaxed);
	m_worker = nullptr;
	SetDlgItemText(IDOK, "Search");
	if (was_cancelled)
		m_progress.SetWindowText("Cancelled.");
	else
	{
		char buf[64];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%zu result%s.",
			m_map.size(), m_map.size() == 1 ? "" : "s");
		m_progress.SetWindowText(buf);
		// Auto-clear the "N results." text after a brief moment so the
		// label stops competing with the results for attention. Cancel
		// keeps its message indefinitely (more informative on re-entry).
		SetTimer(TIMER_CLEAR_PROGRESS, PROGRESS_CLEAR_MS, NULL);
	}
	return 0;
}

void CSearchFileDlg::OnTimer(UINT_PTR id)
{
	if (id == TIMER_CLEAR_PROGRESS)
	{
		KillTimer(TIMER_CLEAR_PROGRESS);
		m_progress.SetWindowText("");
		return;
	}
	ETSLayoutDialog::OnTimer(id);
}

void CSearchFileDlg::finish_search()
{
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
	// Column autosize: always header-width, regardless of result count.
	// Per-content fit (LVSCW_AUTOSIZE) walks every row's dispinfo callback
	// per column = O(N x cols x 2) and is the visible "clunkiness" the
	// user reported. Header-width is instant, readable, sortable, and
	// columns are user-resizable + hidable via the right-click header
	// menu if a row's content is wider than its header. Trade-off
	// accepted across all result-set sizes for predictable behavior.
	m_list.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(1, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(2, LVSCW_AUTOSIZE_USEHEADER);
	// Wire right-click-header column-visibility menu after columns have
	// real widths to remember. Idempotent across re-searches.
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_file");
}

void CSearchFileDlg::apply_sort()
{
	// Group view sorts items within each group; group ordering itself is
	// fixed by group_id (search-encounter order). Primary key is group_id
	// so same-group rows stay clustered, then user-selected column.
	auto key_less = [this](const t_map_entry& a, const t_map_entry& b) {
		switch (m_sort_column)
		{
		case 1: // Size
		{
			if (a.size_bytes != b.size_bytes)
				return a.size_bytes < b.size_bytes;
			return _stricmp(file_part(a.name).c_str(), file_part(b.name).c_str()) < 0;
		}
		case 2: // Path
		{
			int c = _stricmp(a.top_mix_path.c_str(), b.top_mix_path.c_str());
			if (c != 0)
				return c < 0;
			return _stricmp(file_part(a.name).c_str(), file_part(b.name).c_str()) < 0;
		}
		case 0: // File
		default:
			return _stricmp(file_part(a.name).c_str(), file_part(b.name).c_str()) < 0;
		}
	};
	auto cmp = [this, &key_less](int ka, int kb) {
		const t_map_entry& a = m_map.find(ka)->second;
		const t_map_entry& b = m_map.find(kb)->second;
		if (a.group_id != b.group_id)
			return a.group_id < b.group_id;
		return m_sort_descending ? key_less(b, a) : key_less(a, b);
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

void CSearchFileDlg::add(string name, int mix_id, int file_id, const std::vector<int>& sub_mix_chain, const string& top_mix_path, long long size_bytes, bool predefined)
{
	t_map_entry& e = m_map[m_map.size()];
	e.name = name;
	e.id = file_id;
	e.parent = mix_id;
	e.sub_mix_chain = sub_mix_chain;
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
		target->open_location_mix(m_main_frame->mix_map_list().find(e.parent), e.id, e.sub_mix_chain);
	}
	else if (id < m_sepindex) // left
	{
		m_main_frame->left_mix_pane()->open_location_mix(e.parent, e.sub_mix_chain, e.id);
	}
	else
	{
		m_main_frame->right_mix_pane()->open_location_mix(e.parent, e.sub_mix_chain, e.id);
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
	// keyed on path for the top MIX, and (top_path, chain-prefix) for any
	// nested level (supports arbitrary mix > mix > mix > ... > file depth;
	// previously only the immediate parent was cached so deeper nesting
	// re-opened intermediate mixes per file).
	std::map<std::string, std::unique_ptr<Cmix_file>> top_cache;
	std::map<std::pair<std::string, std::vector<int>>, std::unique_ptr<Cmix_file>> sub_cache;

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
		// Walk the sub-MIX chain (root -> ... -> immediate parent of file),
		// reusing cached containers per chain prefix. A bad open at any level
		// skips the row (continue propagates via the goto-equivalent below).
		bool chain_ok = true;
		std::vector<int> prefix;
		for (int idx_in_parent : e.sub_mix_chain)
		{
			prefix.push_back(idx_in_parent);
			auto key = std::make_pair(e.top_mix_path, prefix);
			auto sub_it = sub_cache.find(key);
			if (sub_it == sub_cache.end())
			{
				int sub_id = container->get_id(idx_in_parent);
				auto sub_ptr = std::make_unique<Cmix_file>();
				if (sub_ptr->open(sub_id, *container))
				{
					chain_ok = false;
					break;
				}
				sub_it = sub_cache.emplace(key, std::move(sub_ptr)).first;
			}
			container = sub_it->second.get();
		}
		if (!chain_ok)
			continue;
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
	// Tell the worker to bail at the next iteration boundary, and bump the
	// sequence so any DONE/PROGRESS post that arrives at our queue after
	// this point is dropped by the handler. The CWinThread auto-deletes
	// when its proc returns; we don't need to join.
	m_cancel.store(true, std::memory_order_relaxed);
	++m_search_seq;
	KillTimer(TIMER_CLEAR_PROGRESS);
	ETSLayoutDialog::OnDestroy();
	AfxGetApp()->WriteProfileString(m_reg_key, "file_name", m_filename);
	AfxGetApp()->WriteProfileInt(m_reg_key, "include_game_mixes", m_include_game_mixes ? 1 : 0);
	AfxGetApp()->WriteProfileInt(m_reg_key, "sort_col", m_sort_column);
	AfxGetApp()->WriteProfileInt(m_reg_key, "sort_desc", m_sort_descending ? 1 : 0);
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
