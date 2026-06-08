#include "stdafx.h"
#include "DirectoriesDlg.h"
#include "theme.h"

#include "xcc_dirs.h"
#include <afxdlgs.h>

// Posted from on_path_combo_change so the modal folder picker for "Custom..."
// opens after the combo's CBN_CLOSEUP fully settles (see OnCustomPick).
#define WM_DIR_CUSTOM_PICK (WM_USER + 0x120)
// Posted on CBN_SETFOCUS to clear the edit's auto-select highlight and park the
// caret at the end so long paths scroll their tail into view (see OnDeselectCombo).
#define WM_DIR_DESELECT (WM_USER + 0x121)

namespace
{
	struct DirRow { int combo_id; int static_id; t_game game; };
	const DirRow kRows[] =
	{
		{ IDC_DUNE2,        IDC_DUNE2_STATIC,        game_dune2    },
		{ IDC_TD_PRIMARY,   IDC_TD_PRIMARY_STATIC,   game_td       },
		{ IDC_TD_SECONDARY, IDC_TD_SECONDARY_STATIC, game_unknown  },
		{ IDC_RA,           IDC_RA_STATIC,           game_ra       },
		{ IDC_DUNE2000,     IDC_DUNE2000_STATIC,     game_dune2000 },
		{ IDC_TS,           IDC_TS_STATIC,           game_ts       },
		{ IDC_RA2,          IDC_RA2_STATIC,          game_ra2      },
		{ IDC_RG,           IDC_RG_STATIC,           game_rg       },
		{ IDC_GR,           IDC_GR_STATIC,           game_gr       },
		{ IDC_GR_ZH,        IDC_GR_ZH_STATIC,        game_gr_zh    },
		{ IDC_NOX,          IDC_NOX_STATIC,          game_nox      },
		{ IDC_EBFD,         IDC_EBFD_STATIC,         game_ebfd     },
		{ IDC_BFME,         IDC_BFME_STATIC,         game_bfme     },
		{ IDC_TW,           IDC_TW_STATIC,           game_tw       },
		{ IDC_DATA,         IDC_DATA_STATIC,         game_unknown  },
		{ IDC_CD,           IDC_CD_STATIC,           game_unknown  },
	};

	t_game row_game(int combo_id)
	{
		for (auto& r : kRows) if (r.combo_id == combo_id) return r.game;
		return game_unknown;
	}

	bool is_path_combo(int combo_id)
	{
		for (auto& r : kRows) if (r.combo_id == combo_id) return true;
		return false;
	}
}

CDirectoriesDlg::CDirectoriesDlg(CWnd* pParent /*=NULL*/)
	: ETSLayoutDialog(CDirectoriesDlg::IDD, pParent, "directories_dlg")
{
	//{{AFX_DATA_INIT(CDirectoriesDlg)
	m_edit_dune2 = xcc_dirs::get_dir(game_dune2).c_str();
	m_edit_td_primary = xcc_dirs::get_dir(game_td).c_str();
	m_edit_td_secondary = xcc_dirs::get_td_secondary_dir().c_str();
	m_edit_ra = xcc_dirs::get_dir(game_ra).c_str();
	m_edit_dune2000 = xcc_dirs::get_dir(game_dune2000).c_str();
	m_edit_ts = xcc_dirs::get_dir(game_ts).c_str();
	m_edit_ra2 = xcc_dirs::get_dir(game_ra2).c_str();
	m_edit_rg = xcc_dirs::get_dir(game_rg).c_str();
	m_edit_gr = xcc_dirs::get_dir(game_gr).c_str();
	m_edit_gr_zh = xcc_dirs::get_dir(game_gr_zh).c_str();
	m_edit_nox = xcc_dirs::get_dir(game_nox).c_str();
	m_edit_ebfd = xcc_dirs::get_dir(game_ebfd).c_str();
	m_edit_bfme = xcc_dirs::get_dir(game_bfme).c_str();
	m_edit_tw = xcc_dirs::get_dir(game_tw).c_str();
	m_edit_cd = xcc_dirs::get_cd_dir().c_str();
	m_edit_data = xcc_dirs::get_data_dir().c_str();
	//}}AFX_DATA_INIT
}

