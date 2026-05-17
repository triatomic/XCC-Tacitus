#pragma once

#include "ListCtrlEx.h"
#include "resource.h"

#include <atomic>

using namespace std;

class CSearchFileDlg : public ETSLayoutDialog
{
// Construction
public:
	// Promoted to public so the search-worker context (free namespace in
	// SearchFileDlg.cpp) can name the type. Layout / semantics unchanged.
	struct t_map_entry
	{
		string name;
		int id;
		int parent;
		int parent_parent;
		string top_mix_path;
		long long size_bytes = 0;
		int group_id = -1;       // LVGROUP id; assigned at search time
		bool predefined = false; // true = parent is a mix_map_list id (game MIX);
		                         // false = parent is a pane t_index_list id
	};

	void open_mix(int id);
	void add(string name, int mix_id, int file_id, int sub_mix_id = -1, const string& top_mix_path = "", long long size_bytes = 0, bool predefined = false);
	// `cancel` is checked at the top of each per-file iteration; pass nullptr
	// to disable cancellation. `mix_map` is a snapshot of MainFrame's
	// mix_map_list — passing one avoids touching MFC state from a worker
	// thread; nullptr means "read live from m_main_frame" (foreground only).
	void find(Cmix_file& f, string file_name, string mix_name, int mix_id, int sub_mix_id = -1, const string& top_mix_path = "", bool predefined = false, std::atomic<bool>* cancel = nullptr, const t_mix_map_list* mix_map = nullptr);
	void find(const map<int, t_index_entry>& t_map, const string& post, const string& dir, std::atomic<bool>* cancel = nullptr, const t_mix_map_list* mix_map = nullptr);
	void find_predefined(std::atomic<bool>* cancel = nullptr, const t_mix_map_list* mix_map = nullptr);
	void set(CMainFrame* main_frame, bool prefer_right = false);
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
	afx_msg LRESULT OnSearchProgress(WPARAM wp, LPARAM lp);
	afx_msg LRESULT OnSearchDone(WPARAM wp, LPARAM lp);
	afx_msg void OnTimer(UINT_PTR id);
	DECLARE_MESSAGE_MAP()
private:
	void apply_sort();
	void repopulate_list();
	void start_search();
	void finish_search();
	static UINT AFX_CDECL search_thread_proc(LPVOID p);

	using t_map = map<int, t_map_entry>;

	CMainFrame* m_main_frame;
	bool m_prefer_right = false;	// which pane to route predefined-game results into
	t_map m_map;
	// Display order (m_map keys). Sort + repopulate operate on this; m_map
	// stays insertion-ordered so the m_sepindex left/right split (set in
	// OnFind) keeps working unmodified.
	vector<int> m_order;
	CString m_reg_key;
	int m_sepindex;
	int m_sort_column = 0;     // 0 = Source, 1 = File, 2 = Size
	bool m_sort_descending = false;
	bool m_include_game_mixes = true;

	// Worker-thread search state. The walk runs on a background CWinThread
	// so the dialog stays responsive (drag/resize/cancel). The worker only
	// touches snapshots it owns (m_thread_ctx) plus the atomic cancel flag.
	// PROGRESS / DONE posts to the dialog are tagged with m_search_seq;
	// late posts from a cancelled or stale run are dropped.
	std::atomic<bool> m_cancel{false};
	bool m_searching = false;
	UINT m_search_seq = 0;
	CWinThread* m_worker = nullptr;
	CStatic m_progress;
};
