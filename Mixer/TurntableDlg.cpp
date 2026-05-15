#include "stdafx.h"
#include "TurntableDlg.h"
#include "theme.h"

BEGIN_MESSAGE_MAP(CTurntableDlg, CDialog)
	ON_WM_CTLCOLOR()
	ON_CBN_SELCHANGE(IDC_TT_ANIM_COMBO, OnAnimChanged)
	ON_CBN_SELCHANGE(IDC_TT_FORMAT_COMBO, OnFormatChanged)
END_MESSAGE_MAP()

CTurntableDlg::CTurntableDlg(bool hva_available, int hva_frame_count, int ss, CWnd* parent,
	bool is_shp, int shp_max_frames)
	: CDialog(IDD_VXL_TURNTABLE, parent)
	, m_hva_available(hva_available)
	, m_hva_cf(hva_frame_count)
	, m_ss(ss < 1 ? 1 : ss)
	, m_is_shp(is_shp)
	, m_shp_max_frames(shp_max_frames)
{
	// SS=1 means there's nothing to downscale; force ds_full so the capture
	// loop short-circuits the resample. ds_native remains the default for
	// SS>1 because the user asked for "make GIF look better" which is
	// exactly the render-high / output-low trick.
	if (m_ss <= 1)
		m_downscale = ds_full;
	if (m_is_shp)
	{
		// SHP/WSA: capture every native frame by default. Rotation, HVA, and
		// downscale don't apply — handler bypasses them; dialog hides them.
		if (m_shp_max_frames >= 1)
			m_frames = m_shp_max_frames;
		m_downscale = ds_full;
	}
	// Default frames: when an HVA is loaded, prefer covering the full HVA
	// timeline so combined mode doesn't undersample animation. Cap at 360
	// (matches the dialog spinner range) and round up to a multiple of 4 so
	// rotation steps land on clean 90deg multiples by default.
	if (hva_available && hva_frame_count >= 4)
	{
		int n = hva_frame_count;
		n = (n + 3) & ~3;
		if (n > 360) n = 360;
		if (n < 36)  n = 36;
		m_frames = n;
	}
}

void CTurntableDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_TT_FRAMES_EDIT, m_frames);
	if (m_is_shp)
		DDV_MinMaxInt(pDX, m_frames, 1, m_shp_max_frames > 0 ? m_shp_max_frames : 1);
	else
		DDV_MinMaxInt(pDX, m_frames, 4, 360);
	DDX_Text(pDX, IDC_TT_DELAY_EDIT, m_delay_cs);
	DDV_MinMaxInt(pDX, m_delay_cs, 1, 1000);

	if (pDX->m_bSaveAndValidate)
	{
		CComboBox* fmt = (CComboBox*)GetDlgItem(IDC_TT_FORMAT_COMBO);
		CComboBox* anim = (CComboBox*)GetDlgItem(IDC_TT_ANIM_COMBO);
		if (fmt)  m_format = fmt->GetCurSel();
		if (anim) m_anim   = anim->GetCurSel();
		m_dir_cw = IsDlgButtonChecked(IDC_TT_DIR_CW) == BST_CHECKED;
		// Downscale combo's selection -> ds_* via the per-item DWORD payload
		// stashed at populate time. Using SetItemData/GetItemData avoids a
		// parallel index map and keeps the SS-conditional contents safe.
		if (CComboBox* ds = (CComboBox*)GetDlgItem(IDC_TT_DOWNSCALE_COMBO))
		{
			int sel = ds->GetCurSel();
			if (sel >= 0)
				m_downscale = static_cast<int>(ds->GetItemData(sel));
		}
		m_transparent_pal0 = IsDlgButtonChecked(IDC_TT_TRANSPARENT_PAL0) == BST_CHECKED;
	}
}