void CDirectoriesDlg::DoDataExchange(CDataExchange* pDX)
{
	ETSLayoutDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CDirectoriesDlg)
	DDX_CBString(pDX, IDC_DUNE2, m_edit_dune2);
	DDX_CBString(pDX, IDC_DUNE2000, m_edit_dune2000);
	DDX_CBString(pDX, IDC_RA2, m_edit_ra2);
	DDX_CBString(pDX, IDC_RA, m_edit_ra);
	DDX_CBString(pDX, IDC_TD_PRIMARY, m_edit_td_primary);
	DDX_CBString(pDX, IDC_TD_SECONDARY, m_edit_td_secondary);
	DDX_CBString(pDX, IDC_TS, m_edit_ts);
	DDX_CBString(pDX, IDC_RG, m_edit_rg);
	DDX_CBString(pDX, IDC_GR, m_edit_gr);
	DDX_CBString(pDX, IDC_GR_ZH, m_edit_gr_zh);
	DDX_CBString(pDX, IDC_NOX, m_edit_nox);
	DDX_CBString(pDX, IDC_EBFD, m_edit_ebfd);
	DDX_CBString(pDX, IDC_BFME, m_edit_bfme);
	DDX_CBString(pDX, IDC_TW, m_edit_tw);
	DDX_CBString(pDX, IDC_CD, m_edit_cd);
	DDX_CBString(pDX, IDC_DATA, m_edit_data);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CDirectoriesDlg, ETSLayoutDialog)
	//{{AFX_MSG_MAP(CDirectoriesDlg)
	ON_BN_CLICKED(IDC_RESET_CD, OnResetCd)
	ON_BN_CLICKED(IDC_RESET_DATA, OnResetData)
	//}}AFX_MSG_MAP
	ON_WM_CTLCOLOR()
	ON_CBN_CLOSEUP(IDC_DUNE2,        OnSelDune2)
	ON_CBN_CLOSEUP(IDC_TD_PRIMARY,   OnSelTdPrimary)
	ON_CBN_CLOSEUP(IDC_TD_SECONDARY, OnSelTdSecondary)
	ON_CBN_CLOSEUP(IDC_RA,           OnSelRa)
	ON_CBN_CLOSEUP(IDC_DUNE2000,     OnSelDune2000)
	ON_CBN_CLOSEUP(IDC_TS,           OnSelTs)
	ON_CBN_CLOSEUP(IDC_RA2,          OnSelRa2)
	ON_CBN_CLOSEUP(IDC_RG,           OnSelRg)
	ON_CBN_CLOSEUP(IDC_GR,           OnSelGr)
	ON_CBN_CLOSEUP(IDC_GR_ZH,        OnSelGrZh)
	ON_CBN_CLOSEUP(IDC_NOX,          OnSelNox)
	ON_CBN_CLOSEUP(IDC_EBFD,         OnSelEbfd)
	ON_CBN_CLOSEUP(IDC_BFME,         OnSelBfme)
	ON_CBN_CLOSEUP(IDC_TW,           OnSelTw)
	ON_CBN_CLOSEUP(IDC_DATA,         OnSelData)
	ON_CBN_CLOSEUP(IDC_CD,           OnSelCd)
	ON_MESSAGE(WM_DIR_CUSTOM_PICK,   OnCustomPick)
	ON_MESSAGE(WM_DIR_DESELECT,      OnDeselectCombo)
END_MESSAGE_MAP()

BOOL CDirectoriesDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	// When a path combo gains focus the edit auto-selects all its text (the
	// blue highlight band). Defer a deselect that also parks the caret at the
	// end so long paths scroll into view from the right.
	if (HIWORD(wParam) == CBN_SETFOCUS && is_path_combo(LOWORD(wParam)))
		PostMessage(WM_DIR_DESELECT, (WPARAM)LOWORD(wParam));
	return ETSLayoutDialog::OnCommand(wParam, lParam);
}

