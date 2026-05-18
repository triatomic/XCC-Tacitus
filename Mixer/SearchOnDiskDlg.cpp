#include "stdafx.h"
#include "MainFrm.h"
#include "SearchOnDiskDlg.h"
#include "theme.h"
#include "XCC MixerView.h"

// Everything IPC channel — no DLL, no .lib. We talk directly to the running
// Everything service via WM_COPYDATA to its taskbar notification window. The
// only file from the SDK we need is the constants/structs header below.
#include "everything_ipc.h"

#include <algorithm>

using namespace std;

namespace
{
	// dwData value Everything will use on the WM_COPYDATA result reply.
	// Arbitrary; just has to match what we put in EVERYTHING_IPC_QUERY2.
	const DWORD kReplyMsg = 0x58434346; // 'XCCF'

	// 10 seconds is generous — Everything returns even 100k-result queries
	// in under a second on a warm index. Timeout exists so a hung service
	// doesn't lock the dialog forever.
	const DWORD kReplyTimeoutMs = 10000;

	// Ext filter combobox entries. Mirrors XCCMixerView::OnFileOpen's
	// `Archive files` filter at Mixer/XCC MixerView.cpp:468 so this dialog
	// finds exactly what File > Open accepts. `query_ext` is the Everything
	// ext: clause body (semicolon-separated for "All"). `type_label` is
	// what the Type column shows.
	struct ext_option
	{
		const char* combo_label;
		const wchar_t* query_ext;       // value passed after "ext:"
		const char* type_label;         // unused for "All"
	};
	const ext_option ext_options[] = {
		{ "All archives (*.mix; *.big; *.dat; *.pak; *.pkg; *.wsx)",
		  L"mix;big;dat;pak;pkg;wsx", "" },
		{ "MIX",                       L"mix", "MIX" },
		{ "BIG (Generals)",            L"big", "BIG (Generals)" },
		{ "DAT",                       L"dat", "DAT" },
		{ "PAK (Nox/Westwood)",        L"pak", "PAK (Nox/Westwood)" },
		{ "PKG",                       L"pkg", "PKG" },
		{ "WSX (Sole Survivor)",       L"wsx", "WSX (Sole Survivor)" },
	};
	const int kExtOptionCount = static_cast<int>(sizeof(ext_options) / sizeof(ext_options[0]));

	// Per-extension display label for the Type column. Keyed on lowercase
	// extension (no leading dot). Falls back to uppercase ext for unknowns.
	std::string label_for_ext(const std::wstring& path)
	{
		auto dot = path.find_last_of(L'.');
		if (dot == std::wstring::npos)
			return std::string();
		std::wstring ext = path.substr(dot + 1);
		for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
		for (int i = 1; i < kExtOptionCount; i++)
		{
			if (ext == ext_options[i].query_ext)
				return ext_options[i].type_label;
		}
		// Defensive fallback (shouldn't happen given our query).
		std::string out;
		for (wchar_t c : ext)
			out += static_cast<char>(::towupper(c));
		return out;
	}

	// FILETIME (packed int64) -> "yyyy-MM-dd HH:mm" via locale-aware
	// GetDateFormat/GetTimeFormat. Returns empty for zero/invalid.
	std::string format_mtime(long long mtime)
	{
		if (mtime <= 0)
			return std::string();
		FILETIME ft;
		ft.dwLowDateTime = static_cast<DWORD>(mtime & 0xFFFFFFFFu);
		ft.dwHighDateTime = static_cast<DWORD>((mtime >> 32) & 0xFFFFFFFFu);
		FILETIME ft_local;
		if (!::FileTimeToLocalFileTime(&ft, &ft_local))
			return std::string();
		SYSTEMTIME st;
		if (!::FileTimeToSystemTime(&ft_local, &st))
			return std::string();
		wchar_t date_buf[64] = {};
		wchar_t time_buf[64] = {};
		::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL,
			date_buf, _countof(date_buf));
		::GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL,
			time_buf, _countof(time_buf));
		std::wstring wide = std::wstring(date_buf) + L" " + time_buf;
		int n = ::WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
		std::string out(n > 0 ? n - 1 : 0, '\0');
		if (n > 1)
			::WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &out[0], n, NULL, NULL);
		return out;
	}
}

