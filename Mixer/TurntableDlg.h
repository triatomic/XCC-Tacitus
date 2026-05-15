#pragma once

#include "resource.h"

// IDD_VXL_TURNTABLE: collects parameters for the VXL turntable capture
// (Frames, Format, Direction, Animation source, GIF delay).
//
// Owner code (CXCCFileView::OnPlayerTurntable) constructs the dialog with
// hva_available = (m_hva_loaded && m_player_cf > 1), runs DoModal, and reads
// the public m_* fields on IDOK. The dialog itself does no rendering and no
// file I/O — it's pure data entry.
class CTurntableDlg : public CDialog
{
public:
	enum { fmt_gif = 0, fmt_png_seq = 1 };
	enum { anim_rotation = 0, anim_hva = 1, anim_combined = 2 };
	// Downscale modes — see populate logic in OnInitDialog. The combobox is
	// rebuilt per dialog open to match the current SS; entries that don't
	// apply to the current SS are simply omitted (no greyed-out items).
	enum { ds_native = 0, ds_half = 1, ds_full = 2 };

	// is_shp = true when the source is a SHP/WSA. Hides rotation +
	// downscale rows; locks animation mode to "frames"; frames range
	// becomes 1..max_frames with default = max_frames.
	CTurntableDlg(bool hva_available, int hva_frame_count, int ss, CWnd* parent,
		bool is_shp = false, int shp_max_frames = 0);

	int  m_frames = 36;        // 4..360
	int  m_format = fmt_gif;   // fmt_gif | fmt_png_seq
	bool m_dir_cw = true;      // false = CCW
	int  m_anim   = anim_rotation;
	int  m_delay_cs = 5;       // GIF frame delay in centiseconds (0.05 s = ~20 fps)
	int  m_downscale = ds_native;  // default for SS>1; forced to ds_full when SS==1
	bool m_transparent_pal0 = false;  // SHP/WSA only: emit palette index 0 as transparent

protected:
	bool m_hva_available;
	int  m_hva_cf;
	int  m_ss;
	bool m_is_shp;
	int  m_shp_max_frames;

	virtual void DoDataExchange(CDataExchange* pDX) override;
	virtual BOOL OnInitDialog() override;
	virtual BOOL PreTranslateMessage(MSG* pMsg) override;
	afx_msg void OnAnimChanged();
	afx_msg void OnFormatChanged();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	void update_enabled_state();
	CToolTipCtrl m_tooltips;
	DECLARE_MESSAGE_MAP()
};
