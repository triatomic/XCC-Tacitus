#pragma once

#include <map>
#include "resource.h"
#include "cc_structures.h"

class CDirectoriesDlg : public ETSLayoutDialog
{
// Construction
public:
	CDirectoriesDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CDirectoriesDlg)
	enum { IDD = IDD_DIRECTORIES };
	CString	m_edit_dune2;
	CString	m_edit_dune2000;
	CString	m_edit_ra2;
	CString	m_edit_ra;
	CString	m_edit_td_primary;
	CString	m_edit_td_secondary;
	CString	m_edit_ts;
	CString	m_edit_cd;
	CString	m_edit_data;
	CString	m_edit_rg;
	CString	m_edit_gr;
	CString	m_edit_gr_zh;
	CString	m_edit_nox;
	CString	m_edit_ebfd;
	CString	m_edit_bfme;
	CString	m_edit_tw;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDirectoriesDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	std::map<int, CString> m_last_committed;
	CToolTipCtrl m_tooltips;
	void populate_path_combo(int combo_id);
	void on_path_combo_change(int combo_id);

public:
	virtual BOOL PreTranslateMessage(MSG* pMsg) override;
protected:

	afx_msg void OnSelDune2();
	afx_msg void OnSelTdPrimary();
	afx_msg void OnSelTdSecondary();
	afx_msg void OnSelRa();
	afx_msg void OnSelDune2000();
	afx_msg void OnSelTs();
	afx_msg void OnSelRa2();
	afx_msg void OnSelRg();
	afx_msg void OnSelGr();
	afx_msg void OnSelGrZh();
	afx_msg void OnSelNox();
	afx_msg void OnSelEbfd();
	afx_msg void OnSelBfme();
	afx_msg void OnSelTw();
	afx_msg void OnSelData();
	afx_msg void OnSelCd();

	// Generated message map functions
	//{{AFX_MSG(CDirectoriesDlg)
	virtual void OnOK();
	afx_msg void OnResetCd();
	afx_msg void OnResetData();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};
