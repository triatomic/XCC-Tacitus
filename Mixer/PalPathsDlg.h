#pragma once

#include "ListCtrlEx.h"
#include "resource.h"

#include <string>
#include <vector>

// PAL Paths dialog: an ordered list of folders and archives that get scanned
// for .pal files at app startup, plus walked by Auto Select when the per-game
// palette list doesn't yield a hit. Higher entries take priority.
//
// On-disk shape (registry "PalPaths\path<i>", string):
//   "folder|<full path>"
//   "mix|<full path>"
class CPalPathsDlg : public ETSLayoutDialog
{
public:
	struct Entry
	{
		bool is_folder = true;  // false = mix archive
		std::string path;
	};

	CPalPathsDlg(CWnd* pParent = NULL);

	enum { IDD = IDD_PAL_PATHS };

	// Persist / load the entry list to the "PalPaths" registry section.
	// First-run seed is empty; users add their own folders/mixes explicitly.
	static std::vector<Entry> load_from_registry();
	static void save_to_registry(const std::vector<Entry>& entries);

	// "Override per-game palettes" toggle (PalPaths\override, default 0).
	// When on, Auto Select walks the user-loaded slice BEFORE the per-game
	// range, so a user-loaded "temperat.pal" wins over the stock RA2 one.
	static bool override_per_game();
	static void set_override_per_game(bool on);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	afx_msg void OnAddFolder();
	afx_msg void OnAddMix();
	afx_msg void OnRemove();
	afx_msg void OnMoveUp();
	afx_msg void OnMoveDown();
	afx_msg void OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	DECLARE_MESSAGE_MAP()

	CButton    m_override;
	CToolTipCtrl m_tooltip;

private:
	void rebuild_list();
	int  selected_index() const;
	void select_index(int i);
	void update_buttons();
	void insert_after_selection(const Entry& e);
	void resize_columns();
	afx_msg void OnSize(UINT nType, int cx, int cy);

	CListCtrlEx        m_list;
	CButton            m_remove;
	CButton            m_up;
	CButton            m_down;
	std::vector<Entry> m_entries;
};
