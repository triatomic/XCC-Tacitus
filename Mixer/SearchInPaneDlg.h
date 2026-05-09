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
	afx_msg void OnFind();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRegexToggle();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	struct t_match
	{
		string name;
		long long size_bytes = 0;
		int id;
	};

	bool match_one(const string& name, const string& filter);
	void apply_sort();
	void repopulate_list();

	CXCCMixerView* m_pane = nullptr;
	vector<t_match> m_matches;
	CString m_reg_key;
	bool m_use_regex = false;
	CButton m_regex_btn;
	int m_sort_column = 0;     // 0 = Name, 1 = Size
	bool m_sort_descending = false;
};
