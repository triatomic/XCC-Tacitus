#pragma once

#include "ListCtrlEx.h"
#include "resource.h"

using namespace std;

class CSearchFileDlg : public ETSLayoutDialog
{
// Construction
public:
	void open_mix(int id);
	void add(string name, int mix_id, int file_id, int sub_mix_id = -1, const string& top_mix_path = "", long long size_bytes = 0);
	void find(Cmix_file& f, string file_name, string mix_name, int mix_id, int sub_mix_id = -1, const string& top_mix_path = "");
	void find(const map<int, t_index_entry>& t_map, const string& post, const string& dir);
	void set(CMainFrame* main_frame);
	CSearchFileDlg(CWnd* pParent = NULL);   // standard constructor

	string get_filename() const
	{
		return string(m_filename);
	}

// Dialog Data
	//{{AFX_DATA(CSearchFileDlg)
	enum { IDD = IDD_SEARCH_FILE };
	CListCtrlEx	m_list;
	CString	m_filename;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CSearchFileDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CSearchFileDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnFind();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnExtract();
	//}}AFX_MSG
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
private:
	void apply_sort();
	void repopulate_list();
	struct t_map_entry
	{
		string name;
		int id;
		int parent;
		int parent_parent;
		string top_mix_path;
		long long size_bytes = 0;
		int group_id = -1;       // LVGROUP id; assigned at search time
	};

	using t_map = map<int, t_map_entry>;

	CMainFrame* m_main_frame;
	t_map m_map;
	// Display order (m_map keys). Sort + repopulate operate on this; m_map
	// stays insertion-ordered so the m_sepindex left/right split (set in
	// OnFind) keeps working unmodified.
	vector<int> m_order;
	CString m_reg_key;
	int m_sepindex;
	int m_sort_column = 0;     // 0 = Source, 1 = File, 2 = Size
	bool m_sort_descending = false;
};
