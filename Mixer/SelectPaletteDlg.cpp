#include "stdafx.h"
#include "MainFrm.h"
#include "PalPathsDlg.h"
#include "SelectPaletteDlg.h"
#include "theme.h"

CSelectPaletteDlg::CSelectPaletteDlg(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(CSelectPaletteDlg::IDD, pParent, "select_palette_dlg")
{
	//{{AFX_DATA_INIT(CSelectPaletteDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	m_current_palette = -1;
}

void CSelectPaletteDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CSelectPaletteDlg)
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDC_TREE, m_tree);
	DDX_Control(pDX, IDC_LIST, m_list);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CSelectPaletteDlg, ETSLayoutDialog)
	//{{AFX_MSG_MAP(CSelectPaletteDlg)
	ON_NOTIFY(TVN_SELCHANGED, IDC_TREE, OnSelchangedTree)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST, OnItemchangedList)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST, OnDblclkList)
	ON_BN_CLICKED(IDC_LOAD_FOLDER, OnLoadFolder)
	ON_BN_CLICKED(IDC_LOAD_MIX, OnLoadMix)
	ON_BN_CLICKED(IDC_PAL_PATHS_BUTTON, OnPalPathsButton)
	//}}AFX_MSG_MAP
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

HBRUSH CSelectPaletteDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSelectPaletteDlg::set(CMainFrame* main_frame, t_pal_map_list pal_map_list, t_pal_list pal_list)
{
	m_main_frame = main_frame; 
	m_pal_map_list  = pal_map_list;
	m_pal_list = pal_list;
}

BOOL CSelectPaletteDlg::OnInitDialog()
{
	// Apply dark titlebar via DWM and suppress paint until everything is laid
	// out + themed + populated. Same flash mitigation as CSearchInPaneDlg —
	// without these the first paint shows light defaults for one frame before
	// apply_dialog's repaint catches up.
	theme::apply_titlebar(GetSafeHwnd());
	SetRedraw(FALSE);

	CreateRoot(VERTICAL)
		<< (pane(HORIZONTAL, GREEDY)
			<< item(IDC_TREE, GREEDY)
			<< item(IDC_LIST, GREEDY)
			)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_LOAD_FOLDER, NORESIZE)
			<< item(IDC_LOAD_MIX, NORESIZE)
			<< item(IDC_PAL_PATHS_BUTTON, NORESIZE)
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	// Theme the list + tree backgrounds before populate so their first paint is
	// already dark (avoids a one-frame white SysListView32 / SysTreeView32 erase
	// inside the frozen dialog). apply_listview is idempotent; the tree gets its
	// full treatment from the apply_dialog child-walk below.
	theme::apply_listview(m_list.GetSafeHwnd());
	if (theme::is_dark() && m_tree.GetSafeHwnd())
		TreeView_SetBkColor(m_tree.GetSafeHwnd(), theme::bg());
	m_list.InsertColumn(0, "Name");
	m_list.auto_size();
	insert_tree_entry(-1, TVI_ROOT);
	theme::apply_dialog(GetSafeHwnd());

	// Release redraw + flush one fully-themed paint.
	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	return true;
}

void CSelectPaletteDlg::rebuild_tree()
{
	m_tree.DeleteAllItems();
	m_pal_map_list = m_main_frame->pal_map_list_mut();
	m_pal_list = m_main_frame->pal_list_mut();
	insert_tree_entry(-1, TVI_ROOT);
}

void CSelectPaletteDlg::OnLoadFolder()
{
	CFolderPickerDialog dlg(NULL, 0, this);
	if (dlg.DoModal() != IDOK)
		return;
	string folder = static_cast<string>(dlg.GetPathName());
	if (folder.empty())
		return;
	int parent_id = m_main_frame->load_pal_folder(folder);
	if (parent_id < 0)
	{
		AfxMessageBox("No .pal files found in that folder.", MB_OK | MB_ICONINFORMATION);
		return;
	}
	// Mark as session-only so reload_pal_paths() (triggered by the PAL
	// Paths editor) preserves it instead of wiping it.
	m_main_frame->pal_map_list_mut()[parent_id].session_only = true;
	rebuild_tree();
}

