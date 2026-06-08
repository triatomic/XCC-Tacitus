#include "stdafx.h"
#include "PalPathsDlg.h"
#include "theme.h"

#include <cc_file.h>
#include <mix_file.h>

#include <afxdlgs.h>

namespace
{
	// Recursively count .pal files in a folder.
	int count_pals_in_folder(const std::string& folder)
	{
		std::string dir = folder;
		if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
			dir += '\\';
		WIN32_FIND_DATA fd;
		HANDLE h = FindFirstFile((dir + "*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE)
			return 0;
		int total = 0;
		do
		{
			const std::string name = fd.cFileName;
			if (name == "." || name == "..")
				continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				total += count_pals_in_folder(dir + name);
				continue;
			}
			if (name.size() < 4)
				continue;
			std::string ext = name.substr(name.size() - 4);
			for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
			if (ext == ".pal")
				total++;
		}
		while (FindNextFile(h, &fd));
		FindClose(h);
		return total;
	}

	// Recursively count palette entries inside a MIX archive.
	int count_pals_in_mix_handle(Cmix_file& f)
	{
		int total = 0;
		for (int i = 0; i < f.get_c_files(); i++)
		{
			const int id = f.get_id(i);
			switch (f.get_type(id))
			{
			case ft_pal:
				total++;
				break;
			case ft_mix:
			{
				Cmix_file g;
				if (!g.open(id, f))
					total += count_pals_in_mix_handle(g);
				break;
			}
			default:
				break;
			}
		}
		return total;
	}

	int count_pals_in_mix(const std::string& path)
	{
		Cmix_file f;
		if (f.open(path))
			return 0;
		return count_pals_in_mix_handle(f);
	}
}

CPalPathsDlg::CPalPathsDlg(CWnd* pParent)
	: ETSLayoutDialog(CPalPathsDlg::IDD, pParent, "pal_paths_dlg")
{
}

void CPalPathsDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PAL_PATHS_LIST, m_list);
	DDX_Control(pDX, IDC_PAL_PATHS_REMOVE, m_remove);
	DDX_Control(pDX, IDC_PAL_PATHS_UP, m_up);
	DDX_Control(pDX, IDC_PAL_PATHS_DOWN, m_down);
	DDX_Control(pDX, IDC_PAL_PATHS_OVERRIDE, m_override);
}

BEGIN_MESSAGE_MAP(CPalPathsDlg, ETSLayoutDialog)
	ON_BN_CLICKED(IDC_PAL_PATHS_ADD_FOLDER, OnAddFolder)
	ON_BN_CLICKED(IDC_PAL_PATHS_ADD_MIX, OnAddMix)
	ON_BN_CLICKED(IDC_PAL_PATHS_REMOVE, OnRemove)
	ON_BN_CLICKED(IDC_PAL_PATHS_UP, OnMoveUp)
	ON_BN_CLICKED(IDC_PAL_PATHS_DOWN, OnMoveDown)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_PAL_PATHS_LIST, OnItemchangedList)
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
END_MESSAGE_MAP()

void CPalPathsDlg::OnSize(UINT nType, int cx, int cy)
{
	ETSLayoutDialog::OnSize(nType, cx, cy);
	// After ETSLayout has resized our children, stretch the Path column so
	// it fills the remaining listview width. Otherwise the empty area to
	// the right of the last column shows uninitialized GDI pixels (the
	// streak artifact you saw in dark mode), because the listview only
	// repaints its column extents and the dialog-level invalidate doesn't
	// reach into the listview's empty trailing region.
	resize_columns();
}

void CPalPathsDlg::resize_columns()
{
	if (!m_list.GetSafeHwnd() || !m_list.GetHeaderCtrl())
		return;
	if (m_list.GetHeaderCtrl()->GetItemCount() < 2)
		return;
	CRect rc;
	m_list.GetClientRect(&rc);
	int col0 = m_list.GetColumnWidth(0);
	int rest = rc.Width() - col0;
	if (rest < 100) rest = 100;
	m_list.SetColumnWidth(1, rest);
}