BOOL CTurntableDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	theme::apply_dialog(GetSafeHwnd());

	// SHP/WSA mode: rotation, animation source, and downscale don't apply.
	// Hide those rows entirely (cleaner than disabling) and update labels +
	// caption so the dialog reads as a frame-capture for SHPs instead of a
	// turntable for VXLs.
	if (m_is_shp)
	{
		SetWindowText("Record Frames");
		const int hide_ids[] = {
			IDC_TT_DIR_LABEL, IDC_TT_DIR_CW, IDC_TT_DIR_CCW,
			IDC_TT_ANIM_LABEL, IDC_TT_ANIM_COMBO,
			IDC_TT_DOWNSCALE_LABEL, IDC_TT_DOWNSCALE_COMBO,
		};
		for (int id : hide_ids)
			if (CWnd* w = GetDlgItem(id)) w->ShowWindow(SW_HIDE);
		// Initial state of the transparency checkbox.
		CheckDlgButton(IDC_TT_TRANSPARENT_PAL0, m_transparent_pal0 ? BST_CHECKED : BST_UNCHECKED);
		// Update the frames label so the new max range is obvious to the user.
		CString s;
		s.Format("Frames (1-%d):", m_shp_max_frames > 0 ? m_shp_max_frames : 1);
		if (CWnd* w = GetDlgItem(IDC_TT_FRAMES_LABEL)) w->SetWindowText(s);
		if (CWnd* w = GetDlgItem(IDC_TT_HINT))
			w->SetWindowText("Captures the SHP/WSA frames in order from frame 0. "
				"Side color, BG, shadows, palette all honored.");
	}
	else
	{
		// VXL doesn't get the SHP-specific transparency override (current
		// design call). Hide the checkbox so it doesn't confuse VXL users.
		if (CWnd* w = GetDlgItem(IDC_TT_TRANSPARENT_PAL0)) w->ShowWindow(SW_HIDE);
	}

	// Format combo: GIF first (single animated file is the more useful default
	// for sharing), PNG sequence second.
	if (CComboBox* fmt = (CComboBox*)GetDlgItem(IDC_TT_FORMAT_COMBO))
	{
		fmt->AddString("Animated GIF");
		fmt->AddString("PNG sequence (folder)");
		fmt->SetCurSel(m_format);
		theme::subclass_combobox(fmt->GetSafeHwnd());
		COMBOBOXINFO cbi = {}; cbi.cbSize = sizeof(cbi);
		if (::GetComboBoxInfo(fmt->GetSafeHwnd(), &cbi))
		{
			if (cbi.hwndList) theme::apply_window(cbi.hwndList);
			if (cbi.hwndItem) theme::apply_window(cbi.hwndItem);
		}
	}

	// Animation combo: HVA-only / Combined are added but disabled when no HVA
	// is loaded so the user can see the modes exist (discoverability) without
	// being able to pick something that won't work.
	if (CComboBox* anim = (CComboBox*)GetDlgItem(IDC_TT_ANIM_COMBO))
	{
		anim->AddString("Rotation only");
		anim->AddString(m_hva_available ? "HVA only" : "HVA only (no HVA loaded)");
		anim->AddString(m_hva_available ? "Combined (rotation + HVA)" : "Combined (no HVA loaded)");
		if (!m_hva_available) m_anim = anim_rotation;
		anim->SetCurSel(m_anim);
		anim->EnableWindow(m_hva_available ? TRUE : FALSE);
		theme::subclass_combobox(anim->GetSafeHwnd());
		COMBOBOXINFO cbi = {}; cbi.cbSize = sizeof(cbi);
		if (::GetComboBoxInfo(anim->GetSafeHwnd(), &cbi))
		{
			if (cbi.hwndList) theme::apply_window(cbi.hwndList);
			if (cbi.hwndItem) theme::apply_window(cbi.hwndItem);
		}
	}

	// Downscale combo: contents depend on the current SS multiplier. We
	// stash each entry's ds_* value via SetItemData so the visible label
	// can include the resolved pixel size ("Native (1x = 64x64)") without
	// breaking the index <-> mode mapping. Order: smallest to largest.
	if (CComboBox* ds = (CComboBox*)GetDlgItem(IDC_TT_DOWNSCALE_COMBO))
	{
		ds->ResetContent();
		auto add = [&](const char* label, int mode)
		{
			int idx = ds->AddString(label);
			if (idx >= 0) ds->SetItemData(idx, static_cast<DWORD_PTR>(mode));
		};
		if (m_ss <= 1)
		{
			// Nothing to downscale. Keep the combobox visible (so the user
			// sees what would be available with SS on) but with one entry.
			add("Native (SS off; output unchanged)", ds_full);
			ds->SetCurSel(0);
			ds->EnableWindow(FALSE);
		}
		else
		{
			CString s;
			s.Format("Native (1x, SS=%d -> 1)", m_ss);
			add(s, ds_native);
			if (m_ss >= 4)
			{
				s.Format("Half SS (%d -> %d)", m_ss, m_ss / 2);
				add(s, ds_half);
			}
			s.Format("Full SS (%dx, no downscale)", m_ss);
			add(s, ds_full);
			// Pick the index whose stored mode matches m_downscale; fall
			// back to ds_native (index 0) if the mode isn't in this list.
			int sel = 0;
			for (int i = 0; i < ds->GetCount(); i++)
			{
				if (static_cast<int>(ds->GetItemData(i)) == m_downscale)
				{
					sel = i;
					break;
				}
			}
			ds->SetCurSel(sel);
		}
		theme::subclass_combobox(ds->GetSafeHwnd());
		COMBOBOXINFO cbi = {}; cbi.cbSize = sizeof(cbi);
		if (::GetComboBoxInfo(ds->GetSafeHwnd(), &cbi))
		{
			if (cbi.hwndList) theme::apply_window(cbi.hwndList);
			if (cbi.hwndItem) theme::apply_window(cbi.hwndItem);
		}
	}

	CheckRadioButton(IDC_TT_DIR_CW, IDC_TT_DIR_CCW,
		m_dir_cw ? IDC_TT_DIR_CW : IDC_TT_DIR_CCW);

	if (CSpinButtonCtrl* s = (CSpinButtonCtrl*)GetDlgItem(IDC_TT_FRAMES_SPIN))
	{
		if (m_is_shp)
			s->SetRange32(1, m_shp_max_frames > 0 ? m_shp_max_frames : 1);
		else
			s->SetRange32(4, 360);
		s->SetBuddy(GetDlgItem(IDC_TT_FRAMES_EDIT));
		s->SetPos(m_frames);
	}
	if (CSpinButtonCtrl* s = (CSpinButtonCtrl*)GetDlgItem(IDC_TT_DELAY_SPIN))
	{
		s->SetRange32(1, 1000);
		s->SetBuddy(GetDlgItem(IDC_TT_DELAY_EDIT));
		s->SetPos(m_delay_cs);
	}

	update_enabled_state();

	// Tooltips. Created last so the wrap setting + tools register in the
	// usual order; relayed via PreTranslateMessage like VxlLightingDlg does.
	m_tooltips.Create(this, TTS_ALWAYSTIP);
	m_tooltips.SetMaxTipWidth(320);
	m_tooltips.SetDelayTime(TTDT_INITIAL, 400);
	m_tooltips.SetDelayTime(TTDT_AUTOPOP, 15000);
	m_tooltips.AddTool(GetDlgItem(IDC_TT_FRAMES_EDIT),
		"Total number of frames to capture. The voxel rotates this many "
		"equal-sized steps to complete a full 360\xb0 turn. More frames = "
		"smoother spin and larger output. Range 4..360.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_FRAMES_SPIN),
		"Total number of frames to capture. More frames = smoother spin "
		"and larger output. Range 4..360.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_FORMAT_COMBO),
		"Output format.\r\n"
		"  Animated GIF \x97 single file, plays in browsers / image viewers. "
		"Quantized to 256 colors per frame (effectively lossless for VXL).\r\n"
		"  PNG sequence \x97 numbered PNGs in a folder. Lossless with full "
		"alpha. Assemble in ffmpeg / DaVinci / etc. for video.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_DIR_CW),
		"Rotate clockwise as seen from the camera (the voxel's right side "
		"comes toward you first).");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_DIR_CCW),
		"Rotate counter-clockwise as seen from the camera (the voxel's left "
		"side comes toward you first).");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_ANIM_COMBO),
		"What to capture.\r\n"
		"  Rotation only \x97 spin the voxel 360\xb0, HVA frame stays frozen "
		"on the current frame.\r\n"
		"  HVA only \x97 play the loaded HVA timeline in place, no rotation.\r\n"
		"  Combined \x97 advance both together: each frame steps yaw and HVA "
		"frame at the same time. HVA-loaded VXLs only.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_DELAY_EDIT),
		"How long each frame is shown before the next one, in centiseconds "
		"(1 cs = 10 ms = 1/100 s). 5 = 50 ms = 20 fps. Lower = faster spin. "
		"GIF only \x97 PNG sequence has no embedded timing.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_DELAY_SPIN),
		"Frame delay in centiseconds (1 cs = 10 ms). 5 = 20 fps. GIF only.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_DOWNSCALE_COMBO),
		"Resample each captured frame before writing to disk. With "
		"supersampling on, the splat is rendered larger than the voxel's "
		"native size for cleaner edges \x97 downscaling bakes that cleanup "
		"into a smaller, smoother output (the classic 'render high, output "
		"low' trick).\r\n"
		"  Native \x97 logical voxel size. Smallest file, recommended for GIF.\r\n"
		"  Half SS \x97 halfway between native and full. Available at SS \x3e= 4.\r\n"
		"  Full SS \x97 write the supersampled buffer as-is.");
	m_tooltips.AddTool(GetDlgItem(IDC_TT_TRANSPARENT_PAL0),
		"Export palette index 0 (the engine's transparency color) as real "
		"transparency instead of the on-screen BG color. Works for both "
		"PNG sequence and animated GIF.\r\n"
		"  PNG: clean alpha=0 on transparent pixels.\r\n"
		"  GIF: 1-bit transparency. The very first frame may briefly show "
		"the BG color (gif.h limitation \x97 looped playback hides it after "
		"the first cycle).");
	m_tooltips.AddTool(GetDlgItem(IDOK),
		"Pick an output path and start recording. The dialog closes; the "
		"capture progress is shown in the status bar at the bottom.");
	m_tooltips.AddTool(GetDlgItem(IDCANCEL),
		"Close without recording.");
	m_tooltips.Activate(TRUE);

	return TRUE;
}

