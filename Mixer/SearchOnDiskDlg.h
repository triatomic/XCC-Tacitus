#pragma once

#include "ListCtrlEx.h"
#include "resource.h"

#include <memory>
#include <string>
#include <vector>

class CMainFrame;

class CSearchOnDiskDlg : public ETSLayoutDialog
{
public:
	struct t_result
	{
		std::wstring full_path;
		long long size_bytes = 0;
		long long mtime = 0;       // FILETIME packed as int64 (0 = none)
	};

	CSearchOnDiskDlg(CWnd* pParent = NULL);
	~CSearchOnDiskDlg();

	void set(CMainFrame* main_frame, bool prefer_right);

	enum { IDD = IDD_SEARCH_ON_DISK };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();

	afx_msg void OnFind();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	void apply_sort();
	void repopulate_list();
	void open_selected();

	CListCtrlEx m_list;
	CComboBox m_ext_filter;
	CString m_filename;
	CStatic m_progress;
	CString m_reg_key;
	int m_ext_choice = 0;      // index into the file-static ext_options[] array

	std::vector<t_result> m_results;
	std::vector<int> m_order;  // indices into m_results

	// IPC reply state. The reply window receives WM_COPYDATA from
	// Everything and stashes a copy of the payload here, then sets
	// m_reply_received to break the pump loop in OnFind().
	class CReplyWnd;
	friend class CReplyWnd;
	std::unique_ptr<CReplyWnd> m_reply_wnd;
	std::vector<BYTE> m_reply_buf;
	bool m_reply_received = false;

	int m_sort_column = 0;     // 0 = File, 1 = Type, 2 = Size, 3 = Modified, 4 = Path
	bool m_sort_descending = false;

	CMainFrame* m_main_frame = nullptr;
	bool m_prefer_right = false;
public:
	afx_msg void OnBnClickedCancel();
};
