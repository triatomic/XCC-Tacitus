#pragma once

#include "ListCtrlEx.h"
#include "resource.h"

#include <string>
#include <vector>

class CMainFrame;
class CXCCFileView;
class Cmix_file;

// Searchable palette picker. Replaces the unmanageably-tall TrackPopupMenu
// the Load PAL... button used to spawn (hundreds of entries scrolling off
// screen on PAL-rich MIXes like ra2.mix). Lists every palette already loaded
// into m_pal_list (game roots, PAL Paths, prior Loaded sessions) plus any
// PAL still living unloaded inside the file's source MIX. Selection
// previews live so the user can flip through candidates without committing;
// Cancel reverts to whatever palette was active at dialog open.
class CLoadPalDlg : public ETSLayoutDialog
{
public:
	CLoadPalDlg(CWnd* pParent = NULL);

	void set(CMainFrame* main_frame,
		CXCCFileView* view,
		Cmix_file* source_mix,
		const std::string& fname);

	enum { IDD = IDD_LOAD_PAL };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnFilterChange();
	afx_msg void OnBrowse();
	afx_msg void OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnColumnclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

private:
	struct pal_row
	{
		std::string name;       // display "conquer.pal"
		std::string source;     // "MIX: <name>" / parent-tree path / "Loaded" / "Disk"
		int match_score = 2;    // 0 = exact stem, 1 = 4-char prefix, 2 = other
		enum kind_t { kind_pal_list = 0, kind_mix_entry = 1 };
		kind_t kind = kind_pal_list;
		int pal_list_idx = -1;  // kind_pal_list: index into m_pal_list
		int mix_entry_id = -1;  // kind_mix_entry: id for m_source_mix->get_vdata(id)
	};

	void build_rows();
	void apply_filter_and_sort();
	void repopulate_list();
	void apply_row_live(int row_visible);
	bool apply_mix_entry(int mix_entry_id, const std::string& name);
	int  find_pal_list_idx_by_name(const std::string& name) const;
	std::string compute_source_label(int pal_list_idx) const;

	CListCtrlEx m_list;
	CEdit       m_filter_edit;
	CString     m_filter;

	std::vector<pal_row> m_rows;        // all rows (built once in OnInitDialog)
	std::vector<int>     m_visible;     // indices into m_rows, post-filter, sorted

	int  m_sort_column = -1;        // -1 = default ranking; >=0 = column override
	bool m_sort_descending = false;

	CMainFrame*    m_main_frame = nullptr;
	CXCCFileView*  m_view = nullptr;
	Cmix_file*     m_source_mix = nullptr;
	std::string    m_file_base;     // lowercase basename of m_fname (no ext) - ranking key
	std::string    m_file_display;  // name.ext (preserved case) - title-bar label

	int m_original_palette_i = -1;  // restore on Cancel
	bool m_in_live_apply = false;   // guard against re-entrant LVN_ITEMCHANGED
};
