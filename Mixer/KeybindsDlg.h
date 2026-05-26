#pragma once

#include "ListCtrlEx.h"
#include "ETSLayout.h"
#include "resource.h"
#include "keybinds.h"

#include <vector>

class CKeybindsDlg : public ETSLayoutDialog
{
public:
	CKeybindsDlg(CWnd* pParent = NULL);

	enum { IDD = IDD_KEYBINDS };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	afx_msg void OnChangeKey();
	afx_msg void OnChangeMouse();
	afx_msg void OnClearKey();
	afx_msg void OnClearMouse();
	afx_msg void OnReset();
	afx_msg void OnResetAll();
	afx_msg void OnOpenIni();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnFilterChange();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()

private:
	void rebuild_list();
	void update_row(int row);
	// The m_working index for a given visible list row (stored in the row's
	// lParam, since filtering decouples row order from m_working order). -1 if
	// the row is invalid.
	int  working_index_at(int row) const;
	// Visible list row currently showing m_working[wi], or -1 if filtered out.
	int  row_for_working(int wi) const;
	int  selected_index() const;   // -> m_working index of the selected row
	void update_buttons();
	void resize_columns();
	void change_selected(bool mouse);
	bool has_conflict(int& a_out, int& b_out, bool& is_mouse_out) const;

	CListCtrlEx m_list;
	CEdit       m_filter;
	CButton     m_change_key;
	CButton     m_change_mouse;
	CButton     m_clear_key;
	CButton     m_clear_mouse;
	CButton     m_reset;
	CButton     m_reset_all;
	CButton     m_open_ini;

	std::vector<keybinds::Binding> m_working;
};