// Hidden reply window. Registered with a unique class name on first
// instantiation; pumps WM_COPYDATA back into the owning dialog.
class CSearchOnDiskDlg::CReplyWnd : public CWnd
{
public:
	CReplyWnd(CSearchOnDiskDlg* owner) : m_owner(owner) {}

	BOOL Create()
	{
		static const wchar_t* kClass = L"XCC_SearchOnDiskReply";
		static bool s_registered = false;
		if (!s_registered)
		{
			WNDCLASSW wc = {};
			wc.lpfnWndProc = ::DefWindowProcW;
			wc.hInstance = AfxGetInstanceHandle();
			wc.lpszClassName = kClass;
			::RegisterClassW(&wc);
			s_registered = true;
		}
		HWND h = ::CreateWindowExW(0, kClass, L"", 0,
			0, 0, 0, 0, HWND_MESSAGE, NULL, AfxGetInstanceHandle(), NULL);
		if (!h)
			return FALSE;
		// MFC needs us to attach so WindowProc is called on incoming
		// messages. SubclassWindow does the attach + swap-wndproc dance.
		return SubclassWindow(h);
	}

protected:
	LRESULT WindowProc(UINT msg, WPARAM wp, LPARAM lp) override
	{
		if (msg == WM_COPYDATA)
		{
			COPYDATASTRUCT* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
			if (cds && cds->dwData == kReplyMsg && cds->cbData > 0 && cds->lpData)
			{
				const BYTE* p = static_cast<const BYTE*>(cds->lpData);
				m_owner->m_reply_buf.assign(p, p + cds->cbData);
				m_owner->m_reply_received = true;
			}
			return TRUE;
		}
		return CWnd::WindowProc(msg, wp, lp);
	}

private:
	CSearchOnDiskDlg* m_owner;
};

CSearchOnDiskDlg::CSearchOnDiskDlg(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(CSearchOnDiskDlg::IDD, pParent, "find_on_disk_dlg")
{
	m_reg_key = "find_on_disk_dlg";
	m_filename = AfxGetApp()->GetProfileString(m_reg_key, "filter");
	m_ext_choice = AfxGetApp()->GetProfileInt(m_reg_key, "ext_filter", 0);
	if (m_ext_choice < 0 || m_ext_choice >= kExtOptionCount)
		m_ext_choice = 0;
	m_sort_column = AfxGetApp()->GetProfileInt(m_reg_key, "sort_col", 0);
	if (m_sort_column < 0 || m_sort_column > 4)
		m_sort_column = 0;
	m_sort_descending = AfxGetApp()->GetProfileInt(m_reg_key, "sort_desc", 0) != 0;
}

// Out-of-line so unique_ptr<CReplyWnd> sees a complete type.
CSearchOnDiskDlg::~CSearchOnDiskDlg() = default;

void CSearchOnDiskDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST, m_list);
	DDX_Control(pDX, IDC_EXT_FILTER, m_ext_filter);
	DDX_Text(pDX, IDC_FILENAME, m_filename);
	DDX_CBIndex(pDX, IDC_EXT_FILTER, m_ext_choice);
}

BEGIN_MESSAGE_MAP(CSearchOnDiskDlg, ETSLayoutDialog)
	ON_BN_CLICKED(IDOK, OnFind)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST, OnDblclkList)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LIST, OnColumnclickList)
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST, OnGetdispinfoList)
	ON_WM_DESTROY()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDCANCEL, &CSearchOnDiskDlg::OnBnClickedCancel)
END_MESSAGE_MAP()

HBRUSH CSearchOnDiskDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSearchOnDiskDlg::set(CMainFrame* main_frame, bool prefer_right)
{
	m_main_frame = main_frame;
	m_prefer_right = prefer_right;
}