LRESULT CDirectoriesDlg::OnDeselectCombo(WPARAM wParam, LPARAM)
{
	CComboBox* cb = static_cast<CComboBox*>(GetDlgItem((int)wParam));
	if (!cb) return 0;
	// -1,0 removes the selection (clears the blue band); then park the caret at
	// the end so a long path scrolls its tail (the folder name) into view.
	cb->SetEditSel(-1, 0);
	CString s;
	cb->GetWindowText(s);
	cb->SetEditSel(s.GetLength(), s.GetLength());
	return 0;
}

void CDirectoriesDlg::populate_path_combo(int combo_id)
{
	CComboBox* cb = static_cast<CComboBox*>(GetDlgItem(combo_id));
	if (!cb) return;
	CString preserve;
	cb->GetWindowText(preserve);
	cb->ResetContent();
	const auto& sources = xcc_dirs::get_detected_sources(row_game(combo_id));
	for (size_t i = 0; i < sources.size(); ++i)
	{
		int idx = cb->AddString(sources[i].path.c_str());
		cb->SetItemDataPtr(idx, (void*)(uintptr_t)(i + 1));
	}
	int custom_idx = cb->AddString("Custom...");
	cb->SetItemDataPtr(custom_idx, (void*)0);
	cb->SetWindowText(preserve);
	m_last_committed[combo_id] = preserve;
}

void CDirectoriesDlg::on_path_combo_change(int combo_id)
{
	CComboBox* cb = static_cast<CComboBox*>(GetDlgItem(combo_id));
	if (!cb) return;
	int sel = cb->GetCurSel();
	if (sel < 0) return;
	uintptr_t tag = (uintptr_t)cb->GetItemDataPtr(sel);
	if (tag == 0)
	{
		// "Custom...": opening the modal folder picker synchronously here (still
		// inside the combo's CBN_CLOSEUP) is fragile -- after we return the combo
		// finishes its close-up and restamps the selected "Custom..." item text
		// over whatever we wrote. Defer the picker to a posted message so it runs
		// after the combo has fully settled, on a clean edit field.
		PostMessage(WM_DIR_CUSTOM_PICK, (WPARAM)combo_id);
	}
	else
	{
		const auto& sources = xcc_dirs::get_detected_sources(row_game(combo_id));
		size_t i = tag - 1;
		if (i >= sources.size()) return;
		cb->SetWindowText(sources[i].path.c_str());
		m_last_committed[combo_id] = sources[i].path.c_str();
	}
}

LRESULT CDirectoriesDlg::OnCustomPick(WPARAM wParam, LPARAM)
{
	int combo_id = (int)wParam;
	CComboBox* cb = static_cast<CComboBox*>(GetDlgItem(combo_id));
	if (!cb) return 0;
	// The combo has finished its close-up by now; drop the "Custom..." list
	// selection so nothing restamps over the path we are about to write.
	cb->SetCurSel(-1);
	CString seed = m_last_committed[combo_id];
	CFolderPickerDialog dlg(seed.IsEmpty() ? NULL : (LPCTSTR)seed, 0, this);
	if (dlg.DoModal() == IDOK)
	{
		CString chosen = dlg.GetPathName();
		if (!chosen.IsEmpty() && chosen.GetAt(chosen.GetLength() - 1) != '\\')
			chosen += '\\';
		cb->SetWindowText(chosen);
		cb->SetEditSel(chosen.GetLength(), chosen.GetLength());
		m_last_committed[combo_id] = chosen;
	}
	else
	{
		cb->SetWindowText(m_last_committed[combo_id]);
	}
	return 0;
}