BOOL CTurntableDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltips.GetSafeHwnd())
		m_tooltips.RelayEvent(pMsg);
	return CDialog::PreTranslateMessage(pMsg);
}

void CTurntableDlg::OnAnimChanged()
{
	if (CComboBox* anim = (CComboBox*)GetDlgItem(IDC_TT_ANIM_COMBO))
		m_anim = anim->GetCurSel();
}

void CTurntableDlg::OnFormatChanged()
{
	if (CComboBox* fmt = (CComboBox*)GetDlgItem(IDC_TT_FORMAT_COMBO))
		m_format = fmt->GetCurSel();
	update_enabled_state();
}

void CTurntableDlg::update_enabled_state()
{
	// Delay only meaningful for GIF; PNG sequence has no embedded timing.
	const bool delay_active = (m_format == fmt_gif);
	if (CWnd* w = GetDlgItem(IDC_TT_DELAY_LABEL)) w->EnableWindow(delay_active);
	if (CWnd* w = GetDlgItem(IDC_TT_DELAY_EDIT))  w->EnableWindow(delay_active);
	if (CWnd* w = GetDlgItem(IDC_TT_DELAY_SPIN))  w->EnableWindow(delay_active);
}

HBRUSH CTurntableDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (HBRUSH br = theme::on_ctl_color(pDC->GetSafeHdc(),
		pWnd ? pWnd->GetSafeHwnd() : NULL, nCtlColor))
		return br;
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}