BOOL CSearchOnDiskDlg::OnInitDialog()
{
	CreateRoot(VERTICAL)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_FILENAME_STATIC, NORESIZE)
			<< item(IDC_FILENAME, GREEDY)
			<< item(IDC_EXT_FILTER, NORESIZE)
			)
		<< item(IDC_LIST, GREEDY)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_DISK_SEARCH_PROGRESS, GREEDY)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();

	// Populate the ext filter combobox from the single-source-of-truth
	// ext_options[] table. Pre-select the persisted choice.
	for (int i = 0; i < kExtOptionCount; i++)
		m_ext_filter.AddString(ext_options[i].combo_label);
	m_ext_filter.SetCurSel(m_ext_choice);

	m_list.InsertColumn(0, "Name");
	m_list.InsertColumn(1, "Path");
	m_list.InsertColumn(2, "Date");
	m_list.InsertColumn(3, "Size");
	m_list.InsertColumn(4, "Type");
	m_list.set_size(0);

	m_progress.SubclassDlgItem(IDC_DISK_SEARCH_PROGRESS, this);

	theme::apply_dialog(GetSafeHwnd());
	theme::apply_column_headers(m_list.GetSafeHwnd());
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_on_disk_v2");

	return true;
}

void CSearchOnDiskDlg::OnFind()
{
	if (!UpdateData(true))
		return;

	// Clamp m_ext_choice (DDX already wrote it from the combobox).
	if (m_ext_choice < 0 || m_ext_choice >= kExtOptionCount)
		m_ext_choice = 0;

	// Build the Everything query: substring filter on the file name, ANDed
	// with the ext: clause derived from the combobox selection.
	CString filter = m_filename;
	filter.Trim();
	CStringW ext_clause = L"ext:";
	ext_clause += ext_options[m_ext_choice].query_ext;
	CStringW queryW;
	if (filter.IsEmpty())
	{
		queryW = ext_clause;
	}
	else
	{
		// Wrap bare substrings in wildcards; if the user typed wildcards
		// themselves, pass them through.
		CStringA filterA(filter);
		bool has_wildcard = (filterA.Find('*') >= 0) || (filterA.Find('?') >= 0);
		CStringW userW(filter);
		if (has_wildcard)
			queryW = userW + L" " + ext_clause;
		else
			queryW = L"*" + userW + L"* " + ext_clause;
	}

	// Locate the Everything taskbar-notification window. Null = not running.
	HWND target = ::FindWindowW(EVERYTHING_IPC_WNDCLASSW, NULL);
	if (!target)
	{
		MessageBox(
			"Everything is not running.\n\n"
			"Install or launch Everything (https://www.voidtools.com) "
			"to use this search.",
			"XCC Mixer", MB_OK | MB_ICONINFORMATION);
		EndDialog(IDCANCEL);
		return;
	}

	// Lazy-create the hidden reply window. Kept alive for the dialog's
	// lifetime so repeated searches don't have to re-register the class.
	if (!m_reply_wnd)
	{
		m_reply_wnd.reset(new CReplyWnd(this));
		if (!m_reply_wnd->Create())
		{
			m_progress.SetWindowText("Failed to create reply window.");
			m_reply_wnd.reset();
			return;
		}
	}

	// Build the query packet: EVERYTHING_IPC_QUERY2 header + null-terminated
	// wide search string immediately after.
	const size_t header_sz = sizeof(EVERYTHING_IPC_QUERY2);
	const size_t str_chars = static_cast<size_t>(queryW.GetLength()) + 1; // +null
	const size_t str_bytes = str_chars * sizeof(wchar_t);
	std::vector<BYTE> packet(header_sz + str_bytes, 0);
	EVERYTHING_IPC_QUERY2* q = reinterpret_cast<EVERYTHING_IPC_QUERY2*>(packet.data());
	q->reply_hwnd = reinterpret_cast<DWORD_PTR>(m_reply_wnd->GetSafeHwnd()) & 0xFFFFFFFFu;
	q->reply_copydata_message = kReplyMsg;
	q->search_flags = 0;
	q->offset = 0;
	q->max_results = 50000;
	q->request_flags = EVERYTHING_IPC_QUERY2_REQUEST_FULL_PATH_AND_NAME
		| EVERYTHING_IPC_QUERY2_REQUEST_SIZE
		| EVERYTHING_IPC_QUERY2_REQUEST_DATE_MODIFIED;
	q->sort_type = EVERYTHING_IPC_SORT_NAME_ASCENDING;
	::memcpy(packet.data() + header_sz, static_cast<LPCWSTR>(queryW), str_bytes);

	COPYDATASTRUCT cds = {};
	cds.dwData = EVERYTHING_IPC_COPYDATA_QUERY2W;
	cds.cbData = static_cast<DWORD>(packet.size());
	cds.lpData = packet.data();

	m_progress.SetWindowText("Querying Everything...");
	m_reply_received = false;
	m_reply_buf.clear();

	LRESULT accepted = ::SendMessageW(target, WM_COPYDATA,
		reinterpret_cast<WPARAM>(m_reply_wnd->GetSafeHwnd()),
		reinterpret_cast<LPARAM>(&cds));
	if (!accepted)
	{
		m_progress.SetWindowText("Everything refused the query.");
		return;
	}

	// Pump messages until the reply arrives or we time out. The dialog
	// stays drag/resize-able during the wait — drained input dispatches
	// normally.
	const DWORD deadline = ::GetTickCount() + kReplyTimeoutMs;
	while (!m_reply_received)
	{
		const DWORD now = ::GetTickCount();
		if (now >= deadline)
			break;
		MSG msg;
		if (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
		else
		{
			::MsgWaitForMultipleObjects(0, NULL, FALSE,
				deadline - now, QS_ALLINPUT);
		}
	}

	if (!m_reply_received)
	{
		m_progress.SetWindowText("Timed out waiting for Everything.");
		return;
	}

	// Parse the EVERYTHING_IPC_LIST2 payload. Each item's data sits at
	// list_base + item.data_offset and contains, in request_flags order:
	//   DWORD name_length_chars; wchar_t path[name_length+1];
	//   LARGE_INTEGER size;
	//   FILETIME date_modified;
	m_results.clear();
	if (m_reply_buf.size() >= sizeof(EVERYTHING_IPC_LIST2))
	{
		const BYTE* base = m_reply_buf.data();
		const EVERYTHING_IPC_LIST2* list = reinterpret_cast<const EVERYTHING_IPC_LIST2*>(base);
		const size_t buf_sz = m_reply_buf.size();
		const DWORD nitems = list->numitems;
		const EVERYTHING_IPC_ITEM2* items = reinterpret_cast<const EVERYTHING_IPC_ITEM2*>(
			base + sizeof(EVERYTHING_IPC_LIST2));
		m_results.reserve(nitems);
		for (DWORD i = 0; i < nitems; i++)
		{
			// Skip folders. EVERYTHING_IPC_FOLDER is the folder bit in
			// item.flags (same convention as the LIST struct's items).
			if (items[i].flags & EVERYTHING_IPC_FOLDER)
				continue;
			const DWORD off = items[i].data_offset;
			if (off + sizeof(DWORD) > buf_sz)
				continue;
			const BYTE* p = base + off;
			const BYTE* end = base + buf_sz;
			// Field 1: FULL_PATH_AND_NAME — DWORD length + wchar string + L'\0'.
			DWORD name_len = *reinterpret_cast<const DWORD*>(p);
			p += sizeof(DWORD);
			const size_t need_bytes = (static_cast<size_t>(name_len) + 1) * sizeof(wchar_t);
			if (p + need_bytes > end)
				continue;
			std::wstring path(reinterpret_cast<const wchar_t*>(p), name_len);
			p += need_bytes;
			// Field 2: SIZE — LARGE_INTEGER.
			if (p + sizeof(LARGE_INTEGER) > end)
				continue;
			LARGE_INTEGER sz;
			::memcpy(&sz, p, sizeof(sz));
			p += sizeof(LARGE_INTEGER);
			// Field 3: DATE_MODIFIED — FILETIME (two DWORDs = 8 bytes).
			long long mtime = 0;
			if (p + sizeof(FILETIME) <= end)
			{
				FILETIME ft;
				::memcpy(&ft, p, sizeof(ft));
				p += sizeof(FILETIME);
				mtime = (static_cast<long long>(ft.dwHighDateTime) << 32)
					| static_cast<long long>(ft.dwLowDateTime);
			}
			t_result r;
			r.full_path = std::move(path);
			r.size_bytes = sz.QuadPart;
			r.mtime = mtime;
			m_results.push_back(std::move(r));
		}
	}

	m_order.clear();
	m_order.reserve(m_results.size());
	for (size_t i = 0; i < m_results.size(); i++)
		m_order.push_back(static_cast<int>(i));
	apply_sort();
	repopulate_list();

	m_list.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(1, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(2, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(3, LVSCW_AUTOSIZE_USEHEADER);
	m_list.SetColumnWidth(4, LVSCW_AUTOSIZE_USEHEADER);
	theme::enable_column_visibility_menu(m_list.GetSafeHwnd(), "search_on_disk_v2");

	char buf[64];
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%zu result%s.",
		m_results.size(), m_results.size() == 1 ? "" : "s");
	m_progress.SetWindowText(buf);
}

// Split full_path into (file leaf, parent dir). Returns positions relative
// to the wstring; callers narrow as needed via WideCharToMultiByte.
static void split_path(const std::wstring& p, std::wstring& leaf, std::wstring& dir)
{
	auto slash = p.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
	{
		leaf = p;
		dir.clear();
	}
	else
	{
		leaf = p.substr(slash + 1);
		dir = p.substr(0, slash);
	}
}

static std::string narrow(const std::wstring& w)
{
	if (w.empty())
		return std::string();
	int n = ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()),
		NULL, 0, NULL, NULL);
	std::string out(n, '\0');
	::WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()),
		&out[0], n, NULL, NULL);
	return out;
}

