#include "stdafx.h"
#include "MainFrm.h"
#include "SelectPaletteDlg.h"

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
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

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
			<< itemGrowing(HORIZONTAL)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			);
	ETSLayoutDialog::OnInitDialog();
	m_list.InsertColumn(0, "Name");
	m_list.auto_size();
	insert_tree_entry(-1, TVI_ROOT);
	return true;
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