HBRUSH CPalPathsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CPalPathsDlg::OnInitDialog()
{
	// Apply dark titlebar via DWM and suppress paint until everything is laid
	// out + themed + populated. Same flash mitigation as CSearchInPaneDlg —
	// without these the first paint shows light defaults for one frame before
	// apply_dialog's repaint catches up.
	theme::apply_titlebar(GetSafeHwnd());
	SetRedraw(FALSE);

	CreateRoot(VERTICAL)
		<< (pane(VERTICAL, GREEDY)
			<< item(IDC_STATIC, ABSOLUTE_VERT)
			<< item(IDC_PAL_PATHS_LIST, GREEDY)
			)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_PAL_PATHS_ADD_FOLDER, NORESIZE)
			<< item(IDC_PAL_PATHS_ADD_MIX, NORESIZE)
			<< item(IDC_PAL_PATHS_REMOVE, NORESIZE)
			<< item(IDC_PAL_PATHS_UP, NORESIZE)
			<< item(IDC_PAL_PATHS_DOWN, NORESIZE)
			<< item(IDC_PAL_PATHS_OVERRIDE, NORESIZE)
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();

	// Theme the listview background before columns/populate so its first paint
	// is already dark (avoids a one-frame white SysListView32 erase). Idempotent.
	theme::apply_listview(m_list.GetSafeHwnd());

	m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
	m_list.InsertColumn(0, "Type",  LVCFMT_LEFT, 60);
	m_list.InsertColumn(1, "Path",  LVCFMT_LEFT, 290);
	resize_columns();

	m_entries = load_from_registry();
	rebuild_list();
	if (!m_entries.empty())
		select_index(0);
	update_buttons();

	m_override.SetCheck(override_per_game() ? BST_CHECKED : BST_UNCHECKED);

	// Tooltip on the unlabeled checkbox. Multi-line via \r\n; SetMaxTipWidth
	// lets the tip wrap rather than running off-screen.
	if (m_tooltip.Create(this, TTS_ALWAYSTIP))
	{
		m_tooltip.AddTool(&m_override,
			"Override per-game palettes\r\n\r\n"
			"When on, Auto Select (Ctrl+Q) walks your PAL Paths list BEFORE the\r\n"
			"per-game palette ranges. A user-loaded palette with a matching name\r\n"
			"(e.g. temperat.pal) wins over the stock RA2/TS/etc. one.\r\n\r\n"
			"Off (default): per-game palettes are tried first; PAL Paths only\r\n"
			"acts as a fallback when no per-game match is found.");
		m_tooltip.SetMaxTipWidth(420);
		m_tooltip.Activate(TRUE);
	}

	theme::apply_dialog(GetSafeHwnd());

	// Release redraw + flush one fully-themed paint.
	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	return TRUE;
}

BOOL CPalPathsDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return ETSLayoutDialog::PreTranslateMessage(pMsg);
}

void CPalPathsDlg::OnOK()
{
	save_to_registry(m_entries);
	set_override_per_game(m_override.GetCheck() == BST_CHECKED);
	ETSLayoutDialog::OnOK();
}

void CPalPathsDlg::rebuild_list()
{
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (size_t i = 0; i < m_entries.size(); i++)
	{
		const Entry& e = m_entries[i];
		int row = m_list.InsertItem(static_cast<int>(i), e.is_folder ? "Folder" : "Mix");
		m_list.SetItemText(row, 1, e.path.c_str());
	}
	m_list.SetRedraw(TRUE);
}

int CPalPathsDlg::selected_index() const
{
	POSITION pos = m_list.GetFirstSelectedItemPosition();
	return pos ? m_list.GetNextSelectedItem(pos) : -1;
}