void CSearchOnDiskDlg::apply_sort()
{
	auto key_less = [this](const t_result& a, const t_result& b) {
		std::wstring la, da, lb, db;
		split_path(a.full_path, la, da);
		split_path(b.full_path, lb, db);
		switch (m_sort_column)
		{
		case 1: // Path
		{
			int c = _wcsicmp(da.c_str(), db.c_str());
			if (c != 0)
				return c < 0;
			return _wcsicmp(la.c_str(), lb.c_str()) < 0;
		}
		case 2: // Date
			if (a.mtime != b.mtime)
				return a.mtime < b.mtime;
			return _wcsicmp(la.c_str(), lb.c_str()) < 0;
		case 3: // Size
			if (a.size_bytes != b.size_bytes)
				return a.size_bytes < b.size_bytes;
			return _wcsicmp(la.c_str(), lb.c_str()) < 0;
		case 4: // Type
		{
			std::string ta = label_for_ext(a.full_path);
			std::string tb = label_for_ext(b.full_path);
			int c = _stricmp(ta.c_str(), tb.c_str());
			if (c != 0)
				return c < 0;
			return _wcsicmp(la.c_str(), lb.c_str()) < 0;
		}
		case 0: // Name
		default:
			return _wcsicmp(la.c_str(), lb.c_str()) < 0;
		}
	};
	auto cmp = [this, &key_less](int ia, int ib) {
		const t_result& a = m_results[ia];
		const t_result& b = m_results[ib];
		return m_sort_descending ? key_less(b, a) : key_less(a, b);
	};
	std::stable_sort(m_order.begin(), m_order.end(), cmp);
}

