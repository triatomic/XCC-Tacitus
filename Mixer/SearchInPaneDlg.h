#pragma once

#include "ListCtrlEx.h"
#include "resource.h"

using namespace std;

class CXCCMixerView;

class CSearchInPaneDlg : public ETSLayoutDialog
{
public:
	void set(CXCCMixerView* pane);
	CSearchInPaneDlg(CWnd* pParent = NULL);

	enum { IDD = IDD_SEARCH_IN_PANE };
	CListCtrlEx m_list;
	CString m_filename;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnFilterChange();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	DECLARE_MESSAGE_MAP()

private:
	struct t_match
	{
		string name;
		long long size_bytes = 0;
		int id;
	};

	void populate_all();
	void apply_filter_and_sort();
	void repopulate_list();
	void activate_selected();

	CXCCMixerView* m_pane = nullptr;
	vector<t_match> m_all;     // every pane entry, snapshotted in OnInitDialog
	vector<int> m_visible;     // indices into m_all, post-filter, sorted
	CString m_reg_key;
	int m_sort_column = 0;     // 0 = Name, 1 = Size
	bool m_sort_descending = false;
};
