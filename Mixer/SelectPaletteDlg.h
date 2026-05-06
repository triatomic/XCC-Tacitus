#pragma once

#include "ListCtrlEx.h"
#include "MainFrm.h"
#include "resource.h"

class CSelectPaletteDlg : public ETSLayoutDialog
{
public:
	void check_selection();
	void update_list(int parent_id, int current_palette);
	void insert_tree_entry(int parent_id, HTREEITEM parent_item);
	void set(CMainFrame* main_frame, t_pal_map_list pal_map_list, t_pal_list pal_list);
	CSelectPaletteDlg(CWnd* pParent = NULL);   // standard constructor

	int current_palette() const
	{
		return m_current_palette;
	}

	void current_palette(int v)
	{
		m_current_palette = v;
	}

	//{{AFX_DATA(CSelectPaletteDlg)
	enum { IDD = IDD_SELECT_PALETTE };
	CButton	m_ok;
	CTreeCtrl	m_tree;
	CListCtrlEx	m_list;
	//}}AFX_DATA


	//{{AFX_VIRTUAL(CSelectPaletteDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL
protected:
	//{{AFX_MSG(CSelectPaletteDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSelchangedTree(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	t_pal_map_list m_pal_map_list;
	t_pal_list m_pal_list;
	int m_current_palette;
	CMainFrame* m_main_frame;
};