void CSearchOnDiskDlg::repopulate_list()
{
	m_list.SetRedraw(FALSE);
	const int rows = m_list.GetItemCount();
	const int want = static_cast<int>(m_order.size());
	if (rows == want)
	{
		for (int i = 0; i < want; i++)
			m_list.SetItemData(i, m_order[i]);
	}
	else
	{
		m_list.DeleteAllItems();
		for (size_t i = 0; i < m_order.size(); i++)
		{
			LVITEM lv = {};
			lv.mask = LVIF_TEXT | LVIF_PARAM;
			lv.iItem = static_cast<int>(i);
			lv.pszText = LPSTR_TEXTCALLBACK;
			lv.lParam = m_order[i];
			m_list.InsertItem(&lv);
		}
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate();
}

void CSearchOnDiskDlg::OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	int col = p->iSubItem;
	if (col == m_sort_column)
		m_sort_descending = !m_sort_descending;
	else
	{
		m_sort_column = col;
		// Date (newest first) + Size (biggest first) default to descending;
		// text columns A->Z.
		m_sort_descending = (col == 2 || col == 3);
	}
	apply_sort();
	repopulate_list();
	*pResult = 0;
}

void CSearchOnDiskDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
	int idx = static_cast<int>(pDispInfo->item.lParam);
	if (idx < 0 || idx >= static_cast<int>(m_results.size()))
	{
		pDispInfo->item.pszText = const_cast<char*>("");
		*pResult = 0;
		return;
	}
	const t_result& r = m_results[idx];
	std::wstring leaf, dir;
	split_path(r.full_path, leaf, dir);
	std::string& buffer = m_list.get_buffer();
	switch (pDispInfo->item.iSubItem)
	{
	case 0: // Name
		buffer = narrow(leaf);
		break;
	case 1: // Path
		buffer = narrow(dir);
		break;
	case 2: // Date
		buffer = format_mtime(r.mtime);
		break;
	case 3: // Size
		buffer = r.size_bytes > 0 ? theme::format_size(r.size_bytes) : std::string();
		break;
	case 4: // Type
		buffer = label_for_ext(r.full_path);
		break;
	default:
		buffer.clear();
		break;
	}
	pDispInfo->item.pszText = const_cast<char*>(buffer.c_str());
	*pResult = 0;
}

void CSearchOnDiskDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	open_selected();
	*pResult = 0;
}

void CSearchOnDiskDlg::open_selected()
{
	int idx = m_list.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (idx < 0)
		return;
	int data = static_cast<int>(m_list.GetItemData(idx));
	if (data < 0 || data >= static_cast<int>(m_results.size()))
		return;
	const t_result& r = m_results[data];
	std::string path = narrow(r.full_path);
	if (path.empty() || !m_main_frame)
		return;
	CXCCMixerView* target = m_prefer_right
		? m_main_frame->right_mix_pane()
		: m_main_frame->left_mix_pane();
	if (!target)
		target = m_main_frame->left_mix_pane();
	if (!target)
		return;
	target->close_all_locations();
	target->open_location_mix(path);
	EndDialog(IDOK);
}

void CSearchOnDiskDlg::OnDestroy()
{
	ETSLayoutDialog::OnDestroy();
	AfxGetApp()->WriteProfileString(m_reg_key, "filter", m_filename);
	AfxGetApp()->WriteProfileInt(m_reg_key, "ext_filter", m_ext_choice);
	AfxGetApp()->WriteProfileInt(m_reg_key, "sort_col", m_sort_column);
	AfxGetApp()->WriteProfileInt(m_reg_key, "sort_desc", m_sort_descending ? 1 : 0);
}

void CSearchOnDiskDlg::OnBnClickedCancel()
{
	// Restore default Close behavior. Without this stub the wizard-generated
	// ON_BN_CLICKED(IDCANCEL, ...) swallows the click and the Close button
	// (plus ESC, which also routes through IDCANCEL) does nothing.
	EndDialog(IDCANCEL);
}
