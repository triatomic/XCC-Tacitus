#pragma once

#include <string>

// Optional theme hook. When set, CListCtrlEx::OnCustomDraw will pull row
// colors from this provider instead of using the hardcoded light-mode
// alternating rows. Each app (Mixer, XCCD2D, ...) installs its own provider
// at startup; Library code stays decoupled from any specific theme module.
struct CListCtrlEx_theme
{
	bool (*is_dark)();        // true => use dark colors; false => default light alternating rows
	COLORREF (*row_bg)();     // background for even rows (and odd rows in dark)
	COLORREF (*row_bg_alt)(); // background for odd rows (light mode); ignored in dark
	COLORREF (*text)();       // foreground text color (dark mode only)
	COLORREF (*grid)();       // grid line color (dark mode only); row + column separators
	bool (*show_grid)();      // when true, draw row + column separators in dark mode
};

void CListCtrlEx_set_theme(const CListCtrlEx_theme* hook);

class CListCtrlEx: public CListCtrl
{
public:
	//{{AFX_VIRTUAL(CListCtrlEx)
	public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	protected:
	virtual void PreSubclassWindow();
	//}}AFX_VIRTUAL
public:
	std::string& get_buffer();
	std::string get_selected_rows_tsv();
	void select_all();
	void DeleteAllColumns();
	DWORD GetItemData(int nItem) const;
	int InsertItemData(int nItem, DWORD dwData);
	int InsertItemData(DWORD dwData);
	void auto_size();
	void set_size(int width, int column = 0);
protected:
	//{{AFX_MSG(CListCtrlEx)
	afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
private:
	std::string m_buffer[4];
	int m_buffer_w;
};