void CDirectoriesDlg::OnSelDune2()        { on_path_combo_change(IDC_DUNE2); }
void CDirectoriesDlg::OnSelTdPrimary()    { on_path_combo_change(IDC_TD_PRIMARY); }
void CDirectoriesDlg::OnSelTdSecondary()  { on_path_combo_change(IDC_TD_SECONDARY); }
void CDirectoriesDlg::OnSelRa()           { on_path_combo_change(IDC_RA); }
void CDirectoriesDlg::OnSelDune2000()     { on_path_combo_change(IDC_DUNE2000); }
void CDirectoriesDlg::OnSelTs()           { on_path_combo_change(IDC_TS); }
void CDirectoriesDlg::OnSelRa2()          { on_path_combo_change(IDC_RA2); }
void CDirectoriesDlg::OnSelRg()           { on_path_combo_change(IDC_RG); }
void CDirectoriesDlg::OnSelGr()           { on_path_combo_change(IDC_GR); }
void CDirectoriesDlg::OnSelGrZh()         { on_path_combo_change(IDC_GR_ZH); }
void CDirectoriesDlg::OnSelNox()          { on_path_combo_change(IDC_NOX); }
void CDirectoriesDlg::OnSelEbfd()         { on_path_combo_change(IDC_EBFD); }
void CDirectoriesDlg::OnSelBfme()         { on_path_combo_change(IDC_BFME); }
void CDirectoriesDlg::OnSelTw()           { on_path_combo_change(IDC_TW); }
void CDirectoriesDlg::OnSelData()         { on_path_combo_change(IDC_DATA); }
void CDirectoriesDlg::OnSelCd()           { on_path_combo_change(IDC_CD); }

HBRUSH CDirectoriesDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(), pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return ETSLayoutDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CDirectoriesDlg::OnOK() 
{
	ETSLayoutDialog::OnOK();
	xcc_dirs::set_dir(game_dune2, static_cast<string>(m_edit_dune2));
	xcc_dirs::set_dir(game_td, static_cast<string>(m_edit_td_primary));
	xcc_dirs::set_td_secondary_dir(static_cast<string>(m_edit_td_secondary));
	xcc_dirs::set_dir(game_ra, static_cast<string>(m_edit_ra));
	xcc_dirs::set_dir(game_dune2000, static_cast<string>(m_edit_dune2000));
	xcc_dirs::set_dir(game_ts, static_cast<string>(m_edit_ts));
	xcc_dirs::set_dir(game_ra2, static_cast<string>(m_edit_ra2));
	xcc_dirs::set_dir(game_rg, static_cast<string>(m_edit_rg));
	xcc_dirs::set_dir(game_gr, static_cast<string>(m_edit_gr));
	xcc_dirs::set_dir(game_gr_zh, static_cast<string>(m_edit_gr_zh));
	xcc_dirs::set_dir(game_nox, static_cast<string>(m_edit_nox));
	xcc_dirs::set_dir(game_ebfd, static_cast<string>(m_edit_ebfd));
	xcc_dirs::set_dir(game_bfme, static_cast<string>(m_edit_bfme));
	xcc_dirs::set_dir(game_tw, static_cast<string>(m_edit_tw));
	xcc_dirs::set_cd_dir(static_cast<string>(m_edit_cd));
}

void CDirectoriesDlg::OnResetCd() 
{
	UpdateData(true);
	xcc_dirs::reset_cd_dir();
	m_edit_cd = xcc_dirs::get_cd_dir().c_str();
	UpdateData(false);
}

void CDirectoriesDlg::OnResetData() 
{
	UpdateData(true);
	xcc_dirs::reset_data_dir();
	m_edit_data = xcc_dirs::get_data_dir().c_str();
	UpdateData(false);
}