void CSelectPaletteDlg::OnLoadMix()
{
	// Pick one or more archive files. Each selected archive is opened via
	// load_pal_mix, which recurses into nested MIXes and adds a top-level
	// tree node per archive. Multi-select via Ctrl+click / Shift+click /
	// Ctrl+A in the file dialog. Empty archives (no palettes) are skipped.
	//
	// We size m_filter_buf large enough to hold the picker's null-separated
	// list of leaf filenames after the chosen directory; CFileDialog parses
	// this buffer in-place via OFN_ALLOWMULTISELECT.
	const char* filter =
		"Archive files (*.mix;*.dat;*.pak;*.big;*.pkg;*.wsx)|*.mix;*.dat;*.pak;*.big;*.pkg;*.wsx|"
		"All files (*.*)|*.*||";
	CFileDialog dlg(TRUE, NULL, NULL,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER,
		filter, this);
	// Roomy buffer for the multi-select reply: <dir>\0<file1>\0<file2>\0...\0\0.
	const size_t buf_size = 64 * 1024;
	std::vector<char> buf(buf_size, 0);
	dlg.m_ofn.lpstrFile = buf.data();
	dlg.m_ofn.nMaxFile = static_cast<DWORD>(buf_size);
	if (dlg.DoModal() != IDOK)
		return;
	int total = 0;
	POSITION pos = dlg.GetStartPosition();
	while (pos)
	{
		CString path = dlg.GetNextPathName(pos);
		if (path.IsEmpty())
			continue;
		int root = m_main_frame->load_pal_mix(static_cast<std::string>(path));
		if (root >= 0)
		{
			// Session-only: preserved across reload_pal_paths(), not written
			// to PalPaths registry.
			m_main_frame->pal_map_list_mut()[root].session_only = true;
			total++;
		}
	}
	if (!total)
	{
		AfxMessageBox("No palettes found in the selected archive(s).", MB_OK | MB_ICONINFORMATION);
		return;
	}
	rebuild_tree();
}

void CSelectPaletteDlg::OnPalPathsButton()
{
	CPalPathsDlg dlg(this);
	if (dlg.DoModal() != IDOK)
		return;
	// Reload the user-loaded palette slice from the new registry list. The
	// per-game ranges (m_pal_i[]) are not touched.
	m_main_frame->reload_pal_paths();
	rebuild_tree();
}

void CSelectPaletteDlg::insert_tree_entry(int parent_id, HTREEITEM parent_item)
{
	CTreeCtrl& tc = m_tree;
	// Sort key: (order, name). User-PAL-path roots have order=0..N-1 and
	// appear first in dialog order. Game roots and all nested nodes have
	// order=-1, mapped to INT_MAX so they sort after the user roots and
	// fall back to alphabetical among themselves.
	using t_sort_map = multimap<std::pair<int, string>, int>;
	t_sort_map sort_map;
	for (auto& i : m_pal_map_list)
	{
		if (i.second.parent != parent_id)
			continue;
		int order = i.second.order < 0 ? INT_MAX : i.second.order;
		sort_map.insert(t_sort_map::value_type(std::make_pair(order, i.second.name), i.first));
	}
	for (auto& j : sort_map)
	{
		auto& i = *m_pal_map_list.find(j.second);
		string display_name = (i.second.session_only && parent_id == -1)
			? "[M] " + i.second.name : i.second.name;
		HTREEITEM h = tc.InsertItem(display_name.c_str(), parent_item);
		tc.SetItemData(h, i.first);
		insert_tree_entry(i.first, h);
		if (m_current_palette != -1 && m_pal_list[m_current_palette].parent == i.first)
			tc.SelectItem(h);
	}
}

void CSelectPaletteDlg::update_list(int parent_id, int current_palette)
{
	CListCtrl& lc = m_list;
	lc.DeleteAllItems();
	for (auto& i : m_pal_list)
	{
		if (i.parent != parent_id)
			continue;
		string name = i.name;
		{	
			int i = name.rfind(" - ");
			if (i != string::npos)
				name = name.substr(i + 3);
		}
		int index = lc.InsertItem(lc.GetItemCount(), name.c_str());
		lc.SetItemData(index, &i - m_pal_list.data());
		if (current_palette == &i - m_pal_list.data())
			lc.SetItemState(index, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
	}
	check_selection();
}

void CSelectPaletteDlg::OnSelchangedTree(NMHDR* pNMHDR, LRESULT* pResult) 
{
	NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
	if (~pNMTreeView->itemOld.state & TVIS_SELECTED && pNMTreeView->itemNew.state & TVIS_SELECTED)
		update_list(pNMTreeView->itemNew.lParam, m_current_palette);
	*pResult = 0;
}

void CSelectPaletteDlg::OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	CListCtrl& lc = m_list;
	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;
	m_current_palette = pNMListView->uNewState & LVIS_FOCUSED ? lc.GetItemData(pNMListView->iItem) : -1;
	if (m_current_palette != -1)
		m_main_frame->set_palette(m_current_palette);
	check_selection();
	*pResult = 0;
}

void CSelectPaletteDlg::check_selection()
{
	m_ok.EnableWindow(m_current_palette != -1);
}

void CSelectPaletteDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	if (m_current_palette != -1)
		EndDialog(IDOK);	
	*pResult = 0;
}
