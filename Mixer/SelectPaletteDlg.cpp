#include "stdafx.h"
#include "MainFrm.h"
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
	CreateRoot(VERTICAL)
		<< (pane(HORIZONTAL, GREEDY)
			<< item(IDC_TREE, GREEDY)
			<< item(IDC_LIST, GREEDY)
			)
		<< (pane(HORIZONTAL, ABSOLUTE_VERT)
			<< item(IDC_LOAD_FOLDER, NORESIZE)
			<< item(IDC_LOAD_MIX, NORESIZE)
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	m_list.InsertColumn(0, "Name");
	m_list.auto_size();
	insert_tree_entry(-1, TVI_ROOT);
	theme::apply_dialog(GetSafeHwnd());
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
		if (m_main_frame->load_pal_mix(static_cast<std::string>(path)) >= 0)
			total++;
	}
	if (!total)
	{
		AfxMessageBox("No palettes found in the selected archive(s).", MB_OK | MB_ICONINFORMATION);
		return;
	}
	rebuild_tree();
}

void CSelectPaletteDlg::insert_tree_entry(int parent_id, HTREEITEM parent_item)
{
	CTreeCtrl& tc = m_tree;
	using t_sort_map = multimap<string, int>;
	t_sort_map sort_map;
	for (auto& i : m_pal_map_list)
	{
		if (i.second.parent == parent_id)
			sort_map.insert(t_sort_map::value_type(i.second.name, i.first));
	}
	for (auto& j : sort_map)
	{
		auto& i = *m_pal_map_list.find(j.second);
		HTREEITEM h = tc.InsertItem(i.second.name.c_str(), parent_item);
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