void CPalPathsDlg::select_index(int i)
{
	if (i < 0 || i >= m_list.GetItemCount())
		return;
	m_list.SetItemState(i, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
	m_list.EnsureVisible(i, FALSE);
}

void CPalPathsDlg::update_buttons()
{
	int i = selected_index();
	m_remove.EnableWindow(i >= 0);
	m_up.EnableWindow(i > 0);
	m_down.EnableWindow(i >= 0 && i < m_list.GetItemCount() - 1);
}

void CPalPathsDlg::OnItemchangedList(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	update_buttons();
	*pResult = 0;
}

// Insert a new entry below the currently-selected row (so new paths land
// at a predictable priority next to whatever the user was thinking about),
// or append to the end when nothing is selected.
void CPalPathsDlg::insert_after_selection(const Entry& e)
{
	int sel = selected_index();
	int insert_at = (sel >= 0) ? sel + 1 : static_cast<int>(m_entries.size());
	m_entries.insert(m_entries.begin() + insert_at, e);
	rebuild_list();
	select_index(insert_at);
	update_buttons();
}

void CPalPathsDlg::OnAddFolder()
{
	CFolderPickerDialog dlg(NULL, 0, this);
	if (dlg.DoModal() != IDOK)
		return;
	std::string path = static_cast<std::string>(dlg.GetPathName());
	if (path.empty())
		return;
	int n = count_pals_in_folder(path);
	if (n == 0)
	{
		std::string msg = "No .pal files found in:\n\n" + path
			+ "\n\nThe folder will be added to the list, but it won't contribute palettes to the tree until you put .pal files in it (subfolders are scanned).\n\nAdd anyway?";
		if (AfxMessageBox(msg.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}
	Entry e;
	e.is_folder = true;
	e.path = path;
	insert_after_selection(e);
}

void CPalPathsDlg::OnAddMix()
{
	const char* filter =
		"Archive files (*.mix;*.dat;*.pak;*.big;*.pkg;*.wsx)|*.mix;*.dat;*.pak;*.big;*.pkg;*.wsx|"
		"All files (*.*)|*.*||";
	CFileDialog dlg(TRUE, NULL, NULL,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_EXPLORER,
		filter, this);
	if (dlg.DoModal() != IDOK)
		return;
	std::string path = static_cast<std::string>(dlg.GetPathName());
	if (path.empty())
		return;
	int n = count_pals_in_mix(path);
	if (n == 0)
	{
		std::string msg = "No palette entries found in:\n\n" + path
			+ "\n\nThe archive will be added to the list, but it won't contribute palettes to the tree.\n\nAdd anyway?";
		if (AfxMessageBox(msg.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}
	Entry e;
	e.is_folder = false;
	e.path = path;
	insert_after_selection(e);
}

void CPalPathsDlg::OnRemove()
{
	int i = selected_index();
	if (i < 0 || i >= static_cast<int>(m_entries.size()))
		return;
	m_entries.erase(m_entries.begin() + i);
	rebuild_list();
	if (!m_entries.empty())
		select_index(std::min(i, static_cast<int>(m_entries.size()) - 1));
	update_buttons();
}

void CPalPathsDlg::OnMoveUp()
{
	int i = selected_index();
	if (i <= 0 || i >= static_cast<int>(m_entries.size()))
		return;
	std::swap(m_entries[i], m_entries[i - 1]);
	rebuild_list();
	select_index(i - 1);
	update_buttons();
}

void CPalPathsDlg::OnMoveDown()
{
	int i = selected_index();
	if (i < 0 || i >= static_cast<int>(m_entries.size()) - 1)
		return;
	std::swap(m_entries[i], m_entries[i + 1]);
	rebuild_list();
	select_index(i + 1);
	update_buttons();
}

// ---------- Registry persistence ----------

std::vector<CPalPathsDlg::Entry> CPalPathsDlg::load_from_registry()
{
	std::vector<Entry> out;
	CWinApp* app = AfxGetApp();
	for (int i = 0; ; i++)
	{
		char key[32];
		std::snprintf(key, sizeof key, "path%d", i);
		CString v = app->GetProfileString("PalPaths", key, "");
		if (v.IsEmpty())
			break;
		std::string s = static_cast<std::string>(v);
		size_t bar = s.find('|');
		if (bar == std::string::npos)
			continue;
		Entry e;
		e.is_folder = s.compare(0, bar, "folder") == 0;
		e.path = s.substr(bar + 1);
		if (!e.path.empty())
			out.push_back(e);
	}
	// No default seed. The dialog opens empty on first run; users add their
	// own folders/mixes explicitly. Per-game palettes (Red Alert 2, etc.)
	// are still loaded from xcc_dirs::get_dir(...) by find_mixs() in
	// initialize_lists() — that's separate from PalPaths.
	return out;
}

void CPalPathsDlg::save_to_registry(const std::vector<Entry>& entries)
{
	CWinApp* app = AfxGetApp();
	// Wipe every existing pathN value first, so the registry exactly mirrors
	// the new list order. Without this, two failure modes:
	//  1) Shrinking the list from N to M<N leaves stale path[M..N-1] values
	//     that load_from_registry() would still pick up next session.
	//  2) Reordering rewrites path0..pathN-1 in the new order, which is fine
	//     in itself — but if a previous save had MORE entries than the
	//     current one, the leftover trailing keys would re-attach as
	//     phantom palette paths. Clearing first guarantees the on-disk
	//     ordering is exactly the dialog's ordering, top-to-bottom.
	for (int i = 0; ; i++)
	{
		char key[32];
		std::snprintf(key, sizeof key, "path%d", i);
		CString existing = app->GetProfileString("PalPaths", key, "");
		if (existing.IsEmpty())
			break;
		app->WriteProfileString("PalPaths", key, NULL);
	}
	for (size_t i = 0; i < entries.size(); i++)
	{
		char key[32];
		std::snprintf(key, sizeof key, "path%d", static_cast<int>(i));
		std::string v = (entries[i].is_folder ? "folder|" : "mix|") + entries[i].path;
		app->WriteProfileString("PalPaths", key, v.c_str());
	}
}

bool CPalPathsDlg::override_per_game()
{
	return AfxGetApp()->GetProfileInt("PalPaths", "override", 0) != 0;
}

void CPalPathsDlg::set_override_per_game(bool on)
{
	AfxGetApp()->WriteProfileInt("PalPaths", "override", on ? 1 : 0);
}