BOOL CDirectoriesDlg::OnInitDialog()
{
	ETSLayoutDialog::OnInitDialog();
	// Flashbang mitigation: build + theme the whole dialog with painting frozen
	// so the first frame the user sees is already dark (no light-mode flash).
	// apply_dialog runs while the window is still hidden so apply_titlebar's
	// hidden-frame discard fires. See theme.cpp "Dialog flashbang mitigation".
	SetRedraw(FALSE);
	CreateRoot(HORIZONTAL)
		<< (pane(VERTICAL, ABSOLUTE_VERT)
			<< item(IDC_DUNE2_STATIC, NORESIZE)
			<< item(IDC_TD_PRIMARY_STATIC, NORESIZE)
			<< item(IDC_TD_SECONDARY_STATIC, NORESIZE)
			<< item(IDC_RA_STATIC, NORESIZE)
			<< item(IDC_DUNE2000_STATIC, NORESIZE)
			<< item(IDC_TS_STATIC, NORESIZE)
			<< item(IDC_RA2_STATIC, NORESIZE)
			<< item(IDC_RG_STATIC, NORESIZE)
			<< item(IDC_GR_STATIC, NORESIZE)
			<< item(IDC_GR_ZH_STATIC, NORESIZE)
			<< item(IDC_NOX_STATIC, NORESIZE)
			<< item(IDC_EBFD_STATIC, NORESIZE)
			<< item(IDC_BFME_STATIC, NORESIZE)
			<< item(IDC_TW_STATIC, NORESIZE)
			<< item(IDC_DATA_STATIC, NORESIZE)
			<< item(IDC_CD_STATIC, NORESIZE)
			)
		<< (pane(VERTICAL, ABSOLUTE_VERT)
			<< item(IDC_DUNE2, ABSOLUTE_VERT)
			<< item(IDC_TD_PRIMARY, ABSOLUTE_VERT)
			<< item(IDC_TD_SECONDARY, ABSOLUTE_VERT)
			<< item(IDC_RA, ABSOLUTE_VERT)
			<< item(IDC_DUNE2000, ABSOLUTE_VERT)
			<< item(IDC_TS, ABSOLUTE_VERT)
			<< item(IDC_RA2, ABSOLUTE_VERT)
			<< item(IDC_RG, ABSOLUTE_VERT)
			<< item(IDC_GR, ABSOLUTE_VERT)
			<< item(IDC_GR_ZH, ABSOLUTE_VERT)
			<< item(IDC_NOX, ABSOLUTE_VERT)
			<< item(IDC_EBFD, ABSOLUTE_VERT)
			<< item(IDC_BFME, ABSOLUTE_VERT)
			<< item(IDC_TW, ABSOLUTE_VERT)
			<< item(IDC_DATA, ABSOLUTE_VERT)
			<< item(IDC_CD, ABSOLUTE_VERT)
			)
		<< (pane(VERTICAL, ABSOLUTE_VERT)
			<< item(IDOK, NORESIZE)
			<< item(IDCANCEL, NORESIZE)
			<< itemGrowing(VERTICAL)
			<< item(IDC_RESET_DATA, NORESIZE)
			<< item(IDC_RESET_CD, NORESIZE)
			);
	UpdateLayout();

	for (auto& r : kRows) populate_path_combo(r.combo_id);

	m_tooltips.Create(this, TTS_ALWAYSTIP);
	m_tooltips.SetMaxTipWidth(320);
	m_tooltips.SetDelayTime(TTDT_INITIAL, 400);
	m_tooltips.SetDelayTime(TTDT_AUTOPOP, 15000);
	m_tooltips.AddTool(GetDlgItem(IDC_RESET_DATA),
		"Reset the Data directory to the folder containing XCC Mixer.exe. "
		"That folder holds 'global mix database.dat', used to resolve file "
		"names inside MIX archives.");
	m_tooltips.AddTool(GetDlgItem(IDC_RESET_CD),
		"Re-scan drives for a CD-ROM and set the CD directory to its root. "
		"Used when a game's audio/movie MIX is read from disc instead of "
		"the install folder.");
	m_tooltips.Activate(TRUE);

	theme::apply_dialog(GetSafeHwnd());

	// Clear the combo-edit selection that SetWindowText leaves behind, parking
	// the caret at the end so long paths show their tail. Done after layout +
	// theming so nothing re-selects afterward.
	for (auto& r : kRows)
	{
		CComboBox* cb = static_cast<CComboBox*>(GetDlgItem(r.combo_id));
		if (cb) cb->SetEditSel(-1, 0);
	}

	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

	// Return FALSE + focus OK so MFC's default "focus first tabstop and select
	// all its text" doesn't re-highlight the first combo.
	if (CWnd* ok = GetDlgItem(IDOK))
		ok->SetFocus();
	return FALSE;
}

BOOL CDirectoriesDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltips.GetSafeHwnd())
		m_tooltips.RelayEvent(pMsg);
	return ETSLayoutDialog::PreTranslateMessage(pMsg);
}
