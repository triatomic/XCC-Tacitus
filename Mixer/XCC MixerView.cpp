#include "stdafx.h"
#include "XCC Mixer.h"
#include "XSE_dlg.h"
#include "MainFrm.h"
#include "XCCFileView.h"
#include "XCC MixerDoc.h"
#include "XCC MixerView.h"
#include "AudioPlayerDlg.h"
#include "keybinds.h"
#include "recents.h"
#include "bookmarks.h"
#include "resource.h"
#include "XSTE_dlg.h"
#include <algorithm>
#include <aud_decode.h>
#include <aud_file_write.h>
#include <aud_file.h>
#include <big_edit.h>
#include <big_file_write.h>
#include <cps_file.h>
#include <dds_file.h>
#include <fstream>
#include <hva_file.h>
#include <id_log.h>
#include <ima_adpcm_wav_decode.h>
#include <ima_adpcm_wav_encode.h>
#include <image_tools.h>
#include <jpeg_file.h>
#include <map_ts_encoder.h>
#include <map_ts_ini_reader.h>
#include <mix_edit.h>
#include <mix_file_write.h>
#include <mix_rg_edit.h>
#include <mix_rg_file_write.h>
#include <pal_file.h>
#include <pal_file.h>
#include <pcx_decode.h>
#include <pcx_file.h>
#include <pkt_ts_ini_reader.h>
#include <shp_decode.h>
#include <shp_dune2_file.h>
#include <shp_file.h>
#include <shp_images.h>
#include <shp_ts_file_write.h>
#include <shp_ts_file.h>
#include <sstream>
#include <st_file.h>
#include <string_conversion.h>
#include <text_file.h>
#include <tmp_file.h>
#include <tmp_ra_file.h>
#include <tmp_ts_file.h>
#include <virtual_tfile.h>
#include <voc_file.h>
#include <vqa_file.h>
#include <vxl_file.h>
#include <wav_file.h>
#include <wav_structures.h>
#include <wsa_dune2_file.h>
#include <wsa_file.h>
#include <xcc_dirs.h>
#include <xif_file.h>
#include "dlg_shp_viewer.h"
#include "resizedlg.h"
#include "shp_properties_dlg.h"
#include "shortcut.h"
#include "theme.h"
#include <shellapi.h>
#include <shlobj.h>

// Temp files written by the "Use external programs" toggle. The user double-
// clicks a file inside a MIX, we extract the bytes to %TEMP%\xcc_mixer\<name>
// and ShellExecute("open") it. We can't delete eagerly because the launched
// app may keep the handle open; the tracked list is flushed in ExitInstance.
namespace ext_open
{
	static std::vector<std::string> g_temp_files;
	static std::string g_temp_dir; // %TEMP%\xcc_mixer\, lazily resolved

	static const std::string& temp_dir()
	{
		if (g_temp_dir.empty())
		{
			char buf[MAX_PATH];
			DWORD n = ::GetTempPath(MAX_PATH, buf);
			std::string d = (n && n < MAX_PATH) ? std::string(buf, n) : std::string("C:\\Windows\\Temp\\");
			if (!d.empty() && d.back() != '\\' && d.back() != '/')
				d += '\\';
			d += "xcc_mixer\\";
			::CreateDirectory(d.c_str(), NULL);
			g_temp_dir = d;
		}
		return g_temp_dir;
	}

	// Sanitize a name for use as a leaf filename. MIX entries may contain
	// backslashes (folder separators) — replace those and any other illegal
	// chars so we end up with a single leaf in the temp dir.
	static std::string sanitize(const std::string& name)
	{
		std::string r = name;
		for (auto& c : r)
		{
			if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?'
				|| c == '"' || c == '<' || c == '>' || c == '|')
				c = '_';
		}
		return r;
	}

	// Insert "_tmp" before the extension so the user can tell extracted
	// preview files apart from production assets at a glance. "foo.shp"
	// becomes "foo_tmp.shp"; a name with no extension just gets "_tmp"
	// appended.
	static std::string mark_as_tmp(const std::string& name)
	{
		auto dot = name.find_last_of('.');
		if (dot == std::string::npos || dot == 0)
			return name + "_tmp";
		return name.substr(0, dot) + "_tmp" + name.substr(dot);
	}

	// Extract `data` to <temp_dir>\<sanitized_name>. Returns the resolved path
	// on success and tracks it for cleanup on app exit; returns empty string
	// on save failure. Shared by extract_and_open + extract_and_open_with so
	// both paths agree on the temp filename and cleanup tracking.
	static std::string extract_to_temp(const std::string& name, const Cvirtual_binary& data)
	{
		std::string path = temp_dir() + mark_as_tmp(sanitize(name));
		if (data.save(path))
			return std::string();
		g_temp_files.push_back(path);
		return path;
	}

	// Extract `data` to <temp_dir>\<sanitized_name> and ShellExecute("open") it.
	// Returns true on success. Tracks the path for cleanup on app exit.
	static bool extract_and_open(HWND hwnd, const std::string& name, const Cvirtual_binary& data)
	{
		std::string path = extract_to_temp(name, data);
		if (path.empty())
			return false;
		HINSTANCE rc = ::ShellExecute(hwnd, "open", path.c_str(), NULL, NULL, SW_SHOW);
		return reinterpret_cast<INT_PTR>(rc) > 32;
	}

	// Show the Windows "Open With..." picker for an existing filesystem path.
	// Uses SHOpenWithDialog (Vista+); OAIF_EXEC launches the chosen app
	// immediately, OAIF_HIDE_REGISTRATION hides the "always use this app"
	// checkbox so a casual pick doesn't change the user's file association.
	// Returns true if the dialog was shown (whether or not the user picked).
	static bool open_with_path(HWND hwnd, const std::string& fs_path)
	{
		// Proper ANSI→UTF-16 conversion — naive byte-copy mangles non-ASCII
		// path components (Westwood game installs sometimes live under
		// localized Program Files paths).
		int wlen = ::MultiByteToWideChar(CP_ACP, 0, fs_path.c_str(),
			static_cast<int>(fs_path.size()), nullptr, 0);
		std::wstring wpath(wlen, L'\0');
		if (wlen)
			::MultiByteToWideChar(CP_ACP, 0, fs_path.c_str(),
				static_cast<int>(fs_path.size()), wpath.data(), wlen);
		OPENASINFO oi{};
		oi.pcszFile = wpath.c_str();
		oi.pcszClass = nullptr;
		oi.oaifInFlags = OAIF_EXEC | OAIF_HIDE_REGISTRATION;
		HRESULT hr = ::SHOpenWithDialog(hwnd, &oi);
		return SUCCEEDED(hr);
	}

	// MIX entry → temp file → SHOpenWithDialog. Convenience wrapper.
	static bool extract_and_open_with(HWND hwnd, const std::string& name, const Cvirtual_binary& data)
	{
		std::string path = extract_to_temp(name, data);
		if (path.empty())
			return false;
		return open_with_path(hwnd, path);
	}

	// Best-effort delete of every temp file we wrote this session. Called from
	// ExitInstance. Files still held by the launched app will silently fail
	// to delete, which is fine — the user / OS will clean them eventually.
	void cleanup()
	{
		for (auto& p : g_temp_files)
			::DeleteFile(p.c_str());
		g_temp_files.clear();
	}
}

IMPLEMENT_DYNCREATE(CXCCMixerView, CListView)

BEGIN_MESSAGE_MAP(CXCCMixerView, CListView)
	ON_COMMAND_RANGE(ID_FILE_FOUND_MIX000, ID_FILE_FOUND_MIX199, OnFileFound)
	ON_WM_CONTEXTMENU()
	//{{AFX_MSG_MAP(CXCCMixerView)
	ON_COMMAND(ID_FILE_OPEN, OnFileOpen)
	ON_COMMAND(ID_FILE_CLOSE, OnFileClose)
	ON_NOTIFY_REFLECT(LVN_ITEMCHANGED, OnItemchanged)
	ON_COMMAND(ID_FILE_NEW, OnFileNew)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnDblclk)
	ON_WM_DESTROY()
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnColumnclick)
	ON_COMMAND(ID_POPUP_EXTRACT, OnPopupExtract)
	ON_UPDATE_COMMAND_UI(ID_POPUP_EXTRACT, OnUpdatePopupExtract)
	ON_COMMAND(ID_POPUP_COPY, OnPopupCopy)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY, OnUpdatePopupCopy)
	ON_COMMAND(ID_POPUP_COPY_AS_AUD, OnPopupCopyAsAUD)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_AUD, OnUpdatePopupCopyAsAUD)
	ON_COMMAND(ID_POPUP_COPY_AS_AVI, OnPopupCopyAsAVI)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_AVI, OnUpdatePopupCopyAsAVI)
	ON_COMMAND(ID_POPUP_COPY_AS_CPS, OnPopupCopyAsCPS)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_CPS, OnUpdatePopupCopyAsCPS)
	ON_COMMAND(ID_POPUP_COPY_AS_PCX, OnPopupCopyAsPCX)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_PCX, OnUpdatePopupCopyAsPCX)
	ON_COMMAND(ID_POPUP_COPY_AS_SHP, OnPopupCopyAsSHP)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_SHP, OnUpdatePopupCopyAsSHP)
	ON_COMMAND(ID_POPUP_COPY_AS_WSA, OnPopupCopyAsWSA)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_WSA, OnUpdatePopupCopyAsWSA)
	ON_COMMAND(ID_POPUP_DELETE, OnPopupDelete)
	ON_UPDATE_COMMAND_UI(ID_POPUP_DELETE, OnUpdatePopupDelete)
	ON_COMMAND(ID_POPUP_OPEN, OnPopupOpen)
	ON_UPDATE_COMMAND_UI(ID_POPUP_OPEN, OnUpdatePopupOpen)
	ON_COMMAND(ID_POPUP_OPEN_WITH, OnPopupOpenWith)
	ON_UPDATE_COMMAND_UI(ID_POPUP_OPEN_WITH, OnUpdatePopupOpenWith)
	ON_COMMAND(ID_POPUP_COPY_AS_VXL, OnPopupCopyAsVXL)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_VXL, OnUpdatePopupCopyAsVXL)
	ON_COMMAND(ID_POPUP_COPY_AS_XIF, OnPopupCopyAsXIF)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_XIF, OnUpdatePopupCopyAsXIF)
	ON_COMMAND(ID_POPUP_COPY_AS_CSV, OnPopupCopyAsCSV)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_CSV, OnUpdatePopupCopyAsCSV)
	ON_COMMAND(ID_POPUP_COPY_AS_HVA, OnPopupCopyAsHVA)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_HVA, OnUpdatePopupCopyAsHVA)
	ON_COMMAND(ID_POPUP_COPY_AS_PAL, OnPopupCopyAsPAL)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_PAL, OnUpdatePopupCopyAsPAL)
	ON_COMMAND(ID_POPUP_COPY_AS_SHP_TS, OnPopupCopyAsSHP_TS)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_SHP_TS, OnUpdatePopupCopyAsSHP_TS)
	ON_COMMAND(ID_POPUP_COPY_AS_PAL_JASC, OnPopupCopyAsPAL_JASC)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_PAL_JASC, OnUpdatePopupCopyAsPAL_JASC)
	ON_COMMAND(ID_POPUP_COPY_AS_TEXT, OnPopupCopyAsText)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_TEXT, OnUpdatePopupCopyAsText)
	ON_COMMAND(ID_POPUP_COPY_AS_MAP_TS_PREVIEW, OnPopupCopyAsMapTsPreview)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_MAP_TS_PREVIEW, OnUpdatePopupCopyAsMapTsPreview)
	ON_COMMAND(ID_POPUP_REFRESH, OnPopupRefresh)
	ON_UPDATE_COMMAND_UI(ID_POPUP_REFRESH, OnUpdatePopupRefresh)
	ON_COMMAND(ID_POPUP_RESIZE, OnPopupResize)
	ON_UPDATE_COMMAND_UI(ID_POPUP_RESIZE, OnUpdatePopupResize)
	ON_COMMAND(ID_POPUP_COPY_AS_HTML, OnPopupCopyAsHTML)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_HTML, OnUpdatePopupCopyAsHTML)
	ON_COMMAND(ID_POPUP_COPY_AS_PNG, OnPopupCopyAsPNG)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_PNG, OnUpdatePopupCopyAsPNG)
	ON_COMMAND(ID_POPUP_COPY_AS_WAV_IMA_ADPCM, OnPopupCopyAsWavImaAdpcm)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_WAV_IMA_ADPCM, OnUpdatePopupCopyAsWavImaAdpcm)
	ON_COMMAND(ID_POPUP_COPY_AS_WAV_PCM, OnPopupCopyAsWavPcm)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_WAV_PCM, OnUpdatePopupCopyAsWavPcm)
	ON_COMMAND(ID_POPUP_COPY_AS_PCX_SINGLE, OnPopupCopyAsPcxSingle)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_PCX_SINGLE, OnUpdatePopupCopyAsPcxSingle)
	ON_COMMAND(ID_EDIT_COPY, OnPopupClipboardCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdatePopupClipboardCopy)
	ON_COMMAND(ID_POPUP_COPY_AS_PNG_SINGLE, OnPopupCopyAsPngSingle)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_PNG_SINGLE, OnUpdatePopupCopyAsPngSingle)
	ON_COMMAND(ID_POPUP_CLIPBOARD_PASTE_AS_PCX, OnPopupClipboardPasteAsPcx)
	ON_UPDATE_COMMAND_UI(ID_POPUP_CLIPBOARD_PASTE_AS_PCX, OnUpdatePopupClipboardPasteAsImage)
	ON_COMMAND(ID_POPUP_CLIPBOARD_PASTE_AS_SHP_TS, OnPopupClipboardPasteAsShpTs)
	ON_UPDATE_COMMAND_UI(ID_POPUP_CLIPBOARD_PASTE_AS_SHP_TS, OnUpdatePopupClipboardPasteAsVideo)
	ON_COMMAND(ID_POPUP_CLIPBOARD_PASTE_AS_PNG, OnPopupClipboardPasteAsPng)
	ON_COMMAND(ID_POPUP_COPY_AS_JPEG, OnPopupCopyAsJpeg)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_JPEG, OnUpdatePopupCopyAsJpeg)
	ON_COMMAND(ID_POPUP_CLIPBOARD_PASTE_AS_JPEG, OnPopupClipboardPasteAsJpeg)
	ON_COMMAND(ID_POPUP_EXPLORE, OnPopupExplore)
	ON_UPDATE_COMMAND_UI(ID_POPUP_EXPLORE, OnUpdatePopupExplore)
	ON_COMMAND(ID_POPUP_BOOKMARK, OnPopupBookmark)
	ON_UPDATE_COMMAND_UI(ID_POPUP_BOOKMARK, OnUpdatePopupBookmark)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnGetdispinfo)
	ON_WM_DROPFILES()
	ON_COMMAND(ID_POPUP_COMPACT, OnPopupCompact)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COMPACT, OnUpdatePopupCompact)
	ON_COMMAND(ID_POPUP_COPY_AS_TGA, OnPopupCopyAsTga)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_TGA, OnUpdatePopupCopyAsTga)
	ON_COMMAND(ID_EDIT_SELECT_ALL, OnEditSelectAll)
	ON_COMMAND(ID_POPUP_CLIPBOARD_PASTE_AS_TGA, OnPopupClipboardPasteAsTga)
	ON_UPDATE_COMMAND_UI(ID_POPUP_CLIPBOARD_PASTE_AS_PNG, OnUpdatePopupClipboardPasteAsImage)
	ON_UPDATE_COMMAND_UI(ID_POPUP_CLIPBOARD_PASTE_AS_JPEG, OnUpdatePopupClipboardPasteAsImage)
	ON_UPDATE_COMMAND_UI(ID_POPUP_CLIPBOARD_PASTE_AS_TGA, OnUpdatePopupClipboardPasteAsImage)
	ON_COMMAND(ID_POPUP_COPY_AS_JPEG_SINGLE, OnPopupCopyAsJpegSingle)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_JPEG_SINGLE, OnUpdatePopupCopyAsJpegSingle)
	ON_COMMAND(ID_POPUP_COPY_AS_TGA_SINGLE, OnPopupCopyAsTgaSingle)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_AS_TGA_SINGLE, OnUpdatePopupCopyAsTgaSingle)
	ON_COMMAND(ID_POPUP_COPY_NAME, OnPopupCopyName)
	ON_UPDATE_COMMAND_UI(ID_POPUP_COPY_NAME, OnUpdatePopupCopyName)
	ON_COMMAND(ID_POPUP_BATCH_EXTRACT, OnPopupBatchExtract)
	ON_UPDATE_COMMAND_UI(ID_POPUP_BATCH_EXTRACT, OnUpdatePopupBatchExtract)
	ON_COMMAND(ID_POPUP_BATCH_EXTRACT_PRESERVE, OnPopupBatchExtractPreserve)
	ON_UPDATE_COMMAND_UI(ID_POPUP_BATCH_EXTRACT_PRESERVE, OnUpdatePopupBatchExtractPreserve)
	//}}AFX_MSG_MAP
	ON_WM_XBUTTONUP()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
	ON_WM_SETFOCUS()
	ON_WM_NCCALCSIZE()
	ON_WM_NCPAINT()
	ON_WM_NCHITTEST()
	ON_WM_NCLBUTTONDBLCLK()
	ON_NOTIFY_REFLECT(LVN_BEGINDRAG, OnBeginDrag)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_CAPTURECHANGED()
	ON_WM_TIMER()
END_MESSAGE_MAP()

enum t_error_message
{
	em_bad_fname = 0x400,
	em_bad_depth,
	em_bad_size
};

void CXCCMixerView::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	const bool dark = theme::is_dark();
	switch (cd->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		// Need pre-item-paint (to push theme text/bk colors) and post-item-
		// paint (to draw our own grid lines on top of the row).
		*pResult = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
		break;
	case CDDS_ITEMPREPAINT:
	{
		// Inline banner mode: owner-draw the pinned anchor row (item data 0).
		const int item = static_cast<int>(cd->nmcd.dwItemSpec);
		if (theme::banner_mode_v() == theme::banner_inline
			&& item >= 0 && GetListCtrl().GetItemData(item) == 0)
		{
			CRect rc = cd->nmcd.rc;
			CRect client;
			GetClientRect(&client);
			if (rc.right < client.right)   // some report-view rcs cover col 0 only
				rc.right = client.right;
			draw_mode_banner(cd->nmcd.hdc, rc);
			*pResult = CDRF_SKIPDEFAULT;
			break;
		}
		if (dark)
		{
			cd->clrText = theme::text();
			cd->clrTextBk = theme::bg();
			*pResult = CDRF_NEWFONT | CDRF_NOTIFYPOSTPAINT;
		}
		else
		{
			*pResult = CDRF_DODEFAULT | CDRF_NOTIFYPOSTPAINT;
		}
		break;
	}
	case CDDS_ITEMPOSTPAINT:
		// Dark-mode grid: LVS_EX_GRIDLINES is stripped in apply_grid (dark
		// mode hardcodes light gray), so paint our own row + column
		// separators here using theme::border().
		if (dark && theme::show_grid())
		{
			HDC hdc = cd->nmcd.hdc;
			if (hdc)
			{
				HPEN pen = ::CreatePen(PS_SOLID, 1, theme::border());
				HGDIOBJ old = ::SelectObject(hdc, pen);
				const RECT& rc = cd->nmcd.rc;
				::MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
				::LineTo(hdc, rc.right, rc.bottom - 1);
				CHeaderCtrl* hdr = GetListCtrl().GetHeaderCtrl();
				const int n = hdr ? hdr->GetItemCount() : 0;
				int x = rc.left;
				for (int i = 0; i < n; i++)
				{
					x += GetListCtrl().GetColumnWidth(i);
					::MoveToEx(hdc, x - 1, rc.top, NULL);
					::LineTo(hdc, x - 1, rc.bottom);
				}
				::SelectObject(hdc, old);
				::DeleteObject(pen);
			}
		}
		*pResult = CDRF_DODEFAULT;
		break;
	}
}

// --- Mode banner (pinned anchor row) ----------------------------------------

// Linear blend of two colors; t in [0,1] (0 = a, 1 = b).
static COLORREF banner_blend(COLORREF a, COLORREF b, double t)
{
	auto m = [&](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5); };
	return RGB(m(GetRValue(a), GetRValue(b)),
		m(GetGValue(a), GetGValue(b)),
		m(GetBValue(a), GetBValue(b)));
}

// Small folder silhouette (a body with a tab) drawn inside r.
static void draw_folder_glyph(HDC hdc, CRect r, COLORREF c)
{
	HBRUSH br = ::CreateSolidBrush(banner_blend(c, RGB(255, 255, 255), 0.55));
	HPEN pen = ::CreatePen(PS_SOLID, 1, c);
	HGDIOBJ ob = ::SelectObject(hdc, br);
	HGDIOBJ op = ::SelectObject(hdc, pen);
	const int tab_h = max(2, r.Height() / 4);
	::Rectangle(hdc, r.left, r.top, r.left + (r.Width() * 3) / 5, r.top + tab_h + 2);
	::RoundRect(hdc, r.left, r.top + tab_h, r.right, r.bottom, 3, 3);
	::SelectObject(hdc, ob);
	::SelectObject(hdc, op);
	::DeleteObject(br);
	::DeleteObject(pen);
}

// Small parcel/box (square + lid line + vertical strap) drawn inside r.
static void draw_archive_glyph(HDC hdc, CRect r, COLORREF c)
{
	HBRUSH br = ::CreateSolidBrush(banner_blend(c, RGB(255, 255, 255), 0.55));
	HPEN pen = ::CreatePen(PS_SOLID, 1, c);
	HGDIOBJ ob = ::SelectObject(hdc, br);
	HGDIOBJ op = ::SelectObject(hdc, pen);
	::RoundRect(hdc, r.left, r.top, r.right, r.bottom, 2, 2);
	const int midx = r.left + r.Width() / 2;
	const int lidy = r.top + r.Height() / 3;
	::MoveToEx(hdc, r.left, lidy, NULL);
	::LineTo(hdc, r.right, lidy);
	::MoveToEx(hdc, midx, r.top, NULL);
	::LineTo(hdc, midx, r.bottom);
	::SelectObject(hdc, ob);
	::SelectObject(hdc, op);
	::DeleteObject(br);
	::DeleteObject(pen);
}

void CXCCMixerView::update_banner_label()
{
	if (m_mix_f)
	{
		std::vector<std::string> segs = nav_segments();
		m_banner_label = segs.empty() ? Cfname(m_mix_fname).get_fname() : segs.back();
	}
	else
		m_banner_label = m_dir;
}

// Full path shown by the mode banner, for the double-click "copy path" action.
// Folder mode: the on-disk directory. MIX mode: the on-disk root MIX path, then
// " > <nested>" per nested level (nested MIXes have no real filesystem path).
std::string CXCCMixerView::banner_path() const
{
	if (!m_mix_f)
	{
		std::string d = m_dir;
		while (!d.empty() && (d.back() == '\\' || d.back() == '/'))
			d.pop_back();
		return d;
	}
	std::string p = m_mix_fname;
	std::vector<std::string> segs = const_cast<CXCCMixerView*>(this)->nav_segments();
	// nav_segments = folder parts + root MIX filename + one entry per nested MIX.
	// The root MIX filename is already covered by m_mix_fname; append only the
	// nested names that follow it.
	bool past_root = false;
	const std::string root_leaf = Cfname(m_mix_fname).get_fname();
	for (const std::string& s : segs)
	{
		if (!past_root)
		{
			if (s == root_leaf)
				past_root = true;
			continue;
		}
		p += " > " + s;
	}
	return p;
}

void CXCCMixerView::apply_banner_mode()
{
	if (!GetSafeHwnd())
		return;
	update_banner_label();
	// Strip mode reserves NC space; toggling it on/off must recalc the frame so
	// the header + rows reflow. SWP_FRAMECHANGED forces WM_NCCALCSIZE + WM_NCPAINT.
	SetWindowPos(NULL, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	Invalidate(FALSE);   // inline mode repaints the anchor row
}

void CXCCMixerView::draw_mode_banner(HDC hdc, const CRect& rc)
{
	const bool dark = theme::is_dark();
	const bool is_mix = (m_mix_f != nullptr);
	// Mode colour language: amber/box = MIX archive, blue/folder = filesystem.
	const COLORREF accent = is_mix ? RGB(206, 145, 42) : RGB(58, 124, 201);
	const COLORREF base = dark ? theme::bg() : ::GetSysColor(COLOR_WINDOW);
	const COLORREF bg = banner_blend(base, accent, dark ? 0.22 : 0.12);
	const COLORREF txt = dark ? theme::text() : RGB(30, 30, 30);

	// Background wash, accent left spine, and a bottom rule.
	HBRUSH bbr = ::CreateSolidBrush(bg);
	::FillRect(hdc, &rc, bbr);
	::DeleteObject(bbr);
	{
		RECT spine = { rc.left, rc.top, rc.left + 2, rc.bottom };
		HBRUSH sbr = ::CreateSolidBrush(accent);
		::FillRect(hdc, &spine, sbr);
		::DeleteObject(sbr);
		HPEN pen = ::CreatePen(PS_SOLID, 1, banner_blend(base, accent, 0.5));
		HGDIOBJ op = ::SelectObject(hdc, pen);
		::MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
		::LineTo(hdc, rc.right, rc.bottom - 1);
		::SelectObject(hdc, op);
		::DeleteObject(pen);
	}

	// Glyph, vertically centred.
	int gs = min(16, rc.Height() - 4);
	if (gs < 8)
		gs = max(6, rc.Height() - 2);
	CRect gr(rc.left + 7, rc.top + (rc.Height() - gs) / 2, rc.left + 7 + gs, rc.top + (rc.Height() - gs) / 2 + gs);
	if (is_mix)
		draw_archive_glyph(hdc, gr, accent);
	else
		draw_folder_glyph(hdc, gr, accent);

	// Build the label font. Prefer the list's own font; if the control has none
	// set (WM_GETFONT == NULL here) fall back to the system *message* font (Segoe
	// UI on Win10/11) -- NOT GetStockObject(DEFAULT_GUI_FONT) or the window DC's
	// default stock font, both of which are the dated MS Sans Serif bitmap face
	// and render blocky/aliased next to the ClearType row text. Force ClearType
	// so the strip matches. A regular-weight twin draws the trailing tag (we must
	// NOT restore the DC's stock font for it).
	LOGFONT lf = {};
	HFONT list_font = reinterpret_cast<HFONT>(::SendMessage(GetSafeHwnd(), WM_GETFONT, 0, 0));
	if (list_font)
		::GetObject(list_font, sizeof lf, &lf);
	else
	{
		NONCLIENTMETRICS ncm = {};
		ncm.cbSize = sizeof ncm;
		if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
			lf = ncm.lfMessageFont;
		else
			::GetObject(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)), sizeof lf, &lf);
	}
	lf.lfQuality = CLEARTYPE_QUALITY;
	LOGFONT lfb = lf;
	lfb.lfWeight = FW_BOLD;
	HFONT reg_font = ::CreateFontIndirect(&lf);
	HFONT bold_font = ::CreateFontIndirect(&lfb);

	::SetBkMode(hdc, TRANSPARENT);
	::SetTextColor(hdc, txt);
	const std::string label = m_banner_label.empty()
		? std::string(is_mix ? "MIX archive" : "Folder")
		: m_banner_label;
	CRect name_rc(gr.right + 7, rc.top, rc.right - 7, rc.bottom);

	HGDIOBJ of = ::SelectObject(hdc, bold_font);
	CRect calc = name_rc;
	::DrawText(hdc, label.c_str(), -1, &calc, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
	const int name_w = min(static_cast<int>(calc.Width()), name_rc.Width());
	CRect draw_rc(name_rc.left, name_rc.top, name_rc.left + name_w, name_rc.bottom);
	const UINT name_fmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
		| (is_mix ? DT_END_ELLIPSIS : (DT_PATH_ELLIPSIS | DT_END_ELLIPSIS));
	::DrawText(hdc, label.c_str(), -1, &draw_rc, name_fmt);

	if (is_mix)
	{
		CRect tag_rc(draw_rc.right + 9, rc.top, rc.right - 7, rc.bottom);
		if (tag_rc.left < tag_rc.right)
		{
			::SelectObject(hdc, reg_font);
			::SetTextColor(hdc, banner_blend(txt, bg, 0.45));
			::DrawText(hdc, "MIX archive", -1, &tag_rc,
				DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
	}

	::SelectObject(hdc, of);
	::DeleteObject(bold_font);
	::DeleteObject(reg_font);
}

const char* error_messages[] =
{
	"Bad filename. All files should have a name like \"image ####.pcx\" where #### is the zero based index.",
	"Bad color depth. Only 8-bit images are supported.",
	"Bad size. All images should have the same size."
};

static CXCCMixerApp* GetApp()
{
	return static_cast<CXCCMixerApp*>(AfxGetApp());
}

static CMainFrame* GetMainFrame()
{
	return static_cast<CMainFrame*>(AfxGetMainWnd());
}

// Width (px) of the active-pane accent border, reserved in the non-client area
// by OnNcCalcSize and painted by OnNcPaint.
static const int kPaneFocusBorder = 1;

// Height (px) of the mode banner strip reserved at the top of the client area by
// OnNcCalcSize (pushing the column header + rows down) and painted by OnNcPaint.
static const int kBannerHeight = 20;

void CXCCMixerView::OnSetFocus(CWnd* pOldWnd)
{
	CListView::OnSetFocus(pOldWnd);
	// Become the sticky active pane; the frame repaints both panes' borders.
	if (CMainFrame* mf = GetMainFrame())
		mf->set_active_pane(this);
}

void CXCCMixerView::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
	CListView::OnNcCalcSize(bCalcValidRects, lpncsp);
	// Steal a kPaneFocusBorder strip on every edge from the client area so the
	// border has somewhere to paint without covering row pixels. Same idea as
	// dark_edit_proc's WM_NCCALCSIZE (theme.cpp). Always reserve it (not only
	// when active) so toggling focus doesn't reflow the list.
	if (lpncsp)
	{
		::InflateRect(&lpncsp->rgrc[0], -kPaneFocusBorder, -kPaneFocusBorder);
		// Strip banner mode reserves a fixed strip at the very top (painted in
		// OnNcPaint), pushing the column header + rows down. Off / inline modes
		// reserve nothing here.
		if (theme::banner_mode_v() == theme::banner_strip)
			lpncsp->rgrc[0].top += kBannerHeight;
	}
}

void CXCCMixerView::OnNcPaint()
{
	// Let the base paint scrollbars / any system NC widgets first.
	CListView::OnNcPaint();
	CMainFrame* mf = GetMainFrame();
	// Indicator can be toggled off (Theme > Pane Layout > Active Pane Border).
	// When off, treat no pane as active so the strip paints with the plain bg.
	const bool active = theme::active_pane_border()
		&& mf && mf->is_active_pane(this);
	HDC hdc = ::GetWindowDC(GetSafeHwnd());
	if (!hdc)
		return;
	CRect rc;
	GetWindowRect(rc);
	rc.OffsetRect(-rc.left, -rc.top);   // window DC coords are window-relative
	// Strip banner mode fills the reserved top strip (between the border and the
	// column header). Trim short of a visible vertical scrollbar so we don't paint
	// over the scrollbar's top arrow.
	if (theme::banner_mode_v() == theme::banner_strip)
	{
		CRect strip(rc.left + kPaneFocusBorder, rc.top + kPaneFocusBorder,
			rc.right - kPaneFocusBorder, rc.top + kPaneFocusBorder + kBannerHeight);
		if (GetStyle() & WS_VSCROLL)
			strip.right -= ::GetSystemMetrics(SM_CXVSCROLL);
		if (strip.right > strip.left)
			draw_mode_banner(hdc, strip);
	}
	// Active: accent frame. Inactive: paint the strip with the surrounding bg
	// (theme bg in dark, window color in light) so no stale border lingers.
	const COLORREF c = active ? theme::accent()
		: (theme::is_dark() ? theme::bg() : ::GetSysColor(COLOR_WINDOW));
	HPEN pen = ::CreatePen(PS_INSIDEFRAME, kPaneFocusBorder, c);
	HGDIOBJ old_pen = ::SelectObject(hdc, pen);
	HGDIOBJ old_brush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
	::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
	::SelectObject(hdc, old_brush);
	::SelectObject(hdc, old_pen);
	::DeleteObject(pen);
	::ReleaseDC(GetSafeHwnd(), hdc);
}

LRESULT CXCCMixerView::OnNcHitTest(CPoint point)
{
	// The strip banner is reserved from the client area by OnNcCalcSize, but the
	// default hit-test still reports it as HTCLIENT -- so clicks there generate
	// client mouse messages, never WM_NCLBUTTONDBLCLK. Claim the strip as a
	// (benign) non-client region so the double-click reaches OnNcLButtonDblClk.
	if (theme::banner_mode_v() == theme::banner_strip)
	{
		CRect wr;
		GetWindowRect(wr);
		CPoint pt = point;
		pt.Offset(-wr.left, -wr.top);   // window-relative, matching OnNcPaint
		CRect strip(kPaneFocusBorder, kPaneFocusBorder,
			wr.Width() - kPaneFocusBorder, kPaneFocusBorder + kBannerHeight);
		if (strip.PtInRect(pt))
			return HTBORDER;
	}
	return CListView::OnNcHitTest(point);
}

void CXCCMixerView::OnNcLButtonDblClk(UINT nHitTest, CPoint point)
{
	// Strip banner mode: double-clicking the reserved top strip copies the
	// current path. point is in screen coords; the strip rect mirrors OnNcPaint
	// (window-relative). Inline mode is handled in OnDblclk instead.
	if (theme::banner_mode_v() == theme::banner_strip)
	{
		CRect wr;
		GetWindowRect(wr);
		CPoint pt = point;
		pt.Offset(-wr.left, -wr.top);   // window-relative, matching OnNcPaint
		CRect strip(kPaneFocusBorder, kPaneFocusBorder,
			wr.Width() - kPaneFocusBorder, kPaneFocusBorder + kBannerHeight);
		if (strip.PtInRect(pt))
		{
			copy_banner_path();
			return;
		}
	}
	CListView::OnNcLButtonDblClk(nHitTest, point);
}

void CXCCMixerView::copy_banner_path()
{
	std::string p = banner_path();
	if (p.empty())
		return;
	set_clipboard_text(p);
	if (CMainFrame* mf = GetMainFrame())
		mf->SetMessageText(("Copied path: " + p).c_str());
}

CXCCMixerView::CXCCMixerView()
{
}

CXCCMixerView::~CXCCMixerView()
{
}

BOOL CXCCMixerView::PreCreateWindow(CREATESTRUCT& cs)
{
	cs.style |= LVS_REPORT | LVS_SHOWSELALWAYS;
	return CListView::PreCreateWindow(cs);
}

void CXCCMixerView::OnInitialUpdate()
{
	char dir[MAX_PATH];
	if (GetCurrentDirectory(MAX_PATH, dir))
	{
		Cfname fn(dir);
		fn.make_path();
		m_dir = fn.get_all();
	}
	else
		m_dir = "c:\\";
	{
		string dir(AfxGetApp()->GetProfileString(m_reg_key, "path", m_dir.c_str()));
		string fname = dir + "write_check.temp";
		if (!file32_write(fname, NULL, 0))
		{
			delete_file(fname);
			m_dir = dir;
		}
	}

	CListView::OnInitialUpdate();

	{
		DWORD ex = LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP;
		// In dark mode the system's LVS_EX_GRIDLINES paints with a hardcoded
		// light gray that clashes with the theme; we draw our own grid in
		// OnCustomDraw using theme::border() instead. Only enable the system
		// gridlines in light mode here, matching theme::apply_grid.
		if (theme::show_grid() && !theme::is_dark())
			ex |= LVS_EX_GRIDLINES;
		GetListCtrl().SetExtendedStyle(ex);
	}
	GetListCtrl().InsertColumn(0, "Name", LVCFMT_LEFT);
	GetListCtrl().InsertColumn(1, "Type", LVCFMT_LEFT);
	GetListCtrl().InsertColumn(2, "Size", LVCFMT_RIGHT);
	GetListCtrl().InsertColumn(3, "Description", LVCFMT_LEFT);
	theme::apply_column_headers(GetListCtrl().GetSafeHwnd());
	update_list();

	// Reopen the MIX this pane had open last session, if it still exists on disk.
	// Skip the recents push (it's already recorded) by flagging a nav replay.
	{
		string saved_mix(AfxGetApp()->GetProfileString(m_reg_key, "mix", ""));
		DWORD attr = saved_mix.empty() ? INVALID_FILE_ATTRIBUTES
			: ::GetFileAttributesA(saved_mix.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
		{
			bool was_replaying = m_nav_replaying;
			m_nav_replaying = true;
			open_location_mix(saved_mix);
			m_nav_replaying = was_replaying;
		}
	}
}

void CXCCMixerView::OnFileNew()
{
	const char* save_filter = "TD/RA MIX (*.mix)|*.mix|TS/RA2 MIX (*.mix)|*.mix|Renegade MIX (*.mix)|*.mix|Generals BIG (*.big)|*.big|";

	close_all_locations();
	CFileDialog dlg(false, save_filter, NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, save_filter, this);
	dlg.m_ofn.nFilterIndex = 2;
	if (IDOK == dlg.DoModal())
	{
		string name(dlg.GetPathName());
		int error = 0;
		switch (dlg.m_ofn.nFilterIndex)
		{
		case 1:
			error = Cmix_file_write(game_ra).write().save(name);
			break;
		case 2:
			error = Cmix_file_write(game_ts).write().save(name);
			break;
		//case 3:	//TS mixes are the same as ra2 ones
		//	error = Cmix_file_write(game_ra2).write().save(name);
		//	break;
		case 3:
			error = Cmix_rg_file_write().write().save(name);
			break;
		case 4:
			error = Cbig_file_write().write().save(name);
			break;
		default:
			assert(false);
		}
		//update_list();
		close_all_locations();
		open_location_mix(static_cast<string>(dlg.GetPathName()));
	}
}

void CXCCMixerView::OnFileOpen()
{
	CFileDialog dlg(true, "mix", NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, "Archive files (*.big;*.dat;*.mix;*.pkg;*.wsx;*.pak)|*.big;*.dat;*.mix;*.pkg;*.wsx;*.pak;|", this);
	if (IDOK == dlg.DoModal())
	{
		close_all_locations();
		open_location_mix(static_cast<string>(dlg.GetPathName()));
	}
}

void CXCCMixerView::OnFileClose()
{
	nav_go_up();
}

void CXCCMixerView::open_location_dir(const string& name)
{
	if (!m_nav_replaying)
		nav_clear_forward();
	m_dir = name;
	update_list();
}

void CXCCMixerView::open_location_mix(const string& name)
{
	if (!m_nav_replaying)
		nav_clear_forward();
	Cmix_file* mix_f = new Cmix_file;
	if (mix_f->open(name))
	{
		delete mix_f;
		mix_f = new Cmix_file_rd;
		if (static_cast<Cmix_file_rd*>(mix_f)->open(name))
		{
			delete mix_f;
		}
		else
		{
			goto MPUSH;
		}
	}
	else
	{
	MPUSH:
		// Entering a disk-root MIX (no MIX was open yet). Anchor m_dir to the
		// MIX's containing folder so "go up" / breadcrumb folder segments return
		// to the right place. Game MIXes opened from the built-in list (e.g.
		// D:\Westwood\RA1\redalert.mix) never set m_dir otherwise, so exiting the
		// MIX dropped the pane back onto a stale directory (the navigation-bar
		// limitation: folder clicks landed somewhere unrelated). Only when this
		// is the root open (m_mix_f == nullptr) and the path has a folder part.
		bool is_root_open = (m_mix_f == nullptr);
		if (is_root_open)
		{
			string folder = Cfname(name).get_path();
			if (!folder.empty())
				m_dir = folder;
		}
		m_location.push(m_mix_f);
		m_entered_ids.push(-1);
		m_mix_f = mix_f;
		m_mix_fname = name;
		// Record root-open of a disk archive in the Recents list. Skip nested
		// MIXes (those have a parent m_mix_f) and nav-stack replays (the path
		// is already in recents from the original open).
		if (is_root_open && !m_nav_replaying)
			recents::push(name);
	}
	update_list();
}

void CXCCMixerView::open_location_mix(t_mix_map_list::const_iterator i, int file_id, const vector<int>& sub_mix_chain)
{
	using t_stack = stack<int>;
	t_stack stack;
	close_all_locations();
	const t_mix_map_list& mix_list = GetMainFrame()->mix_map_list();
	while (i->second.fname.empty())
	{
		stack.push(i->second.id);
		i = mix_list.find(i->second.parent);
	}
	open_location_mix(i->second.fname);
	while (!stack.empty())
	{
		open_location_mix(stack.top());
		stack.pop();
	}
	// After the mix_map_list parent chain is opened, descend any further
	// in-mix sub-MIX indices the search captured. Required for hits in
	// nested mixes that aren't pre-registered in mix_map_list (e.g.
	// ra2.mix > local.mix > file: ra2.mix is in mix_map_list, local.mix
	// is just a nested mix discovered by the recursive scan). Without
	// this the navigation stops at ra2.mix and file_id is missing from
	// its listing, leaving the user staring at the wrong MIX.
	for (int idx_in_parent : sub_mix_chain)
	{
		if (idx_in_parent < 0 || !m_mix_f)
			break;
		open_location_mix(m_mix_f->get_id(idx_in_parent));
	}
	if (file_id)
	{
		CListCtrl& lc = GetListCtrl();
		LVFINDINFO lvf;
		lvf.flags = LVFI_PARAM;
		lvf.lParam = file_id;
		int i = lc.FindItem(&lvf, -1);
		if (i != -1)
		{
			lc.SetItemState(i, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
			lc.EnsureVisible(i, false);
		}
	}
}

void CXCCMixerView::open_location_mix(int mix_id, const vector<int>& sub_mix_chain, int file_id)
{
	close_all_locations();
	const t_index_entry& mix = t_index_list().at(mix_id);
	open_location_mix(m_dir.rfind('\\') == string::npos ? (m_dir + '\\' + mix.name) : (m_dir + mix.name));
	// Descend through every nested MIX in the chain (root -> sub -> sub -> ...
	// -> immediate parent of file). Each element is an index that
	// Cmix_file::get_id() resolves to the next level's MIX id.
	// Each open_location_mix(id) refreshes m_mix_f to point at the just-opened
	// mix, so the next chain step's get_id() addresses the correct level.
	// Previously this took a single sub_mix_id int and could only navigate 2
	// levels deep -- deeper nesting landed on the wrong MIX.
	for (int idx_in_parent : sub_mix_chain)
	{
		if (idx_in_parent < 0 || !m_mix_f)
			break;
		open_location_mix(m_mix_f->get_id(idx_in_parent));
	}
	if (file_id)
	{
		CListCtrl& lc = GetListCtrl();
		LVFINDINFO lvf;
		lvf.flags = LVFI_PARAM;
		lvf.lParam = file_id;
		int i = lc.FindItem(&lvf, -1);
		if (i != -1)
		{
			lc.SetItemState(i, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
			lc.EnsureVisible(i, false);
		}
	}
}

// Extract nested MIX `id` (a child of the currently-open m_mix_f, with display
// name `name`) to a unique temp file and push a t_nested_edit describing it.
// Returns the temp path, or empty on failure. Called from open_location_mix(id)
// at descent time, while m_mix_f still points at the PARENT (so get_vdata works).
bool CXCCMixerView::ensure_nested_temp()
{
	// Lazily materialize the temp for the current nested level. Browsing a
	// nested MIX writes nothing; the temp copy is created only here, the first
	// time an edit op runs (edit_release calls this). No-op once extracted.
	if (m_nested_edit.empty())
		return false;
	t_nested_edit& ne = m_nested_edit.back();
	if (!ne.temp_path.empty())
		return true;                    // already extracted this session

	// Extract from the PARENT: while sitting in a nested level, m_mix_f is the
	// nested MIX itself, so the bytes come from m_location.top() (parent) at the
	// entered id (m_entered_ids.top()). Both are valid at a nested level.
	if (m_location.empty() || m_entered_ids.empty())
		return false;
	Cmix_file* parent = m_location.top();
	int id = m_entered_ids.top();
	if (!parent)
		return false;
	Cvirtual_binary d = parent->get_vdata(id);
	if (!d.data() || !d.size())
		return false;

	// Unique leaf so two same-named nested MIXes (or the same one at different
	// depths) never collide: "<depth>_<id>_<sanitized name>".
	char prefix[32];
	wsprintfA(prefix, "%u_%d_", static_cast<unsigned>(m_nested_edit.size() - 1), id);
	string p = ext_open::temp_dir() + string(prefix) + ext_open::sanitize(ne.entry_name.empty() ? "nested.mix" : ne.entry_name);
	if (d.save(p))                      // save() returns 0 on success
		return false;
	ext_open::g_temp_files.push_back(p);  // cleaned up on app exit
	ne.temp_path = p;
	m_mix_fname = p;                    // the editors open m_mix_fname
	return true;
}

// Materialize a temp for the CURRENT top nested level (m_nested_edit.back())
// when it has none. Unlike ensure_nested_temp(), this does NOT touch m_mix_fname
// -- it is called from nested_flush_top() while reducing up the chain, where the
// top level is the *parent* receiving a child's re-inject. The parent's bytes
// come from its own parent, which at flush time is the live m_location.top() at
// id m_entered_ids.top(). Returns true if a usable temp exists afterward.
bool CXCCMixerView::ensure_parent_temp()
{
	if (m_nested_edit.empty())
		return false;
	t_nested_edit& pe = m_nested_edit.back();
	if (!pe.temp_path.empty())
		return true;
	if (m_location.empty() || m_entered_ids.empty())
		return false;
	Cmix_file* grandparent = m_location.top();
	int id = m_entered_ids.top();
	if (!grandparent)
		return false;
	Cvirtual_binary d = grandparent->get_vdata(id);
	if (!d.data() || !d.size())
		return false;
	char prefix[32];
	wsprintfA(prefix, "%u_%d_", static_cast<unsigned>(m_nested_edit.size() - 1), id);
	string p = ext_open::temp_dir() + string(prefix) + ext_open::sanitize(pe.entry_name.empty() ? "nested.mix" : pe.entry_name);
	if (d.save(p))                      // save() returns 0 on success
		return false;
	ext_open::g_temp_files.push_back(p);
	pe.temp_path = p;
	return true;
}

// Re-inject the deepest nested temp into its parent if it was edited, then pop
// it. The parent's editable file is the next path down: another temp if the
// parent is itself nested, else the on-disk root (m_mix_fname after the pop).
// Re-injecting marks the parent dirty so a multi-level edit propagates all the
// way to the on-disk root. Called from nav_go_up (the real "leaving" path), not
// from the edit ops' internal close_location.
void CXCCMixerView::nested_flush_top()
{
	if (m_nested_edit.empty())
		return;
	t_nested_edit ne = m_nested_edit.back();
	m_nested_edit.pop_back();
	if (ne.dirty && !ne.temp_path.empty())
	{
		// Parent path: the new top temp if still nested, else the disk root
		// (front of the fname stack, which close_location restores next).
		//
		// At depth >= 2 the parent is itself a nested MIX whose temp may not
		// exist yet -- a browse-only intermediate level never ran
		// ensure_nested_temp(). A dirty child is now a reason the parent MUST
		// have a disk-backed temp to receive the re-inject, so materialize it
		// here on demand (extract the parent's bytes from ITS parent, which at
		// this point is the live m_location.top() at id m_entered_ids.top()).
		// Without this the re-inject target is "" and the child edit is dropped
		// silently -- that was the depth->=2 bug.
		if (!m_nested_edit.empty() && m_nested_edit.back().temp_path.empty())
			ensure_parent_temp();
		string parent_path = m_nested_edit.empty()
			? (m_mix_fname_stack.empty() ? m_mix_fname : m_mix_fname_stack.front())
			: m_nested_edit.back().temp_path;
		Cvirtual_binary d;
		if (!parent_path.empty() && !d.load(ne.temp_path))
		{
			// The parent file (m_mix_f, just restored by close_location) is still
			// open and memory-mapped -- Cmix_edit::open uses open_edit (read-write,
			// EXCLUSIVE share), which fails while any handle holds the file. Close
			// the live parent around the edit, then reopen it from its path so the
			// view stays consistent. This was the bug: open_edit(parent) returned
			// nonzero (sharing violation) and the reinject was silently skipped.
			Cmix_file* saved_parent = m_mix_f;
			m_mix_f = nullptr;
			delete saved_parent;            // release the parent's file handle/map

			Cmix_edit f;
			int err = f.open(parent_path);
			if (!err)
			{
				f.erase(ne.entry_id);
				f.insert(ne.entry_name, d);
				f.write_index();
				f.compact();
				f.close();
				if (!m_nested_edit.empty())
					m_nested_edit.back().dirty = true;   // propagate up the chain
				else if (GetMainFrame())
					GetMainFrame()->SetMessageText(("Saved changes to " + Cfname(parent_path).get_fname()).c_str());
			}

			// Reopen the parent from its path so m_mix_f is live again for the
			// post-flush view (close_location's caller expects a valid parent).
			Cmix_file* mix_f = new Cmix_file;
			if (mix_f->open(parent_path))
			{
				delete mix_f;
				mix_f = new Cmix_file_rd;
				static_cast<Cmix_file_rd*>(mix_f)->open(parent_path);
			}
			m_mix_f = mix_f;
		}
	}
	if (!ne.temp_path.empty())
		DeleteFile(ne.temp_path.c_str());
}

void CXCCMixerView::open_location_mix(int id)
{
	if (!m_nav_replaying)
		nav_clear_forward();
	Cmix_file* mix_f = new Cmix_file;
	if (mix_f->open(id, *m_mix_f))
	{
		delete mix_f;
		mix_f = new Cmix_file_rd;
		if (static_cast<Cmix_file_rd*>(mix_f)->open(id, *m_mix_f))
		{
			delete static_cast<Cmix_file_rd*>(mix_f);
		}
		else
		{
			goto MPUSH;
		}
	}
	else
	{
	MPUSH:
		// Push a placeholder nested-edit entry for this level (temp_path empty).
		// We do NOT extract here -- browsing must be free of disk writes. The
		// temp copy is materialized lazily by ensure_nested_temp() on the first
		// edit (via edit_release). m_mix_fname stays the parent path until then.
		// Every nested descent pushes exactly one m_nested_edit + one
		// m_mix_fname_stack entry (kept in lock-step; close_location pops both).
		// Capture the name now, before m_mix_f is swapped to the nested MIX.
		{
			t_nested_edit ne;
			ne.entry_name = m_mix_f ? m_mix_f->get_name(id) : string();
			ne.entry_id = id;
			ne.temp_path.clear();
			ne.dirty = false;
			m_nested_edit.push_back(ne);
		}
		m_mix_fname_stack.push_back(m_mix_fname);
		m_location.push(m_mix_f);
		m_entered_ids.push(id);
		m_mix_f = mix_f;
		update_list();
	}
}

void CXCCMixerView::close_location(int reload)
{
	if (m_mix_f)
	{
		// Release the live Cmix_file FIRST: it memory-maps / holds an open handle
		// on the nested temp, and nested_flush_top below must load(temp) +
		// reinject. Flushing while the handle is still open made d.load fail
		// silently, so nested edits never reached the parent. Delete before flush.
		delete m_mix_f;
		m_mix_f = m_location.top();
		m_location.pop();
		if (!m_entered_ids.empty())
			m_entered_ids.pop();
		// Flush+pop the nested-edit temp for this level (no-op at disk root).
		// Re-inject happens here so it covers every genuine leave route (nav up,
		// ".." row, breadcrumb, Backspace, file-close); they all funnel through
		// close_location. The edit ops do NOT teardown via close_location at
		// nested levels -- they use edit_release()/edit_reopen() so the temp
		// survives the edit (see those).
		nested_flush_top();
		if (!m_mix_fname_stack.empty())
		{
			m_mix_fname = m_mix_fname_stack.back();
			m_mix_fname_stack.pop_back();
		}
		if (reload)
			update_list();
	}
}

void CXCCMixerView::close_all_locations()
{
	while (!m_location.empty())
		close_location(false);
}

// Release the current MIX level's file handle so a disk-path editor
// (Cmix_edit/Cbig_edit/Cmix_rg_edit) can open m_mix_fname. The level is NOT
// left -- at a nested level the t_nested_edit temp survives so edit_reopen()
// can restore the view afterward. Used by the in-place edit ops (insert/drop/
// delete/compact) in place of the old close_location(false)+reopen pair.
bool CXCCMixerView::edit_release()
{
	if (editing_nested())
	{
		// Materialize the temp on first edit (lazy extract). If it fails (disk
		// full / unwritable temp), return false WITHOUT releasing m_mix_f so the
		// caller skips the edit entirely -- the level stays read-only and the
		// parent archive is never touched.
		if (!ensure_nested_temp())
			return false;
		// Drop just the live Cmix_file (unlocks the temp); keep m_mix_f's parent
		// on m_location and the t_nested_edit/m_mix_fname (= temp) intact.
		delete m_mix_f;
		m_mix_f = nullptr;
	}
	else
	{
		close_location(false);
	}
	return true;
}

// Restore the view after an in-place edit and record that the edit happened.
// At a nested level, re-open the (now edited) temp as a standalone disk MIX and
// mark the level dirty so close_location re-injects it into the parent on leave.
// At the disk root, behaves like the old reopen.
void CXCCMixerView::edit_reopen(bool edited)
{
	if (editing_nested())
	{
		if (edited)
			nested_mark_dirty();
		Cmix_file* mix_f = new Cmix_file;
		if (mix_f->open(m_mix_fname))   // open(path) returns 0 on success
		{
			delete mix_f;
			mix_f = new Cmix_file_rd;
			static_cast<Cmix_file_rd*>(mix_f)->open(m_mix_fname);
		}
		m_mix_f = mix_f;
		update_list();
	}
	else
	{
		open_location_mix(m_mix_fname);
	}
}

void CXCCMixerView::nav_clear_forward()
{
	while (!m_nav_forward.empty())
		m_nav_forward.pop();
}

void CXCCMixerView::nav_record_up(const t_nav_entry& e)
{
	m_nav_forward.push(e);
}

bool CXCCMixerView::nav_go_up()
{
	if (m_mix_f)
	{
		t_nav_entry e;
		int entered_id = m_entered_ids.empty() ? -1 : m_entered_ids.top();
		if (entered_id == -1)
		{
			e.kind = t_nav_entry::kind_disk_mix;
			e.s = m_mix_fname;
			e.id = 0;
		}
		else
		{
			e.kind = t_nav_entry::kind_nested_mix_id;
			e.s.clear();
			e.id = entered_id;
		}
		nav_record_up(e);
		m_nav_replaying = true;
		close_location(true);
		m_nav_replaying = false;
		return true;
	}
	int i = m_dir.rfind('\\');
	if (i == string::npos)
		return false;
	i = m_dir.rfind('\\', i - 1);
	if (i == string::npos)
		return false;
	t_nav_entry e;
	e.kind = t_nav_entry::kind_dir;
	e.s = m_dir;
	e.id = 0;
	nav_record_up(e);
	m_nav_replaying = true;
	open_location_dir(m_dir.substr(0, i + 1));
	m_nav_replaying = false;
	return true;
}

std::vector<std::string> CXCCMixerView::nav_segments() const
{
	std::vector<std::string> segs;
	if (!m_mix_f)
	{
		// Filesystem: split m_dir ("c:\\westwood\\data\\") into drive + folders.
		// Keep the drive ("c:") as the first segment; skip the trailing empty
		// token produced by the terminating backslash.
		string cur;
		for (char c : m_dir)
		{
			if (c == '\\' || c == '/')
			{
				if (!cur.empty())
					segs.push_back(cur);
				cur.clear();
			}
			else
				cur += c;
		}
		if (!cur.empty())
			segs.push_back(cur);
		return segs;
	}

	// MIX: walk the parent chain bottom-to-top. m_location holds the parent
	// Cmix_file* at each level (nullptr at the very bottom — the pre-MIX state),
	// m_entered_ids the file-id entered at each level (-1 for the disk-root MIX).
	// Copy the stacks so we can read them root-first.
	std::vector<Cmix_file*> parents;   // bottom (nullptr) .. immediate parent
	std::vector<int> entered;          // matching entered ids
	{
		std::stack<Cmix_file*> sl = m_location;
		std::stack<int> se = m_entered_ids;
		while (!sl.empty()) { parents.push_back(sl.top()); sl.pop(); }
		while (!se.empty()) { entered.push_back(se.top()); se.pop(); }
		std::reverse(parents.begin(), parents.end());
		std::reverse(entered.begin(), entered.end());
	}

	// First: the folder path components of the on-disk root MIX (so the user
	// can click back out to the filesystem), then the root MIX filename.
	string root_folder = Cfname(m_mix_fname).get_path();
	{
		string cur;
		for (char c : root_folder)
		{
			if (c == '\\' || c == '/')
			{
				if (!cur.empty())
					segs.push_back(cur);
				cur.clear();
			}
			else
				cur += c;
		}
		if (!cur.empty())
			segs.push_back(cur);
	}
	segs.push_back(Cfname(m_mix_fname).get_fname());

	// Then each nested MIX, named via its parent's mix database. parents[0] is
	// the nullptr pre-MIX entry and entered[0] is -1 (the root, already added),
	// so nested levels start at index 1. The parent for entered[i] is the MIX
	// open one level shallower: parents[i+1] for i+1 < size, else m_mix_f's
	// parent — but the running `cur_parent` chain is exact.
	Cmix_file* cur_parent = nullptr;
	for (size_t i = 0; i < entered.size(); i++)
	{
		int id = entered[i];
		if (id == -1)
		{
			// Disk-root MIX: its name is already shown; advance the parent
			// pointer to it for the next nested lookup.
			cur_parent = nullptr; // resolved lazily below
		}
		else if (cur_parent)
		{
			t_game g = cur_parent->get_game();
			string name = mix_database::get_name(g, id);
			if (name.empty())
				name = nh(8, id);
			segs.push_back(name);
		}
		// The MIX opened at this level becomes the parent for the next one.
		// parents[] stores the parent that was current *before* this level was
		// entered, so the MIX entered at level i is parents[i+1] when present.
		cur_parent = (i + 1 < parents.size()) ? parents[i + 1] : m_mix_f;
	}
	return segs;
}

void CXCCMixerView::nav_to_segment(int level)
{
	std::vector<std::string> segs = nav_segments();
	int last = static_cast<int>(segs.size()) - 1;
	if (level < 0 || level >= last)
		return; // already at (or past) the requested level

	int up = last - level; // number of levels to ascend
	// Each nav_go_up() drops the current level by exactly one (a nested MIX pops
	// to its parent MIX; the root MIX pops to its on-disk folder; a folder pops
	// to its parent folder), so the segment index stays in lock-step. Stop early
	// if a level can't ascend (e.g. clicking the drive root).
	for (int i = 0; i < up; i++)
		if (!nav_go_up())
			break;
}

// Count of leading filesystem (folder) segments in nav_segments(). For a pure
// filesystem location that's all of them; for a MIX location it's the number
// of path components in the root MIX's folder (the MIX itself and any nested
// MIXes follow). Used to tell folder levels from MIX levels.
static int count_folder_segments(const std::string& dir)
{
	int n = 0;
	std::string cur;
	for (char c : dir)
	{
		if (c == '\\' || c == '/')
		{
			if (!cur.empty()) n++;
			cur.clear();
		}
		else
			cur += c;
	}
	if (!cur.empty()) n++;
	return n;
}

std::string CXCCMixerView::folder_path_at_level(int level) const
{
	std::vector<std::string> segs = nav_segments();
	int folder_segs = m_mix_f
		? count_folder_segments(Cfname(m_mix_fname).get_path())
		: static_cast<int>(segs.size());
	if (level < 0 || level >= folder_segs)
		return std::string();   // MIX level or out of range
	std::string path;
	for (int i = 0; i <= level; i++)
		path += segs[i] + '\\';
	return path;
}

Cmix_file* CXCCMixerView::mix_at_level(int level) const
{
	if (!m_mix_f)
		return nullptr;
	int folder_segs = count_folder_segments(Cfname(m_mix_fname).get_path());
	int mix_index = level - folder_segs;   // 0 = root MIX, 1 = first nested, ...
	if (mix_index < 0)
		return nullptr;                    // a folder level
	// Build the open MIX chain root-first: parents (skipping the nullptr bottom)
	// then m_mix_f (the current/deepest).
	std::vector<Cmix_file*> open_mixes;
	{
		std::stack<Cmix_file*> sl = m_location;
		std::vector<Cmix_file*> parents;
		while (!sl.empty()) { parents.push_back(sl.top()); sl.pop(); }
		std::reverse(parents.begin(), parents.end());
		for (size_t i = 1; i < parents.size(); i++)   // skip parents[0] == nullptr
			open_mixes.push_back(parents[i]);
		open_mixes.push_back(m_mix_f);
	}
	if (mix_index >= static_cast<int>(open_mixes.size()))
		return nullptr;
	return open_mixes[mix_index];
}

// Set of archive extensions we treat as navigable MIX-likes. Shared by the
// breadcrumb child dropdown and the editable-path navigation.
static bool is_archive_ext(const std::string& ext_lower)
{
	return ext_lower == ".mix" || ext_lower == ".big" || ext_lower == ".dat"
		|| ext_lower == ".pak" || ext_lower == ".pkg" || ext_lower == ".mmx"
		|| ext_lower == ".yro" || ext_lower == ".bag";
}

std::vector<CXCCMixerView::t_nav_child> CXCCMixerView::nav_children(int level) const
{
	std::vector<t_nav_child> out;

	std::string folder = folder_path_at_level(level);
	if (!folder.empty())
	{
		// Filesystem level: list subfolders + navigable archive files.
		WIN32_FIND_DATA fd;
		HANDLE h = FindFirstFile((folder + "*").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				std::string name = fd.cFileName;
				if (name == "." || name == "..")
					continue;
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					t_nav_child c; c.name = name; c.is_mix_child = false;
					out.push_back(c);
				}
				else
				{
					if (is_archive_ext(to_lower(Cfname(name).get_fext())))
					{
						t_nav_child c; c.name = name; c.is_mix_child = false;
						out.push_back(c);
					}
				}
			}
			while (FindNextFile(h, &fd));
			FindClose(h);
		}
	}
	else if (Cmix_file* mf = mix_at_level(level))
	{
		// MIX level: list nested archive entries inside that open MIX.
		t_game g = mf->get_game();
		for (int i = 0; i < mf->get_c_files(); i++)
		{
			int id = mf->get_id(i);
			t_file_type ft = mf->get_type(id);
			if (ft == ft_mix || ft == ft_big || ft == ft_mix_rg || ft == ft_pak)
			{
				std::string name = mix_database::get_name(g, id);
				if (name.empty())
					name = nh(8, id);
				t_nav_child c; c.name = name; c.is_mix_child = true; c.id = id;
				out.push_back(c);
			}
		}
	}

	std::sort(out.begin(), out.end(),
		[](const t_nav_child& a, const t_nav_child& b)
		{ return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
	return out;
}

void CXCCMixerView::nav_descend(int level, const t_nav_child& child)
{
	// First climb to the chosen level (no-op if already there), then descend
	// into the picked child — same primitives as a click + double-click.
	nav_to_segment(level);
	if (child.is_mix_child)
	{
		// We're now at the MIX `level`; the child is one of its nested entries.
		if (m_mix_f)
			open_location_mix(child.id);
	}
	else
	{
		// Filesystem child: a subfolder or an archive file under folder `level`.
		std::string folder = folder_path_at_level(level);
		if (folder.empty())
			return;
		std::string full = folder + child.name;
		if (is_archive_ext(to_lower(Cfname(child.name).get_fext())))
			open_location_mix(full);
		else
			open_location_dir(full + '\\');   // subfolder
	}
}

std::string CXCCMixerView::nav_current_path() const
{
	// Inside a MIX: the on-disk root MIX path (the canonical thing the user can
	// paste back). On the filesystem: the current directory.
	if (m_mix_f)
		return m_mix_fname;
	return m_dir;
}

bool CXCCMixerView::nav_open_path(const std::string& path)
{
	// Trim whitespace + surrounding quotes (Explorer's "Copy as path" quotes).
	std::string p = path;
	size_t a = p.find_first_not_of(" \t\r\n\"");
	size_t b = p.find_last_not_of(" \t\r\n\"");
	if (a == std::string::npos)
		return false;
	p = p.substr(a, b - a + 1);
	if (p.empty())
		return false;

	DWORD attr = ::GetFileAttributesA(p.c_str());
	if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		// Existing folder: ensure a trailing backslash (update_list globs m_dir + "*").
		if (p.back() != '\\' && p.back() != '/')
			p += '\\';
		close_all_locations();
		open_location_dir(p);
		return true;
	}
	if (attr != INVALID_FILE_ATTRIBUTES)
	{
		// Existing file: only navigable if it's an archive we can open as a MIX.
		if (is_archive_ext(to_lower(Cfname(p).get_fext())))
		{
			close_all_locations();
			open_location_mix(p);
			return true;
		}
		return false;   // a regular file — nothing to navigate into
	}
	return false;       // path doesn't exist
}

bool CXCCMixerView::nav_go_forward()
{
	if (m_nav_forward.empty())
		return false;
	t_nav_entry e = m_nav_forward.top();
	m_nav_forward.pop();
	m_nav_replaying = true;
	switch (e.kind)
	{
	case t_nav_entry::kind_dir:
		open_location_dir(e.s);
		break;
	case t_nav_entry::kind_disk_mix:
		open_location_mix(e.s);
		break;
	case t_nav_entry::kind_nested_mix_id:
		if (m_mix_f)
			open_location_mix(e.id);
		break;
	}
	m_nav_replaying = false;
	return true;
}

void CXCCMixerView::OnXButtonUp(UINT nFlags, UINT nButton, CPoint point)
{
	if (nButton == XBUTTON1)
		nav_go_up();
	else if (nButton == XBUTTON2)
		nav_go_forward();
	else
		CListView::OnXButtonUp(nFlags, nButton, point);
}

// ===========================================================================
// Cross-pane drag & drop
//
// The listview already selects the item on button-down, then fires LVN_BEGINDRAG
// when the user starts dragging it. We take over with a manual CImageList drag:
// the ghost image is locked to the desktop window so it can cross the splitter
// into the other pane. On button-up, if the cursor sits over the OTHER mix pane,
// the snapshotted selection is handed to copy_as(-1) -- the same raw-copy path
// the right-click "Copy" command uses, which extracts from a MIX source, writes
// to a folder destination, and inserts into a MIX destination as appropriate.
// No temp files, no OLE / CF_HDROP marshalling.
// ===========================================================================

CXCCMixerView* CXCCMixerView::pane_under_point(CPoint screen) const
{
	CWnd* w = WindowFromPoint(screen);
	while (w)
	{
		HWND h = w->GetSafeHwnd();
		if (h == GetSafeHwnd())
			return const_cast<CXCCMixerView*>(this);
		if (m_other_pane && h == m_other_pane->GetSafeHwnd())
			return m_other_pane;
		w = w->GetParent();
	}
	return nullptr;
}

void CXCCMixerView::cancel_drag_visual()
{
	if (!m_dragging)
		return;
	m_dragging = false;
	CImageList::DragLeave(CWnd::GetDesktopWindow());
	CImageList::EndDrag();
	delete m_drag_image;
	m_drag_image = nullptr;
}

void CXCCMixerView::OnBeginDrag(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	NM_LISTVIEW* nm = reinterpret_cast<NM_LISTVIEW*>(pNMHDR);
	// Need a real, draggable selection and a second pane to drop on. can_delete()
	// populates m_index_selected from the listview selection and rejects the
	// anchor rows ("..", "Browse...", drives), which we must never drag/copy.
	if (m_dragging || !m_other_pane || !can_delete() || m_index_selected.empty())
		return;
	m_drag_sel = m_index_selected;

	CPoint origin;
	m_drag_image = GetListCtrl().CreateDragImage(nm->iItem, &origin);
	if (!m_drag_image)
	{
		m_drag_sel.clear();
		return;
	}
	// Hotspot = cursor offset within the item image at drag start. The ghost is
	// locked to the desktop, so DragEnter/DragMove use screen coordinates.
	CPoint action(nm->ptAction);
	m_drag_image->BeginDrag(0, CPoint(action.x - origin.x, action.y - origin.y));
	CPoint screen(action);
	ClientToScreen(&screen);
	m_drag_image->DragEnter(CWnd::GetDesktopWindow(), screen);
	SetCapture();
	m_dragging = true;
}

void CXCCMixerView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_dragging)
	{
		// Manual capture doesn't auto-handle Escape -- cancel the drag on it.
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			m_drag_sel.clear();
			ReleaseCapture();   // -> OnCaptureChanged tears down the visual; no drop
			return;
		}
		CPoint screen(point);
		ClientToScreen(&screen);
		CImageList::DragMove(screen);
		// Feedback: a plain arrow over a valid drop pane, "no" elsewhere. We hold
		// capture, so WM_SETCURSOR doesn't fire -- set it here each move.
		::SetCursor(::LoadCursor(NULL, pane_under_point(screen) == m_other_pane ? IDC_ARROW : IDC_NO));
		return;
	}
	CListView::OnMouseMove(nFlags, point);
}

void CXCCMixerView::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragging)
	{
		CPoint screen(point);
		ClientToScreen(&screen);
		CXCCMixerView* target = pane_under_point(screen);
		cancel_drag_visual();            // clears m_dragging, tears down the image list
		if (GetCapture() == this)
			ReleaseCapture();
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		if (target == m_other_pane && !m_drag_sel.empty())
		{
			m_index_selected = m_drag_sel;
			copy_as(static_cast<t_file_type>(-1));   // raw copy into the other pane
		}
		m_drag_sel.clear();
		return;
	}
	CListView::OnLButtonUp(nFlags, point);
}

void CXCCMixerView::OnCaptureChanged(CWnd* pWnd)
{
	// Capture yanked away mid-drag (Escape release, focus steal, a popup, ...):
	// abort the visual without performing a drop.
	if (m_dragging)
	{
		cancel_drag_visual();
		m_drag_sel.clear();
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
	}
	CListView::OnCaptureChanged(pWnd);
}

// --- Auto-refresh (folder views) --------------------------------------------

static const UINT_PTR k_watch_timer_id = 0x5743;   // per-view, arbitrary ('WC')

void CXCCMixerView::arm_dir_watch()
{
	// Off via the Configure > Auto-refresh toggle, MIX panes (held in memory) and
	// the pre-navigation empty state aren't watched.
	if (!theme::auto_refresh() || m_mix_f || m_dir.empty())
	{
		stop_dir_watch();
		return;
	}
	// Already watching this exact folder -- keep the live handle so our own
	// auto-refresh doesn't tear it down and rebuild it every change.
	if (m_watch_handle != INVALID_HANDLE_VALUE && m_watch_dir == m_dir)
		return;
	stop_dir_watch();
	HANDLE h = FindFirstChangeNotification(m_dir.c_str(), FALSE,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
		FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
		FILE_NOTIFY_CHANGE_ATTRIBUTES);
	if (h == INVALID_HANDLE_VALUE)
		return;   // unwatchable volume (some network/removable): manual Refresh still works
	m_watch_handle = h;
	m_watch_dir = m_dir;
	m_watch_pending = false;
	if (!m_watch_timer)
		m_watch_timer = (SetTimer(k_watch_timer_id, 500, nullptr) != 0);
}

void CXCCMixerView::stop_dir_watch()
{
	if (m_watch_handle != INVALID_HANDLE_VALUE)
	{
		FindCloseChangeNotification(m_watch_handle);
		m_watch_handle = INVALID_HANDLE_VALUE;
	}
	m_watch_dir.clear();
	m_watch_pending = false;
	if (m_watch_timer)
	{
		KillTimer(k_watch_timer_id);
		m_watch_timer = false;
	}
}

void CXCCMixerView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == k_watch_timer_id)
	{
		if (m_watch_handle != INVALID_HANDLE_VALUE)
		{
			if (WaitForSingleObject(m_watch_handle, 0) == WAIT_OBJECT_0)
			{
				// A change landed. Re-arm for the next one and defer the rebuild
				// to the first quiet tick, so a burst (a large copy, an archive
				// extraction) collapses into a single refresh once it settles.
				FindNextChangeNotification(m_watch_handle);
				m_watch_pending = true;
			}
			else if (m_watch_pending && !m_dragging)
			{
				m_watch_pending = false;
				refresh_preserving();
			}
		}
		return;
	}
	CListView::OnTimer(nIDEvent);
}

void CXCCMixerView::refresh_preserving()
{
	if (m_mix_f)   // only folders are watched; guard anyway
		return;
	CListCtrl& lc = GetListCtrl();
	// Snapshot selection + focus by id (stable across the rebuild) and the scroll
	// position by top row, so an automatic refresh doesn't yank the user's place.
	vector<int> sel_ids;
	int focus_id = -1;
	int i = lc.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	while (i != -1)
	{
		sel_ids.push_back(lc.GetItemData(i));
		i = lc.GetNextItem(i, LVNI_ALL | LVNI_SELECTED);
	}
	int f = lc.GetNextItem(-1, LVNI_FOCUSED);
	if (f != -1)
		focus_id = lc.GetItemData(f);
	int top = lc.GetTopIndex();
	int per_page = lc.GetCountPerPage();

	// Re-read the folder without disturbing the filter or sort, then rebuild the
	// (possibly filtered) rows in the current sort order. m_reading drives OnIdle
	// to re-resolve file types of any newly seen entries.
	read_dir_into_index();
	SetRedraw(false);
	lc.DeleteAllItems();
	insert_filtered_rows();
	sort_list(m_sort_column < 0 ? 1 : m_sort_column, m_sort_reverse);

	// Restore selection + focus by matching item data. Preview is suppressed so a
	// background pane's refresh doesn't hijack the file-view pane.
	m_suppress_sel_preview = true;
	int n = lc.GetItemCount();
	int focus_row = -1;
	for (int r = 0; r < n; r++)
	{
		int id = lc.GetItemData(r);
		if (id == focus_id)
		{
			lc.SetItemState(r, LVIS_FOCUSED, LVIS_FOCUSED);
			focus_row = r;
		}
		for (int s : sel_ids)
		{
			if (s == id)
			{
				lc.SetItemState(r, LVIS_SELECTED, LVIS_SELECTED);
				break;
			}
		}
	}
	m_suppress_sel_preview = false;
	SetRedraw(true);

	// Restore scroll: pin the previous top row, then keep the focused row visible.
	if (n > 0)
	{
		lc.EnsureVisible(min(top + per_page - 1, n - 1), FALSE);
		lc.EnsureVisible(min(top, n - 1), FALSE);
		if (focus_row != -1)
			lc.EnsureVisible(focus_row, FALSE);
	}
	lc.Invalidate();
	m_reading = true;
}

void CXCCMixerView::clear_list()
{
	GetListCtrl().DeleteAllItems();
	m_index.clear();
}

string totalSize(size_t i)
{
	static const char* sizenames[] = { " B", " KB", " MB", " GB" };
	size_t div = 0;
	size_t rem = 0;
	while (i >= 1024 && div < (sizeof sizenames / sizeof * sizenames))
	{
		rem = (i % 1024);
		div++;
		i /= 1024;
	}
	return n((float)i + (float)rem / 1024.0) + sizenames[div];
}

void CXCCMixerView::update_list()
{
	DragAcceptFiles(can_edit());
	clear_list();
	// Navigation resets the filter: every location change funnels through
	// update_list, so each new MIX/folder starts showing all files. The frame
	// owns the filter edit; it re-syncs the edit's text to this pane's (now
	// empty) filter when the active pane changes or after a nav (sync_filter_ui).
	m_filter_text.clear();
	t_index_entry e;
	m_palette_loaded = false;

	if (m_mix_f)
	{
		e.name = "..";
		e.ft = ft_dir;
		e.size = "";
		e.size_bytes = -1;
		e.description = "";
		m_index[0] = e;

		m_game = m_mix_f->get_game();
		for (int i = 0; i < m_mix_f->get_c_files(); i++)
		{
			int id = m_mix_f->get_id(i);
			e.description = mix_database::get_description(m_game, id);
			e.ft = m_mix_f->get_type(id);
			e.name = mix_database::get_name(m_game, id);
			if (e.name.empty())
				e.name = nh(8, id);
			long long sz = m_mix_f->get_size(id);
			e.size = totalSize(sz);
			e.size_bytes = sz;
			m_index[id] = e;
			if (e.ft == ft_pal)
			{
				m_mix_f->get_vdata(id).read(m_palette);
				m_palette_loaded = true;
			}
		}
	}
	else
		read_dir_into_index();
	SetRedraw(false);
	insert_filtered_rows();
	sort_list(1, false);
	SetRedraw(true);
	autosize_colums();
	m_reading = true;
	// Navigation cleared m_filter_text above; tell the frame to refresh the
	// shared filter edit so it doesn't keep showing the stale text. Safe before
	// the edit exists (guarded) and non-recursive (empty text => set_filter
	// no-ops). Only for the active pane, so a background pane's update_list
	// doesn't stomp the focused pane's edit.
	if (CMainFrame* mf = GetMainFrame())
		mf->sync_filter_ui();
	// (Re)point the directory watcher at this location: a folder arms a watch on
	// m_dir, a MIX stops it. Navigation always funnels through update_list, so
	// this keeps the watch following wherever the pane goes.
	arm_dir_watch();
	update_banner_label();
	// Repaint the reserved NC strip so the banner reflects the new location.
	if (GetSafeHwnd())
		RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
}

void CXCCMixerView::read_dir_into_index()
{
	// Folder branch of update_list, factored out: rebuild m_index from the
	// on-disk listing of m_dir. Does NOT touch the filter, sort or selection so
	// the auto-refresh can re-read silently. File types are left as the -1
	// placeholder and resolved lazily by OnIdle while m_reading is set.
	m_index.clear();
	m_game = game_ts;
	m_palette_loaded = false;
	t_index_entry e;
	e.name = "Browse...";
	e.ft = ft_drive;
	e.size = "";
	e.description = "";
	m_index[0] = e;
	WIN32_FIND_DATA finddata;
	HANDLE findhandle = FindFirstFile((m_dir + "*").c_str(), &finddata);
	if (findhandle != INVALID_HANDLE_VALUE)
	{
		do
		{
			e.name = finddata.cFileName;
			e.size = "";
			e.size_bytes = 0;
			if (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (e.name == ".")
					continue;
				e.ft = ft_dir;
				e.size_bytes = -1;
			}
			else
			{
				e.ft = static_cast<t_file_type>(-1);
				e.size_bytes = (static_cast<long long>(finddata.nFileSizeHigh) << 32) | finddata.nFileSizeLow;
				e.size = totalSize(e.size_bytes);
			}
			int id = Cmix_file::get_id(get_game(), finddata.cFileName);
			e.description = mix_database::get_description(get_game(), id);
			m_index[id] = e;
		}
		while (FindNextFile(findhandle, &finddata));
		FindClose(findhandle);
	}
}

void CXCCMixerView::insert_filtered_rows()
{
	CListCtrl& lc = GetListCtrl();
	for (auto& i : m_index)
	{
		// id 0 is the pinned anchor row (".." inside a MIX, "Browse..." on
		// disk) — always shown so the user can still navigate while filtering.
		if (i.first != 0 && !m_filter_text.empty()
			&& !fname_filter(i.second.name, m_filter_text))
			continue;
		lc.SetItemData(lc.InsertItem(lc.GetItemCount(), LPSTR_TEXTCALLBACK), i.first);
	}
}

void CXCCMixerView::move_selection(int step)
{
	CListCtrl& lc = GetListCtrl();
	if (lc.GetItemCount() <= 0)
		return;
	int sel = lc.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	if (sel < 0)
		sel = 0;
	else
		sel = max(0, min(sel + step, lc.GetItemCount() - 1));
	lc.SetItemState(sel, LVIS_SELECTED | LVIS_FOCUSED,
		LVIS_SELECTED | LVIS_FOCUSED);
	lc.EnsureVisible(sel, FALSE);
}

void CXCCMixerView::set_filter(const string& filter)
{
	if (filter == m_filter_text)
		return;
	m_filter_text = filter;
	CListCtrl& lc = GetListCtrl();
	SetRedraw(false);
	lc.DeleteAllItems();
	insert_filtered_rows();
	sort_list(m_sort_column, m_sort_reverse);
	SetRedraw(true);
	lc.Invalidate();
}

void CXCCMixerView::autosize_colums()
{
	SetRedraw(false);
	for (int i = 0; i < GetListCtrl().GetHeaderCtrl()->GetItemCount(); i++)
		GetListCtrl().SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
	SetRedraw(true);
	// Wire the right-click-header column-visibility menu and re-apply
	// persisted hidden state. Idempotent — header subclass install is
	// guarded, and the hidden-state restore is a no-op for columns whose
	// width is already 0. Called from here (not OnInitialUpdate) because
	// the autosize above is what gives columns their real "last visible"
	// widths to remember.
	const char* lv_id =
		(m_reg_key == "left_mix_pane") ? "main_pane_left" :
		(m_reg_key == "right_mix_pane") ? "main_pane_right" : NULL;
	if (lv_id)
		theme::enable_column_visibility_menu(GetListCtrl().GetSafeHwnd(), lv_id);
}

void CXCCMixerView::OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
	m_buffer[++m_buffer_w &= 3].erase();
	const t_index_entry& e = find_ref(m_index, pDispInfo->item.lParam);
	switch (pDispInfo->item.iSubItem)
	{
	case 0:
		m_buffer[m_buffer_w] = e.name;
		break;
	case 1:
		if (e.ft != -1)
			m_buffer[m_buffer_w] = ft_name[e.ft];
		break;
	case 2:
		m_buffer[m_buffer_w] = e.size_bytes > 0 ? theme::format_size(e.size_bytes) : std::string();
		break;
	case 3:
		m_buffer[m_buffer_w] = e.description;
		break;
	}
	pDispInfo->item.pszText = const_cast<char*>(m_buffer[m_buffer_w].c_str());
	*pResult = 0;
}

template <class T>
static int compare(const T& a, const T& b)
{
	return a < b ? -1 : a != b;
}

int CXCCMixerView::compare(int id_a, int id_b) const
{
	const t_index_entry& ra = find_ref(m_index, id_a);
	const t_index_entry& rb = find_ref(m_index, id_b);
	bool a_anchor = ra.ft == ft_drive || ra.name == "..";
	bool b_anchor = rb.ft == ft_drive || rb.name == "..";
	if (a_anchor && !b_anchor)
		return -1;
	if (!a_anchor && b_anchor)
		return 1;
	if (a_anchor && b_anchor)
	{
		if (ra.name == ".." && rb.ft == ft_drive)
			return 1;
		if (ra.ft == ft_drive && rb.name == "..")
			return -1;
		return 0;
	}
	if (m_sort_reverse)
		swap(id_a, id_b);
	const t_index_entry& a = find_ref(m_index, id_a);
	const t_index_entry& b = find_ref(m_index, id_b);
	if (a.ft != b.ft)
	{
		if (a.ft == ft_dir || a.ft == ft_lnkdir)
			return -1;
		if (b.ft == ft_dir || b.ft == ft_lnkdir)
			return 1;
	}
	int r = 0;
	switch (m_sort_column)
	{
	case 0:
		r = ::compare(to_lower(a.name), to_lower(b.name));
		break;
	case 1:
		r = ::compare(a.ft, b.ft);
		break;
	case 2:
		r = ::compare(a.size_bytes, b.size_bytes);
		break;
	case 3:
		r = ::compare(a.description, b.description);
		break;
	}
	// Tie-breaker: when the primary key compares equal, fall back to name so
	// items within a type/size/description group stay alphabetized. This used
	// to be done by running sort_list twice (column 0 then the real column);
	// folding it into the comparator lets update_list() sort once.
	if (r == 0 && m_sort_column != 0)
		r = ::compare(to_lower(a.name), to_lower(b.name));
	return r;
}

static int CALLBACK Compare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	return reinterpret_cast<CXCCMixerView*>(lParamSort)->compare(lParam1, lParam2);
}

void CXCCMixerView::sort_list(int i, bool reverse)
{
	m_sort_column = i;
	m_sort_reverse = reverse;
	GetListCtrl().SortItems(Compare, reinterpret_cast<DWORD_PTR>(this));
}

void CXCCMixerView::OnColumnclick(NMHDR* pNMHDR, LRESULT* pResult)
{
	int column = reinterpret_cast<NM_LISTVIEW*>(pNMHDR)->iSubItem;
	bool default_reverse = (column == 2);
	sort_list(column, column == m_sort_column ? !m_sort_reverse : default_reverse);
	*pResult = 0;
}

void CXCCMixerView::OnDestroy()
{
	cancel_drag_visual();   // safety: don't leak the drag image if torn down mid-drag
	stop_dir_watch();       // close the folder-watch handle + kill its timer
	AfxGetApp()->WriteProfileString(m_reg_key, "path", m_dir.c_str());
	// Remember the root on-disk MIX so the pane reopens it next session. For a
	// nested MIX the root is the front of the fname stack; otherwise m_mix_fname.
	// Empty when browsing a folder, so the folder path (above) is used instead.
	string root_mix = m_mix_f
		? (m_mix_fname_stack.empty() ? m_mix_fname : m_mix_fname_stack.front())
		: string();
	AfxGetApp()->WriteProfileString(m_reg_key, "mix", root_mix.c_str());
	close_all_locations();
}

void CXCCMixerView::OnDblclk(NMHDR* pNMHDR, LRESULT* pResult)
{
	int id = get_current_id();
	// Inline banner mode paints the mode banner over the pinned anchor row
	// (item data 0). Double-clicking the banner copies the current path
	// instead of navigating. Strip mode handles this in OnNcLButtonDblClk.
	if (id == 0 && theme::banner_mode_v() == theme::banner_inline)
	{
		copy_banner_path();
		*pResult = 0;
		return;
	}
	if (id != -1)
		open_item(id);
	*pResult = 0;
}

void CXCCMixerView::OnItemchanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	NM_LISTVIEW* pNMListView = reinterpret_cast<NM_LISTVIEW*>(pNMHDR);
	// Programmatic re-selection during an auto-refresh must not re-open the
	// preview (it would steal the file-view pane from the active pane).
	if (m_suppress_sel_preview)
	{
		*pResult = 0;
		return;
	}
	if (pNMListView->uNewState & LVIS_FOCUSED)
	{
		LV_ITEM lvi;
		lvi.mask = LVIF_PARAM;
		lvi.iItem = pNMListView->iItem;
		GetListCtrl().GetItem(&lvi);
		if (m_mix_f)
			m_file_view_pane->open_f(lvi.lParam, *m_mix_f, m_game, m_palette_loaded ? m_palette : NULL);
		else
		{
			const t_index_entry& index = m_index[lvi.lParam];
			if (index.ft != ft_dir && index.ft != ft_drive && index.ft != ft_lnkdir)
				m_file_view_pane->open_f(m_dir + index.name);
		}
	}
	*pResult = 0;
}

int CXCCMixerView::get_current_id() const
{
	int i = get_current_index();
	return i == -1 ? -1 : get_id(i);
}

int CXCCMixerView::get_current_index() const
{
	return GetListCtrl().GetNextItem(-1, LVNI_ALL | LVNI_FOCUSED);
}

int CXCCMixerView::get_id(int i) const
{
	return GetListCtrl().GetItemData(i);
}

void CXCCMixerView::set_other_panes(CXCCFileView* file_view_pane, CXCCMixerView* other_pane)
{
	m_file_view_pane = file_view_pane;
	m_other_pane = other_pane;
}

void CXCCMixerView::OnContextMenu(CWnd*, CPoint point)
{
		if (point.x == -1 && point.y == -1)
		{
			// keystroke invocation
			CRect rect;
			GetClientRect(rect);
			ClientToScreen(rect);

			point = rect.TopLeft();
			point.Offset(5, 5);
		}

		CMenu menu;
		VERIFY(menu.LoadMenu(CG_IDR_POPUP_MIX_VIEW));

		CMenu* pPopup = menu.GetSubMenu(0);
		ASSERT(pPopup != NULL);
		CWnd* pWndPopupOwner = this;

		while (pWndPopupOwner->GetStyle() & WS_CHILD)
			pWndPopupOwner = pWndPopupOwner->GetParent();

		pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y,
			pWndPopupOwner);
}

void CXCCMixerView::OnFileFound(UINT ID)
{
	close_all_locations();
	open_location_mix(GetMainFrame()->get_mix_name(ID - ID_FILE_FOUND_MIX000));
}

int CXCCMixerView::open_f_id(Ccc_file& f, int id) const
{
	return m_mix_f ? f.open(id, *m_mix_f) : f.open(m_dir + find_ref(m_index, id).name);
}

int CXCCMixerView::open_f_index(Ccc_file& f, int i) const
{
	return open_f_id(f, GetListCtrl().GetItemData(i));
}

int CXCCMixerView::vload_f_index(Ccc_file& f, int i) const
{
	Cvirtual_binary d = get_vdata(i);
	if (!d.data() || d.size() <= 0)
		return 1;
	f.load(d);
	return 0;
}

Cvirtual_binary CXCCMixerView::get_vdata_id(int id) const
{
	return m_mix_f ? m_mix_f->get_vdata(id) : Cvirtual_binary(m_dir + find_ref(m_index, id).name);
}

Cvirtual_binary CXCCMixerView::get_vdata(int i) const
{
	return get_vdata_id(GetListCtrl().GetItemData(i));
}

Cvirtual_image CXCCMixerView::get_vimage_id(int id) const
{
	Cvirtual_image d;
	switch (find_ref(m_index, id).ft)
	{
	case ft_cps:
		{
			Ccps_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
		}
		break;
	case ft_dds:
		{
			Cdds_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
			d.remove_alpha();
		}
		break;
	case ft_jpeg:
	case ft_pcx:
	case ft_png:
	case ft_tga:
		d.load(get_vdata_id(id));
		break;
	case ft_shp:
		{
			Cshp_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
		}
		break;
	case ft_shp_ts:
		{
			Cshp_ts_file f;
			f.load(get_vdata_id(id));
			d = f.extract_as_pcx_single(get_default_palette(), GetMainFrame()->combine_shadows(), static_cast<Cshp_ts_file::shadow_style>(GetMainFrame()->shadow_style()));
		}
		break;
	case ft_tmp_ra:
		{
			Ctmp_ra_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
		}
		break;
	case ft_tmp_ts:
		{
			Ctmp_ts_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
		}
		break;
	case ft_wsa_dune2:
		{
			Cwsa_dune2_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
		}
		break;
	case ft_wsa:
		{
			Cwsa_file f;
			f.load(get_vdata_id(id));
			d = f.vimage();
		}
		break;
	}
	if (d.cb_pixel() == 1 && !d.palette())
		d.palette(get_default_palette(), true);
	return d;
}

Cvirtual_image CXCCMixerView::get_vimage(int i) const
{
	return get_vimage_id(GetListCtrl().GetItemData(i));
}

void CXCCMixerView::OnPopupExtract()
{
	int id = get_current_id();
	CFileDialog dlg(false, NULL, NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, "All files (*.*)|*.*|", this);
	char s[MAX_PATH];
	strcpy(s, find_ref(m_index, id).name.c_str());
	dlg.m_ofn.lpstrFile = s;
	if (IDOK != dlg.DoModal())
		return;
	Ccc_file f(false);
	if (!open_f_index(f, get_current_index()))
		f.extract(static_cast<string>(dlg.GetPathName()));
}

void CXCCMixerView::OnUpdatePopupExtract(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(get_current_id() != -1);
}

void CXCCMixerView::batch_extract(bool preserve)
{
	if (m_index_selected.empty())
		return;
	CFolderPickerDialog dlg(NULL, 0, this);
	if (dlg.DoModal() != IDOK)
		return;
	string out_dir = static_cast<string>(dlg.GetPathName());
	if (out_dir.empty())
		return;
	if (out_dir.back() != '\\' && out_dir.back() != '/')
		out_dir += '\\';
	if (preserve && m_mix_f && !m_mix_fname.empty())
	{
		string sub = Cfname(m_mix_fname).get_fname();
		if (!sub.empty())
		{
			create_deep_dir(out_dir, sub + '\\');
			out_dir += sub + '\\';
		}
	}
	CWaitCursor wait;
	if (theme::parallel_extract())
	{
		// Two-phase parallel extract.
		//
		// Phase 1 (UI thread, serial): pull each selected entry's bytes out
		// of the MIX into a Cvirtual_binary. Must be serial because every
		// Ccc_file from the same Cmix_file shares the parent's HANDLE
		// (cc_file.cpp::open(int, Cmix_file&) does m_f = mix_f.m_f), so
		// concurrent seek+read would race on file position.
		//
		// Phase 2 (OpenMP): write each blob to disk. file32_write opens its
		// own handle per call and Cvirtual_binary's data()/size() are pure
		// const reads on already-allocated memory, so writes are independent.
		// Throughput gain shows up on a fast SSD; spinning disks stay roughly
		// serial because the bottleneck is destination I/O, not encode.
		struct extract_job { string out_path; Cvirtual_binary data; };
		std::vector<extract_job> jobs;
		jobs.reserve(m_index_selected.size());
		for (auto& i : m_index_selected)
		{
			int id = get_id(i);
			auto it = m_index.find(id);
			if (it == m_index.end())
				continue;
			const string& src_name = it->second.name;
			if (src_name.empty() || src_name == ".." || src_name == "Browse...")
				continue;
			Ccc_file f(false);
			if (open_f_id(f, id))
				continue;
			// Force the bytes into memory before leaving the UI thread.
			// vdata() returns the cached blob if extract's fast path would
			// hit it; otherwise read() pulls bytes through the shared MIX
			// handle (still serial here — that's the point).
			if (!f.data())
				f.read();
			string out_name = src_name;
			for (size_t j = 0; j < out_name.size(); j++)
				if (out_name[j] == '\\' || out_name[j] == '/')
					out_name[j] = '_';
			jobs.push_back({ out_dir + out_name, f.vdata() });
		}
		const int n = static_cast<int>(jobs.size());
		#pragma omp parallel for schedule(dynamic)
		for (int k = 0; k < n; k++)
			jobs[k].data.save(jobs[k].out_path);
	}
	else
	{
		for (auto& i : m_index_selected)
		{
			int id = get_id(i);
			auto it = m_index.find(id);
			if (it == m_index.end())
				continue;
			const string& src_name = it->second.name;
			if (src_name.empty() || src_name == ".." || src_name == "Browse...")
				continue;
			Ccc_file f(false);
			if (open_f_id(f, id))
				continue;
			string out_name = src_name;
			for (size_t j = 0; j < out_name.size(); j++)
				if (out_name[j] == '\\' || out_name[j] == '/')
					out_name[j] = '_';
			f.extract(out_dir + out_name);
		}
	}
	AfxMessageBox("Extraction successful.", MB_OK | MB_ICONINFORMATION);
}

void CXCCMixerView::OnPopupBatchExtract()
{
	batch_extract(false);
}

void CXCCMixerView::OnUpdatePopupBatchExtract(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(!m_index_selected.empty());
}

void CXCCMixerView::OnPopupBatchExtractPreserve()
{
	batch_extract(true);
}

void CXCCMixerView::OnUpdatePopupBatchExtractPreserve(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(!m_index_selected.empty() && m_mix_f != nullptr);
}

const t_palette_entry* CXCCMixerView::get_default_palette() const
{
	const t_palette_entry* p = GetMainFrame()->get_pal_data();
	return p ? p : m_palette;
}

bool CXCCMixerView::can_accept() const
{
	//return !m_mix_f;
	return true;
}

bool CXCCMixerView::can_edit() const
{
	// Disk-root MIX (m_location.size()==1) edits in place. Nested MIXes edit a
	// temp copy that is re-injected into the parent on the way up (see
	// t_nested_edit). Both are editable.
	// Folder view (no MIX open, but a real filesystem dir) is also editable:
	// OnPopupDelete's !del_into_mix branch deletes the selected files/dirs on
	// disk directly. Only the degenerate no-mix/no-dir case is not editable.
	return m_location.size() >= 1 || (!m_mix_f && !m_dir.empty());
}

string CXCCMixerView::get_dir() const
{
	return m_dir;
}

void CXCCMixerView::set_reg_key(const string& v)
{
	m_reg_key = v.c_str();
}

static bool can_convert(t_file_type s, t_file_type d)
{
	switch (s)
	{
	case ft_aud:
	case ft_voc:
		return d == ft_wav_pcm;
	case ft_cps:
	case ft_dds:
	case ft_jpeg:
	case ft_pcx:
	case ft_png:
	case ft_tga:
	case ft_shp:
	case ft_tmp_ra:
	case ft_tmp_ts:
	case ft_wsa_dune2:
	case ft_wsa:
		return d == ft_clipboard
			|| d == ft_cps
			|| d == ft_jpeg
			|| d == ft_map_ts_preview
			|| d == ft_pal
			|| d == ft_pal_jasc
			|| d == ft_pcx
			|| d == ft_png
			|| d == ft_shp
			|| d == ft_shp_ts
			|| d == ft_tga
			|| d == ft_vxl;
	case ft_hva:
		return d == ft_csv;
	case ft_pal:
		return d == ft_pal_jasc
			|| d == ft_text;
	case ft_mix:
	case ft_st:
		return d == ft_text;
	case ft_shp_dune2:
		return d == ft_jpeg
			|| d == ft_pcx
			|| d == ft_png
			|| d == ft_tga;
	case ft_shp_ts:
		return d == ft_clipboard
			|| d == ft_jpeg
			|| d == ft_jpeg_single
			|| d == ft_pcx
			|| d == ft_pcx_single
			|| d == ft_png
			|| d == ft_png_single
			|| d == ft_tga
			|| d == ft_tga_single;
	case ft_text:
		return d == ft_html
			|| d == ft_hva
			|| d == ft_vxl;
	case ft_vqa:
		return d == ft_avi
			|| d == ft_jpeg
			|| d == ft_pcx
			|| d == ft_png
			|| d == ft_tga
			|| d == ft_wav_pcm;
	case ft_vxl:
		return d == ft_jpeg
			|| d == ft_pcx
			|| d == ft_png
			|| d == ft_text
			|| d == ft_tga
			|| d == ft_xif;
	case ft_wav:
		return d == ft_aud
			|| d == ft_wav_ima_adpcm
			|| d == ft_wav_pcm;
	case ft_xif:
		return d == ft_html
			|| d == ft_text
			|| d == ft_vxl;
	}
	return false;
}

bool CXCCMixerView::can_copy()
{
	return can_delete() && m_other_pane->can_accept();
}

bool CXCCMixerView::can_delete()
{
	m_index_selected.clear();
	int i = GetListCtrl().GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
	while (i != -1)
	{
		int id = GetListCtrl().GetItemData(i);
		const t_index_entry& index = find_ref(m_index, id);
		if (index.ft == ft_drive || index.name.empty() || (index.ft == ft_dir && (index.name == "." || index.name == "..")))
			return false;
		m_index_selected.push_back(i);
		i = GetListCtrl().GetNextItem(i, LVNI_ALL | LVNI_SELECTED);
	}
	return true;
}

bool CXCCMixerView::can_copy_as(t_file_type ft)
{
	// can_delete() populates m_index_selected from the listview selection.
	// The Copy-as... update handlers all sit on the same shared vector but
	// MFC fires OnUpdate* in menu order, so without calling it ourselves
	// the enable state would depend on whether OnUpdatePopupCopy (which
	// calls can_copy -> can_delete) happened to run first.
	if (!can_delete())
		return false;
	if (m_index_selected.empty())
		return false;
	// Destination-in-MIX: copy_as ignores `ft` and silently falls back to
	// raw bytes insert, so format conversion would be a lie. Always block.
	if (m_other_pane->m_mix_f)
		return false;
	// Source-in-MIX: most per-format converters open the inner file via
	// Ccc_file::open(id, mix_f) which is fine. The exception is Copy as
	// Text on a nested .mix — that re-enters Cmix_file::post_open whose
	// strict size checks reject many real archives when read at an inner
	// offset. Block only that case; let the rest run.
	if (m_mix_f && ft == ft_text)
	{
		for (auto& i : m_index_selected)
		{
			if (find_ref(m_index, get_id(i)).ft == ft_mix)
				return false;
		}
	}
	for (auto& i : m_index_selected)
	{
		if (!can_convert(find_ref(m_index, get_id(i)).ft, ft))
			return false;
	}
	return true;
}

static void set_msg(const string& s)
{
	GetMainFrame()->SetMessageText(s.c_str());
}

static void copy_failed(const Cfname& fname, int error)
{
	string v = "Copy " + fname.get_ftitle() + " failed, error " + n(error);
	if (error & 0x400)
	{
		v += ": ";
		v += error_messages[error & 0x3ff];
	}
	set_msg(v);
}

static void copy_succeeded(const Cfname& fname)
{
	set_msg("Copy " + fname.get_ftitle() + " succeeded");
}

int big_insert_dir(Cbig_edit& f, const string& dir, const string& name_prefix)
{
	int error = 0;
	WIN32_FIND_DATA fd;
	HANDLE fh = FindFirstFile((dir + '*').c_str(), &fd);
	if (fh != INVALID_HANDLE_VALUE)
	{
		do
		{
			string fname = dir + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (*fd.cFileName != '.')
					error = error ? big_insert_dir(f, fname + '/', name_prefix + fd.cFileName + '\\'), error : big_insert_dir(f, fname + '/', name_prefix + fd.cFileName + '\\');
			}
			else
			{
				Cvirtual_binary d;
				if (!d.load(fname))
					error = error ? f.insert(name_prefix + fd.cFileName, d) : f.insert(name_prefix + fd.cFileName, d), error;
			}
		} while (FindNextFile(fh, &fd));
		FindClose(fh);
	}
	return error;
}

static int seh_call_dispatch(CXCCMixerView* v, t_file_type ft, int i, Cfname* fname);

void CXCCMixerView::copy_as(t_file_type ft)
{
	// One-pane mode: there's no "other pane" the user can see, so prompt for a
	// destination folder and redirect m_other_pane's state to it for the duration
	// of this call. The whole function reads destination through m_other_pane->
	// get_dir() / ->m_mix_f, so a save+temp-override+restore works without
	// touching the per-format branches below.
	struct other_pane_saver {
		CXCCMixerView* p;
		string saved_dir;
		Cmix_file* saved_mix_f;
		bool armed;
		~other_pane_saver() { if (armed) { p->m_dir = saved_dir; p->m_mix_f = saved_mix_f; } }
	} restore{ m_other_pane, m_other_pane ? m_other_pane->m_dir : string(), m_other_pane ? m_other_pane->m_mix_f : nullptr, false };
	if (m_other_pane && GetMainFrame() && !GetMainFrame()->two_panes())
	{
		CFolderPickerDialog dlg;
		dlg.m_ofn.lpstrTitle = "Choose destination folder";
		if (dlg.DoModal() != IDOK)
			return;
		string dest = static_cast<const char*>(dlg.GetPathName());
		if (dest.empty())
			return;
		if (dest.back() != '\\' && dest.back() != '/')
			dest += '\\';
		restore.armed = true;
		m_other_pane->m_dir = dest;
		m_other_pane->m_mix_f = nullptr;   // force the "write to disk" branch
	}

	CWaitCursor wait;
	int error;
	// Tally insert results so a MIX-destination drop/copy can report a clear
	// summary -- the trailing OnPopupCompact() otherwise leaves "Compact done"
	// as the last status, hiding whether anything was actually inserted.
	int n_ok = 0, n_fail = 0;
	for (auto& i : m_index_selected)
	{
		const Cfname fname = m_other_pane->get_dir() + find_ref(m_index, get_id(i)).name;
		if (find_ref(m_index, get_id(i)).name.find('\\') != string::npos)
			create_deep_dir(m_other_pane->get_dir(), Cfname(find_ref(m_index, get_id(i)).name).get_path());
		if (m_other_pane->m_mix_f)
		{
			const Cfname fname = get_dir() + find_ref(m_index, get_id(i)).name;
			if (find_ref(m_index, get_id(i)).name.find('\\') != string::npos)
				create_deep_dir(get_dir(), Cfname(find_ref(m_index, get_id(i)).name).get_path());
			t_file_type mix_ft = m_other_pane->m_mix_f->get_file_type();
			if (!m_other_pane->edit_release())
			{
				// Lazy nested extract failed on the destination -- skip this copy.
				copy_failed(fname, 1);
				continue;
			}
			switch (mix_ft)
			{
			case ft_big:
				{
					Cbig_edit f;
					error = f.open(m_other_pane->m_mix_fname);
					if (!error)
					{
						DWORD file_attributes = GetFileAttributes(fname.get_all().c_str());
						if (file_attributes == INVALID_FILE_ATTRIBUTES || ~file_attributes & FILE_ATTRIBUTE_DIRECTORY)
						{
							// Source-aware bytes: get_vdata reads from the source MIX
							// when this pane is a MIX, or from disk for a folder source.
							// d.load(fname) only worked for folder sources (a MIX
							// entry has no on-disk path), so MIX->MIX never inserted.
							Cvirtual_binary d = get_vdata(i);
							if (d.data() && d.size() > 0)
								error = f.insert(Cfname(fname).get_fname(), d);
							else
								error = 1;
						}
						else
							error = error ? big_insert_dir(f, string(fname) + '/', Cfname(fname).get_fname() + '\\'), error : big_insert_dir(f, string(fname) + '/', Cfname(fname).get_fname() + '\\');

						error = error ? f.write_index(), error : f.write_index();
						f.close();
					}
					break;
				}
			case ft_mix_rg:
				{
					Cmix_rg_edit f;
					error = f.open(m_other_pane->m_mix_fname);
					if (!error)
					{
						Cvirtual_binary d = get_vdata(i);   // source-aware (MIX or folder)
						if (d.data() && d.size() > 0)
							error = f.insert(Cfname(fname).get_fname(), d);
						else
							error = 1;
						error = error ? f.write_index(), error : f.write_index();
						f.close();
					}
					break;
				}
			default:
				{
					Cmix_edit f;
					error = f.open(m_other_pane->m_mix_fname);
					if (!error)
					{
						Cvirtual_binary d = get_vdata(i);   // source-aware (MIX or folder)
						if (d.data() && d.size() > 0)
							error = f.insert(Cfname(fname).get_fname(), d);
						else
							error = 1;
						error = error ? f.write_index(), error : f.write_index();
						f.close();
					}
				}
			}
			m_other_pane->edit_reopen(!error);
			if (error)
			{
				n_fail++;
				copy_failed(fname, error);
			}
			else
			{
				n_ok++;
				copy_succeeded(fname);
			}
		}
		else
		{
			// SEH-wrap the per-format dispatch. Decoders read from the
			// MIX-mapped buffer directly; a malformed header (the JPEG-from-
			// paletted-SHP bug, an IMA-ADPCM block_align of 0, etc.) can drive
			// an out-of-bounds read past the mapped page and raise an access
			// violation. Catching here turns the AV into a "Copy as ... failed"
			// dialog instead of taking down the app.
			error = seh_call_dispatch(this, ft, i, const_cast<Cfname*>(&fname));
			if (error)
				copy_failed(fname, error);
			else
				copy_succeeded(fname);
		}
	}
	if (m_dir == m_other_pane->m_dir && !m_mix_f)
		update_list();
	m_other_pane->update_list();
	if (m_other_pane->m_mix_f)
	{
		m_other_pane->OnPopupCompact();
		// OnPopupCompact left "Compact done" as the status; replace it with a
		// summary of what was actually inserted so the user knows the files
		// landed in the MIX (not just that a compact ran).
		if (n_ok || n_fail)
		{
			string msg = "Inserted " + n(n_ok) + (n_ok == 1 ? " file" : " files");
			if (n_fail)
				msg += ", " + n(n_fail) + " failed";
			set_msg(msg);
		}
	}
}

int CXCCMixerView::copy(int i, Cfname fname) const
{
	Ccc_file f(false);
	int error = open_f_index(f, i);
	return error ? error : f.extract(fname);
}

static int seh_call_dispatch(CXCCMixerView* v, t_file_type ft, int i, Cfname* fname)
{
	__try
	{
		return v->dispatch_copy_as(ft, i, *fname);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0xC0000005;
	}
}

int CXCCMixerView::dispatch_copy_as(t_file_type ft, int i, const Cfname& fname)
{
	switch (ft)
	{
	case -1:                  return copy(i, fname);
	case ft_aud:              return copy_as_aud(i, fname);
	case ft_avi:              return copy_as_avi(i, fname);
	case ft_cps:              return copy_as_cps(i, fname);
	case ft_csv:              return copy_as_csv(i, fname);
	case ft_html:             return copy_as_html(i, fname);
	case ft_hva:              return copy_as_hva(i, fname);
	case ft_jpeg:
	case ft_jpeg_single:
	case ft_pcx:
	case ft_pcx_single:
	case ft_png:
	case ft_png_single:
	case ft_tga:
	case ft_tga_single:       return copy_as_pcx(i, fname, ft);
	case ft_map_ts_preview:   return copy_as_map_ts_preview(i, fname);
	case ft_pal:              return copy_as_pal(i, fname);
	case ft_pal_jasc:         return copy_as_pal_jasc(i, fname);
	case ft_shp:              return copy_as_shp(i, fname);
	case ft_shp_ts:           return copy_as_shp_ts(i, fname);
	case ft_text:             return copy_as_text(i, fname);
	case ft_vxl:              return copy_as_vxl(i, fname);
	case ft_wav_ima_adpcm:    return copy_as_wav_ima_adpcm(i, fname);
	case ft_wav_pcm:          return copy_as_wav_pcm(i, fname);
	case ft_xif:              return copy_as_xif(i, fname);
	}
	return 1;
}

int CXCCMixerView::copy_as_aud(int i, Cfname fname) const
{
	Cwav_file f;
	int error = vload_f_index(f, i);
	if (error)
		return error;
	fname.set_ext(".aud");
	if (error = f.process())
		return error;
	const t_riff_wave_format_chunk& format_chunk = f.get_format_chunk();
	int samplerate = format_chunk.samplerate;
	int c_channels = format_chunk.c_channels;
	if (c_channels != 1 && c_channels != 2)
		return 0x100;
	// AUD encoder consumes PCM s16. Two source paths feed it:
	//   - tag 1 (PCM) at 16-bit: read raw, no decode.
	//   - tag 0x11 (IMA-ADPCM) at 4-bit: decode to PCM s16 first.
	// Anything else is rejected.
	Cvirtual_binary data;
	int cb_data;
	if (format_chunk.tag == 0x0001 && format_chunk.cbits_sample == 16)
	{
		cb_data = f.get_data_header().size;
		f.seek(f.get_data_ofs());
		if (error = f.read(data.write_start(cb_data), cb_data))
			return error;
	}
	else if (format_chunk.tag == 0x0011 && format_chunk.cbits_sample == 4)
	{
		int block = format_chunk.block_align;
		if (block <= 0)
			block = 512 * c_channels;
		Cima_adpcm_wav_decode decode;
		decode.load(f.get_data() + f.get_data_ofs(), f.get_data_size(), c_channels, block);
		cb_data = decode.cb_data();
		if (cb_data <= 0)
			return 0x100;
		memcpy(data.write_start(cb_data), decode.data(), cb_data);
	}
	else
		return 0x100;
	int c_samples = cb_data >> 1;
	if (format_chunk.c_channels == 2)
	{
		audio_combine_channels(reinterpret_cast<__int16*>(data.data_edit()), c_samples);
		c_samples >>= 1;
	}
	if (samplerate == 44100)
	{
		audio_combine_channels(reinterpret_cast<__int16*>(data.data_edit()), c_samples);
		c_samples >>= 1;
		samplerate >>= 1;
	}
	Caud_file_write g;
	if (error = g.open_write(fname))
		return error;
	g.set_c_samples(c_samples);
	g.set_samplerate(samplerate);
	if (error = g.write_header())
		return error;
	aud_decode aud_d;
	aud_d.init();
	const short* r = reinterpret_cast<const short*>(data.data());
	byte chunk[512];
	while (c_samples)
	{
		int cs_chunk = min(c_samples, 1024);
		aud_d.encode_chunk(r, chunk, cs_chunk);
		r += cs_chunk;
		if (error = g.write_chunk(chunk, cs_chunk))
			return error;
		c_samples -= cs_chunk;
	}
	return error;
}

int CXCCMixerView::copy_as_avi(int i, Cfname fname) const
{
	int error = 0;
	fname.set_ext(".avi");
	Cvqa_file f;
	error = open_f_index(f, i);
	if (!error)
		error = f.extract_as_avi(fname, AfxGetMainWnd()->GetSafeHwnd());
	return error;
}

int CXCCMixerView::copy_as_cps(int i, Cfname fname) const
{
	fname.set_ext(".cps");
	Cvirtual_image image = get_vimage(i);
	if (image.cx() != 320 || image.cy() != 200)
		return 257;
	t_palette palette;
	if (image.palette())
	{
		memcpy(palette, image.palette(), sizeof(t_palette));
		convert_palette_24_to_18(palette);
	}
	else
		memcpy(palette, get_default_palette(), sizeof(t_palette));
	if (image.cb_pixel() != 1)
		image.decrease_color_depth(1, palette);
	return cps_file_write(image.image(), palette).save(fname);
}

int CXCCMixerView::copy_as_csv(int i, Cfname fname) const
{
	int error = 0;
	fname.set_ext(".csv");
	Chva_file f;
	error = open_f_index(f, i);
	if (!error)
		error = f.extract_as_csv(fname);
	return error;
}

int CXCCMixerView::copy_as_html(int i, Cfname fname) const
{
	fname.set_ext(".html");
	Ccc_file f(true);
	int error = open_f_index(f, i);
	if (error)
		return error;
	switch (f.get_file_type(false))
	{
	case ft_map_ts:
		{
			Cmap_ts_ini_reader ir;
			ir.fast(true);
			ir.process(f.vdata());
			stringstream ini;
			Cmap_ts_encoder encoder(ini, true);
			Cmap_ts_encoder::t_header header;
			header.cx = ir.get_map_data().size_right;
			header.cy = ir.get_map_data().size_bottom;
			encoder.header(header);
			encoder.process(f.vdata());
			ofstream os(fname);
			ir.write_report(os, fname, encoder);
		}
		break;
	case ft_pkt_ts:
		{
			Cpkt_ts_ini_reader ir;
			ir.process(f.vdata());
			ofstream os(fname);
			ir.write_report(os);
		}
		break;
	}
	return error;
}

int CXCCMixerView::copy_as_hva(int i, Cfname fname) const
{
	fname.set_ext(".hva");
	Ctext_file f;
	int error = open_f_index(f, i);
	return error ? error : hva_file_write(f.get_data(), f.get_size()).save(fname);
}

int CXCCMixerView::copy_as_map_ts_preview(int i, Cfname fname) const
{
	fname.set_ext(".txt");
	Cvirtual_image image = get_vimage(i);
	image.cb_pixel(3);
	Cvirtual_binary d;
	d.set_size(encode5(image.image(), d.write_start(image.cx() * image.cy() * 6), image.cb_image(), 5));
	Cvirtual_binary e = encode64(d);
	ofstream g(fname);
	g << "[Preview]" << endl
		<< "Size=0,0," << image.cx() << ',' << image.cy() << endl
		<< endl
		<< "[PreviewPack]" << endl;
	const byte* r = e.data();
	int line_i = 1;
	while (r < e.data_end())
	{
		char line[80];
		int cb_line = min(e.data_end() - r, 70);
		memcpy(line, r, cb_line);
		line[cb_line] = 0;
		r += cb_line;
		g << line_i++ << '=' << line << endl;
	}
	return g.fail();
}

int CXCCMixerView::copy_as_pal(int i, Cfname fname) const
{
	Cvirtual_image image = get_vimage(i);
	if (!image.palette())
		return 1;
	t_palette palette;
	memcpy(palette, image.palette(), sizeof(t_palette));
	convert_palette_24_to_18(palette);
	fname.set_ext(".pal");
	return file32_write(fname, palette, sizeof(t_palette));
}

int CXCCMixerView::copy_as_pal_jasc(int i, Cfname fname) const
{
	Cpal_file f;
	bool shift_left = false;
	Cvirtual_image image = get_vimage(i);
	if (image.palette())
		f.load(Cvirtual_binary(image.palette(), sizeof(t_palette)));
	else if (open_f_index(f, i))
		return 1;
	else
		shift_left = true;
	fname.set_ext(".pal");
	ofstream os(fname);
	return f.extract_as_pal_jasc(os, shift_left).fail();
}

static int copy_as_image(Cvideo_decoder* v, string fname, t_file_type ft)
{
	t_palette palette;
	memcpy(palette, v->palette(), sizeof(t_palette));
	convert_palette_18_to_24(palette);
	Cvirtual_binary frame;
	Cfname t = fname;
	for (int i = 0; i < v->cf(); i++)
	{
		v->decode(frame.write_start(v->cb_image()));
		t.set_title(Cfname(fname).get_ftitle() + " " + nwzl(4, i));
		if (int error = image_file_write(t, ft, frame.data(), palette, v->cx(), v->cy(), v->cb_pixel()))
			return error;
	}
	delete v;
	return 0;
}

int CXCCMixerView::copy_as_pcx(int i, Cfname fname, t_file_type ft) const
{
	switch (ft)
	{
	case ft_jpeg:
	case ft_jpeg_single:
		fname.set_ext(".jpeg");
		break;
	case ft_pcx:
	case ft_pcx_single:
		fname.set_ext(".pcx");
		break;
	case ft_tga:
	case ft_tga_single:
		fname.set_ext(".tga");
		break;
	default:
		fname.set_ext(".png");
	}
	switch (find_ref(m_index, get_id(i)).ft)
	{
	case ft_cps:
	case ft_dds:
	case ft_jpeg:
	case ft_pcx:
	case ft_png:
	case ft_tga:
	case ft_tmp_ra:
	case ft_tmp_ts:
		return get_vimage(i).save(fname, ft);
	case ft_shp_dune2:
		{
			Cshp_dune2_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_pcx(fname, ft, get_default_palette());
		}
	case ft_shp:
		{
			Cshp_file f;
			int error = vload_f_index(f, i);
			return error ? error : copy_as_image(f.decoder(get_default_palette()), fname, ft);
		}
	case ft_shp_ts:
		{
			switch (ft)
			{
			case ft_jpeg_single:
				return get_vimage(i).save(fname, ft_jpeg);
			case ft_pcx_single:
				return get_vimage(i).save(fname, ft_pcx);
			case ft_png_single:
				return get_vimage(i).save(fname, ft_png);
			case ft_tga_single:
				return get_vimage(i).save(fname, ft_tga);
			}
			Cshp_ts_file f;
			f.load(get_vdata(i));
			return f.extract_as_pcx(fname, ft, get_default_palette(), GetMainFrame()->combine_shadows(), static_cast<Cshp_ts_file::shadow_style>(GetMainFrame()->shadow_style()));
		}
	case ft_vqa:
		{
			Cvqa_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_pcx(fname, ft);
		}
	case ft_vxl:
		{
			Cvxl_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_pcx(fname, ft, get_default_palette());
		}
	case ft_wsa_dune2:
		{
			Cwsa_dune2_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_pcx(fname, ft, get_default_palette());
		}
	case ft_wsa:
		{
			Cwsa_file f;
			int error = vload_f_index(f, i);
			return error ? error : copy_as_image(f.decoder(), fname, ft);
		}
	}
	return 0;
}

static string get_base_name(const string& fname)
{
	string t = Cfname(fname).get_ftitle();
	int p = -1;
	for (int i = 0; i < t.length(); i++)
	{
		if (!isdigit(t[i]))
			p = i;
	}
	return t.substr(0, p + 1);
}

static int get_index_from_name(const string& base_name, const string& fname)
{
	string t = Cfname(fname).get_ftitle();
	return t.substr(0, base_name.length()) != base_name
		|| base_name.find_first_not_of("0123456789", base_name.length()) != string::npos
		? -1
		: atoi(t.substr(base_name.length()).c_str());
}

static void create_rp(const t_palette s1, const t_palette s2, byte* d, t_game game)
{
	d[0] = 0;
	for (int i = 1; i < 256; i++)
	{
		switch (game)
		{
		case game_td:
			if (i >= 0xb0 && i < 0xc0)
				d[i] = i - 0xa0;
			else
				d[i] = find_color(s1[i].r, s1[i].g, s1[i].b, s2);
			break;
		case game_ra:
			if (i >= 0x50 && i < 0x60)
				d[i] = i - 0x40;
			else
				d[i] = find_color(s1[i].r, s1[i].g, s1[i].b, s2);
			break;
		default:
			d[i] = find_color(s1[i].r, s1[i].g, s1[i].b, s2);
		}
	}
}

int CXCCMixerView::copy_as_shp(int _i, Cfname fname) const
{
	fname.set_ext(".shp");
	Cvirtual_binary s;
	t_palette s_palette;
	string base_name = get_base_name(fname);
	if (get_index_from_name(base_name, fname))
		return em_bad_fname;
	Cvirtual_image image = get_vimage(_i);
	if (image.palette())
		memcpy(s_palette, image.palette(), sizeof(t_palette));
	else
	{
		memcpy(s_palette, get_default_palette(), sizeof(t_palette));
		convert_palette_18_to_24(s_palette);
	}
	int cx = image.cx();
	int cy = image.cy();
	int c_images = 0;
	int index[1000];
	int i;
	for (i = 0; i < 1000; i++)
		index[i] = -1;
	for (auto& j : m_index)
	{
		int z = get_index_from_name(base_name, j.second.name);
		if (z != -1 && z < 1000)
			index[z] = j.first;
	}
	while (i--)
	{
		int id = index[i];
		if (id == -1)
			continue;
		image = get_vimage_id(id);
		if (image.cx() != cx || image.cy() != cy)
			return em_bad_size;
		image.cb_pixel(1, s_palette);
		if (!s.data())
			c_images = i + 1;
		memcpy(s.write_start(cx * cy * c_images) + cx * cy * i, image.image(), cx * cy);
	}
	if (!s.data())
		return 1;
	if (GetMainFrame()->use_palette_for_conversion())
	{
		byte rp[256];
		t_palette p;
		memcpy(p, get_default_palette(), sizeof(t_palette));
		convert_palette_18_to_24(p);
		create_rp(s_palette, p, rp);
		apply_rp(s.data_edit(), cx * cy * c_images, rp);
	}
	trim(base_name);
	fname.set_title(base_name);
	return shp_file_write(s.data(), cx, cy, c_images).save(fname);
}

int CXCCMixerView::copy_as_shp_ts(int i, Cfname fname) const
{
	const bool convert_from_td = GetMainFrame()->convert_from_td();
	const bool convert_from_ra = GetMainFrame()->convert_from_ra();
	const bool convert_shadow = GetMainFrame()->split_shadows();
	int cx;
	int cy;
	int c_images;
	fname.set_ext(".shp");
	Cvirtual_binary s;
	t_palette s_palette;
	string base_name = fname.get_ftitle();
	switch (find_ref(m_index, get_id(i)).ft)
	{
	case ft_shp:
		{
			Cshp_file f;
			int error = vload_f_index(f, i);
			if (error)
				return error;
			memcpy(s_palette, GetMainFrame()->get_game_palette(convert_from_td ? game_td : game_ra), sizeof(t_palette));
			convert_palette_18_to_24(s_palette);
			cx = f.cx();
			cy = f.cy();
			c_images = f.cf() << 1;
			shp_images::t_image_data* p;
			byte* w = s.write_start(cx * cy * c_images);
			if (cx && cy && !shp_images::load_shp(f, p))
			{
				for (int i = 0; i < c_images >> 1; i++)
				{
					memcpy(w, p->get(i), cx * cy);
					w += cx * cy;
				}
				shp_images::destroy_shp(p);
			}
			break;
		}
	default:
		base_name = get_base_name(fname);
		if (get_index_from_name(base_name, fname))
			return em_bad_fname;
		Cvirtual_image image = get_vimage(i);
		if (image.palette())
			memcpy(s_palette, image.palette(), sizeof(t_palette));
		else
		{
			memcpy(s_palette, get_default_palette(), sizeof(t_palette));
			convert_palette_18_to_24(s_palette);
		}
		cx = image.cx();
		cy = image.cy();
		c_images = 0;
		int index[10000];
		int i;
		for (i = 0; i < 10000; i++)
			index[i] = -1;
		for (auto& j : m_index)
		{
			int z = get_index_from_name(base_name, j.second.name);
			if (z != -1 && z < 10000)
				index[z] = j.first;
		}
		while (i--)
		{
			int id = index[i];
			if (id == -1)
				continue;
			image = get_vimage_id(id);
			if (image.cx() != cx || image.cy() != cy)
				return em_bad_size;
			image.cb_pixel(1, s_palette);
			if (!s.data())
			{
				c_images = i + 1;
				if (convert_shadow)
					c_images <<= 1;
			}
			memcpy(s.write_start(cx * cy * c_images) + cx * cy * i, image.image(), cx * cy);
		}
		break;
	}
	if (!s.data())
		return 1;
	if (convert_shadow)
	{
		int count = cx * cy * c_images >> 1;
		byte* r = s.data_edit();
		byte* w = s.data_edit() + count;
		while (count--)
		{
			byte& v = *r++;
			if (v == 4)
			{
				v = 0;
				*w++ = 1;
			}
			else
				*w++ = 0;
		}
	}
	if (GetMainFrame()->use_palette_for_conversion())
	{
		byte rp[256];
		t_palette p;
		memcpy(p, get_default_palette(), sizeof(t_palette));
		convert_palette_18_to_24(p);
		if (convert_from_td)
			create_rp(s_palette, p, rp, game_td);
		else if (convert_from_ra)
			create_rp(s_palette, p, rp, game_ra);
		else
			create_rp(s_palette, p, rp);
		apply_rp(s.data_edit(), cx * cy * c_images >> (int)convert_shadow, rp);
	}
	if (GetMainFrame()->fix_shadows() && ~c_images & 1)
	{
		int count = cx * cy * c_images >> 1;
		for (byte* w = s.data_edit() + count; count--; w++)
		{
			if (*w)
				*w = 1;
		}
	}
	trim(base_name);
	fname.set_title(base_name);
	return shp_ts_file_write(s.data(), cx, cy, c_images, GetMainFrame()->enable_compression()).save(fname);
}

int CXCCMixerView::copy_as_text(int i, Cfname fname) const
{
	fname.set_ext(".txt");
	switch (find_ref(m_index, get_id(i)).ft)
	{
	case ft_st:
		{
			Cst_file f;
			if (int error = open_f_index(f, i))
				return error;
			ofstream os(fname);
			return f.extract_as_text(os).fail();
		}
	case ft_vxl:
		{
			Cvxl_file f;
			if (int error = open_f_index(f, i))
				return error;
			ofstream os(fname);
			return f.extract_as_text(os).fail();
		}
	case ft_xif:
		{
			Cxif_file f;
			if (int error = open_f_index(f, i))
				return error;
			Cxif_key key;
			if (int error = f.decode(key))
				return error;
			ofstream os(fname);
			key.dump(os, true);
			return 0;
		}
	case ft_pal:
		{
			Cpal_file f;
			if (int error = open_f_index(f, i))
				return error;
			ofstream os(fname);
			return f.extract_as_text(os).fail();
		}
	case ft_mix:
		{
			Cmix_file f;
			if (int error = open_f_index(f, i))
				return error;
			ofstream os(fname);
			return f.extract_as_text(os).fail();
		}
	}
	return 0;
}

int CXCCMixerView::copy_as_vxl(int i, Cfname fname) const
{
	int error = 0;
	fname.set_ext(".vxl");
	switch (find_ref(m_index, get_id(i)).ft)
	{
	case ft_text:
		{
			Cvirtual_tfile f;
			f.load_data(get_vdata(i));
			Cvirtual_binary d = vxl_file_write(f);
			return d.size() ? d.save(fname) : 1;
		}
	case ft_xif:
		{
			Cxif_key k;
			int error = k.load_key(get_vdata(i));
			if (error)
				return error;
			Cvirtual_binary d = vxl_file_write(k);
			return d.size() ? d.save(fname) : 1;
		}
	default:
		{
			string base_name = get_base_name(fname);
			if (get_index_from_name(base_name, fname))
				return em_bad_fname;
			Cvirtual_image image = get_vimage(i);
			int cx = image.cx();
			int cy = image.cy();
			Cvirtual_binary s;
			int index[256];
			int i;
			for (i = 0; i < 256; i++)
				index[i] = -1;
			for (auto& j : m_index)
			{
				int z = get_index_from_name(base_name, j.second.name);
				if (z != -1 && z < 256)
					index[z] = j.first;
			}
			int c_images = 0;
			while (i--)
			{
				int id = index[i];
				if (id == -1)
					continue;
				image = get_vimage_id(id);
				if (image.cx() != cx || image.cy() != cy)
					return em_bad_size;
				if (image.cb_pixel() != 1)
					return em_bad_depth;
				if (!s.data())
					c_images = i + 1;
				memcpy(s.write_start(cx * cy * c_images) + cx * cy * i, image.image(), cx * cy);
			}
			Cvirtual_binary d = vxl_file_write(s.data(), NULL, cx, cy, c_images);
			trim(base_name);
			fname.set_title(base_name);
			return d.size() ? d.save(fname) : 1;
		}
	}
	return error;
}

int CXCCMixerView::copy_as_wav_ima_adpcm(int i, Cfname fname) const
{
	fname.set_ext(".wav");
	switch (find_ref(m_index, get_id(i)).ft)
	{
	case ft_wav:
		{
			Cwav_file f;
			int error = vload_f_index(f, i);
			if (error)
				return error;
			if (error = f.process())
				return error;
			const t_riff_wave_format_chunk& format_chunk = f.get_format_chunk();
			int c_channels = format_chunk.c_channels;
			if (c_channels != 1 && c_channels != 2)
				return 0x100;
			// PCM s16 source: encode to IMA-ADPCM.
			// IMA-ADPCM source: already in target format, re-emit a
			// self-contained .wav with our header (the source may have
			// been embedded in a MIX with stripped/odd RIFF wrapping).
			if (format_chunk.tag == 1 && format_chunk.cbits_sample == 16)
			{
				int cb_s = f.get_data_header().size;
				Cvirtual_binary s;
				f.seek(f.get_data_ofs());
				if (error = f.read(s.write_start(cb_s), cb_s))
					return error;
				Cima_adpcm_wav_encode encode;
				encode.load(reinterpret_cast<short*>(s.data_edit()), cb_s, c_channels);
				int c_samples = encode.c_samples();
				int cb_audio = encode.cb_data();
				int cb_d = sizeof(t_wav_ima_adpcm_header) + cb_audio;
				Cvirtual_binary d;
				byte* w = d.write_start(cb_d);
				w += wav_ima_adpcm_file_write_header(w, cb_audio, c_samples, format_chunk.samplerate, c_channels);
				memcpy(w, encode.data(), cb_audio);
				return d.save(fname);
			}
			else if (format_chunk.tag == 0x11 && format_chunk.cbits_sample == 4)
			{
				int cb_audio = f.get_data_header().size;
				int block = format_chunk.block_align;
				if (block <= 0)
					block = 512 * c_channels;
				// Decode once just to recover an accurate sample count for
				// the WAV/fact header; the source data itself is copied
				// through unchanged.
				Cima_adpcm_wav_decode decode;
				decode.load(f.get_data() + f.get_data_ofs(), f.get_data_size(), c_channels, block);
				int c_samples = decode.c_samples();
				int cb_d = sizeof(t_wav_ima_adpcm_header) + cb_audio;
				Cvirtual_binary d;
				byte* w = d.write_start(cb_d);
				w += wav_ima_adpcm_file_write_header(w, cb_audio, c_samples, format_chunk.samplerate, c_channels);
				memcpy(w, f.get_data() + f.get_data_ofs(), cb_audio);
				return d.save(fname);
			}
			else
				return 0x100;
		}
	}
	return 0;
}

int CXCCMixerView::copy_as_wav_pcm(int i, Cfname fname) const
{
	fname.set_ext(".wav");
	switch (find_ref(m_index, get_id(i)).ft)
	{
	case ft_aud:
		{
			Caud_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_wav(fname);
		}
	case ft_voc:
		{
			Cvoc_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_wav(fname);
		}
	case ft_vqa:
		{
			Cvqa_file f;
			int error = vload_f_index(f, i);
			return error ? error : f.extract_as_wav(fname);
		}
	case ft_wav:
		{
			Cwav_file f;
			int error = vload_f_index(f, i);
			if (error)
				return error;
			if (error = f.process())
				return error;
			const t_riff_wave_format_chunk& format_chunk = f.get_format_chunk();
			if (format_chunk.tag != 0x11
				|| format_chunk.c_channels != 1 && format_chunk.c_channels != 2
				|| format_chunk.cbits_sample != 4)
				return 0x100;
			int cb_s = f.get_data_header().size;
			Cvirtual_binary s;
			f.seek(f.get_data_ofs());
			if (error = f.read(s.write_start(cb_s), cb_s))
				return error;
			int c_channels = format_chunk.c_channels;
			// Decode first and trust the decoder's reported sample/byte counts.
			// The fact chunk's c_samples is unreliable (may be missing, zero, or
			// a bogus large value, e.g. 29M for a 7 MB source), and using it to
			// size the destination buffer caused memcpy to read past decode's
			// own output buffer and crash.
			Cima_adpcm_wav_decode decode;
			decode.load(s.data(), cb_s, c_channels, 512 * c_channels);
			int c_samples = decode.c_samples();
			int cb_audio = decode.cb_data();
			int cb_d = sizeof(t_wav_header) + cb_audio;
			Cvirtual_binary d;
			byte* w = d.write_start(cb_d);
			w += wav_file_write_header(w, c_samples, format_chunk.samplerate, 2, c_channels);
			memcpy(w, decode.data(), cb_audio);
			return d.save(fname);
		}
	}
	return 0;
}

int CXCCMixerView::copy_as_xif(int i, Cfname fname) const
{
	fname.set_ext(".xif");
	Cvxl_file f;
	int error = open_f_index(f, i);
	return error ? error : f.extract_as_xif(fname);
}

void CXCCMixerView::OnPopupCopy()
{
	copy_as(static_cast<t_file_type>(-1));
}

void CXCCMixerView::OnUpdatePopupCopy(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy());
}

void CXCCMixerView::OnPopupCopyAsAUD()
{
	copy_as(ft_aud);
}

void CXCCMixerView::OnUpdatePopupCopyAsAUD(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_aud));
}

void CXCCMixerView::OnPopupCopyAsAVI()
{
	copy_as(ft_avi);
}

void CXCCMixerView::OnUpdatePopupCopyAsAVI(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_avi));
}

void CXCCMixerView::OnPopupCopyAsCPS()
{
	copy_as(ft_cps);
}

void CXCCMixerView::OnUpdatePopupCopyAsCPS(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_cps));
}

void CXCCMixerView::OnPopupCopyAsCSV()
{
	copy_as(ft_csv);
}

void CXCCMixerView::OnUpdatePopupCopyAsCSV(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_csv));
}

void CXCCMixerView::OnPopupCopyAsHTML()
{
	copy_as(ft_html);
}

void CXCCMixerView::OnUpdatePopupCopyAsHTML(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_html));
}

void CXCCMixerView::OnPopupCopyAsHVA()
{
	copy_as(ft_hva);
}

void CXCCMixerView::OnUpdatePopupCopyAsHVA(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_hva));
}

void CXCCMixerView::OnPopupCopyAsJpegSingle()
{
	copy_as(ft_jpeg_single);
}

void CXCCMixerView::OnUpdatePopupCopyAsJpegSingle(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_jpeg_single));
}

void CXCCMixerView::OnPopupCopyAsJpeg()
{
	copy_as(ft_jpeg);
}

void CXCCMixerView::OnUpdatePopupCopyAsJpeg(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_jpeg));
}

void CXCCMixerView::OnPopupCopyAsMapTsPreview()
{
	copy_as(ft_map_ts_preview);
}

void CXCCMixerView::OnUpdatePopupCopyAsMapTsPreview(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_map_ts_preview));
}

void CXCCMixerView::OnPopupCopyAsPAL()
{
	copy_as(ft_pal);
}

void CXCCMixerView::OnUpdatePopupCopyAsPAL(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_pal));
}

void CXCCMixerView::OnPopupCopyAsPAL_JASC()
{
	copy_as(ft_pal_jasc);
}

void CXCCMixerView::OnUpdatePopupCopyAsPAL_JASC(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_pal_jasc));
}

void CXCCMixerView::OnPopupCopyAsPCX()
{
	copy_as(ft_pcx);
}

void CXCCMixerView::OnUpdatePopupCopyAsPCX(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_pcx));
}

void CXCCMixerView::OnPopupCopyAsPcxSingle()
{
	copy_as(ft_pcx_single);
}

void CXCCMixerView::OnUpdatePopupCopyAsPcxSingle(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_pcx_single));
}

void CXCCMixerView::OnPopupCopyAsPngSingle()
{
	copy_as(ft_png_single);
}

void CXCCMixerView::OnUpdatePopupCopyAsPngSingle(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_png_single));
}

void CXCCMixerView::OnPopupCopyAsPNG()
{
	copy_as(ft_png);
}

void CXCCMixerView::OnUpdatePopupCopyAsPNG(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_png));
}

void CXCCMixerView::OnPopupCopyAsSHP()
{
	copy_as(ft_shp);
}

void CXCCMixerView::OnUpdatePopupCopyAsSHP(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_shp));
}

void CXCCMixerView::OnPopupCopyAsSHP_TS()
{
	copy_as(ft_shp_ts);
}

void CXCCMixerView::OnUpdatePopupCopyAsSHP_TS(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_shp_ts));
}


void CXCCMixerView::OnPopupCopyAsTgaSingle()
{
	copy_as(ft_tga_single);
}

void CXCCMixerView::OnUpdatePopupCopyAsTgaSingle(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_tga_single));
}

void CXCCMixerView::OnPopupCopyAsTga()
{
	copy_as(ft_tga);
}

void CXCCMixerView::OnUpdatePopupCopyAsTga(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_tga));
}

void CXCCMixerView::OnPopupCopyAsText()
{
	copy_as(ft_text);
}

void CXCCMixerView::OnUpdatePopupCopyAsText(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_text));
}

void CXCCMixerView::OnPopupCopyAsWavImaAdpcm()
{
	copy_as(ft_wav_ima_adpcm);
}

void CXCCMixerView::OnUpdatePopupCopyAsWavImaAdpcm(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_wav_ima_adpcm));
}

void CXCCMixerView::OnPopupCopyAsWavPcm()
{
	copy_as(ft_wav_pcm);
}

void CXCCMixerView::OnUpdatePopupCopyAsWavPcm(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_wav_pcm));
}

void CXCCMixerView::OnPopupCopyAsWSA()
{
	copy_as(ft_wsa);
}

void CXCCMixerView::OnUpdatePopupCopyAsWSA(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_wsa));
}

void CXCCMixerView::OnPopupCopyAsVXL()
{
	copy_as(ft_vxl);
}

void CXCCMixerView::OnUpdatePopupCopyAsVXL(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_vxl));
}

void CXCCMixerView::OnPopupCopyAsXIF()
{
	copy_as(ft_xif);
}

void CXCCMixerView::OnUpdatePopupCopyAsXIF(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_copy_as(ft_xif));
}

void CXCCMixerView::OnDropFiles(HDROP hDropInfo)
{
	CWaitCursor wait;
	int error = 0;
	int c_files = DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0);
	char fname[MAX_PATH];
	t_file_type drop_ft = m_mix_f ? m_mix_f->get_file_type() : ft_unknown;
	bool drop_into_mix = m_mix_f != nullptr;   // before edit_release nulls it
	if (drop_into_mix && edit_release())
	{
		t_file_type ft = drop_ft;
		switch (ft)
		{
		case ft_big:
			{
				Cbig_edit f;
				error = f.open(m_mix_fname);
				if (!error)
				{
					for (int i = 0; i < c_files; i++)
					{
						DragQueryFile(hDropInfo, i, fname, MAX_PATH);
						DWORD file_attributes = GetFileAttributes(fname);
						if (file_attributes == INVALID_FILE_ATTRIBUTES || ~file_attributes & FILE_ATTRIBUTE_DIRECTORY)
						{
							Cvirtual_binary d;
							if (!d.load(fname))
								error = error ? f.insert(Cfname(fname).get_fname(), d) : f.insert(Cfname(fname).get_fname(), d), error;
						}
						else
							error = error ? big_insert_dir(f, string(fname) + '/', Cfname(fname).get_fname() + '\\'), error : big_insert_dir(f, string(fname) + '/', Cfname(fname).get_fname() + '\\');
					}
					error = error ? f.write_index(), error : f.write_index();
					f.close();
				}
				break;
			}
		case ft_mix_rg:
			{
				Cmix_rg_edit f;
				error = f.open(m_mix_fname);
				if (!error)
				{
					for (int i = 0; i < c_files; i++)
					{
						DragQueryFile(hDropInfo, i, fname, MAX_PATH);
						Cvirtual_binary d;
						if (!d.load(fname))
							error = error ? f.insert(Cfname(fname).get_fname(), d) : f.insert(Cfname(fname).get_fname(), d), error;
					}
					error = error ? f.write_index(), error : f.write_index();
					f.close();
				}
				break;
			}
		default:
			Cmix_edit f;
			error = f.open(m_mix_fname);
			if (!error)
			{
				for (int i = 0; i < c_files; i++)
				{
					DragQueryFile(hDropInfo, i, fname, MAX_PATH);
					Cvirtual_binary d;
					if (!d.load(fname))
						error = error ? f.insert(Cfname(fname).get_fname(), d) : f.insert(Cfname(fname).get_fname(), d), error;
				}
				error = error ? f.write_index(), error : f.write_index();
				f.close();
			}
		}
		edit_reopen(!error);
		if (!editing_nested())
			OnPopupCompact();
	}
	else if (!drop_into_mix)
	{
		for (int i = 0; i < c_files; i++)
		{
			DragQueryFile(hDropInfo, i, fname, MAX_PATH);
			error = copy_file(fname, get_dir() + Cfname(fname).get_fname());
		}
		update_list();
	}
	else
	{
		// Dropping into a MIX but the lazy nested extract failed -- nothing
		// written; report it rather than silently copying to the MIX's folder.
		error = 1;
	}
	set_msg(error ? "Insert failed" : "Insert done");
	DragFinish(hDropInfo);
}

void CXCCMixerView::OnPopupCompact()
{
	if (!m_mix_f)
		return;
	CWaitCursor wait;
	int error = 0;
	t_file_type ft = m_mix_f->get_file_type();
	if (!edit_release())   // lazy nested extract failed -> nothing to compact
		return;
	switch (ft)
	{
	case ft_big:
		{
			Cbig_edit f;
			error = f.open(m_mix_fname);
			if (!error)
			{
				error = f.compact();
				f.close();
			}
			break;
		}
	case ft_mix_rg:
		{
			Cmix_rg_edit f;
			error = f.open(m_mix_fname);
			if (!error)
			{
				error = f.compact();
				f.close();
			}
			break;
		}
	default:
		Cmix_edit f;
		error = f.open(m_mix_fname);
		if (!error)
		{
			error = f.compact();
			f.close();
		}
	}
	set_msg(error ? "Compact failed" : "Compact done");
	edit_reopen(!error);
}

void CXCCMixerView::OnUpdatePopupCompact(CCmdUI* pCmdUI)
{
	// Disk-root MIX, or any nested MIX (the temp is extracted on demand when
	// Compact runs, so don't require it to already exist).
	pCmdUI->Enable(m_location.size() == 1 || editing_nested());
}

bool DeleteDirectory(string strPath, const int uiEndLevel = -1, int iCurrentLevel = 0)
{
	if (iCurrentLevel > uiEndLevel)
	{
		return true;
	}
	WIN32_FIND_DATA findFileData;
	auto strFindPath = strPath + "\\*";

	HANDLE hFind = FindFirstFile(strFindPath.c_str(), &findFileData);
	if (INVALID_HANDLE_VALUE == hFind) {
		return false;
	}

	bool result = true;
	do
	{
		string strFileName = findFileData.cFileName;
		if (strFileName != "." && strFileName != "..")
		{
			strFindPath = strPath + "\\" + strFileName;
			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (!DeleteDirectory(strFindPath, uiEndLevel, iCurrentLevel + 1) || !RemoveDirectory(strFindPath.c_str()))
				{
					result = false;
					break;
				}
			}
			else
			{
				if (!DeleteFile(strFindPath.c_str()))
				{
					result = false;
					break;
				}
			}
		}
	} while (FindNextFile(hFind, &findFileData));
	FindClose(hFind);
	if (result && !RemoveDirectory(strPath.c_str()))
	{
		result = false;
	}
	return result;
}

void CXCCMixerView::OnPopupDelete()
{
	// Re-populate m_index_selected from the live selection. Menus fire the
	// OnUpdate* handler first (which calls can_delete), but the Del-key
	// accelerator does not, so do it here to stay correct on both paths and
	// to bail when the selection holds only undeletable rows (drives, "..").
	if (!can_edit() || !can_delete() || m_index_selected.empty())
		return;
	// Confirm unless Silent Delete is on (Configure menu) or Shift is held.
	// The prompt names the actual targets so the user sees what's being
	// removed (capped so a huge multi-select can't overflow the message box).
	if (!theme::silent_delete() && ~GetAsyncKeyState(VK_SHIFT) < 0)
	{
		const size_t n = m_index_selected.size();
		string prompt = n == 1 ? "Are you sure you want to delete this item?\n\n"
		                       : "Are you sure you want to delete these " + std::to_string(n) + " items?\n\n";
		const size_t max_shown = 15;
		size_t shown = 0;
		for (auto& i : m_index_selected)
		{
			if (shown == max_shown)
			{
				prompt += "... and " + std::to_string(n - shown) + " more";
				break;
			}
			const t_index_entry& index = find_ref(m_index, get_id(i));
			prompt += (index.ft == ft_dir ? "[" + index.name + "]" : index.name) + "\n";
			shown++;
		}
		if (MessageBox(prompt.c_str(), "Delete", MB_ICONQUESTION | MB_YESNO) != IDYES)
			return;
	}
	CWaitCursor wait;
	int error = 0;
	t_file_type del_ft = m_mix_f ? m_mix_f->get_file_type() : ft_unknown;
	bool del_into_mix = m_mix_f != nullptr;   // before edit_release nulls it
	if (del_into_mix && edit_release())
	{
		t_file_type ft = del_ft;
		switch (ft)
		{
		case ft_big:
		{
			Cbig_edit f;
			error = f.open(m_mix_fname);
			if (!error)
			{
				for (auto& i : m_index_selected)
					f.erase(find_ref(m_index, get_id(i)).name);
				error = f.write_index();
				f.close();
			}
			break;
		}
		case ft_mix_rg:
		{
			Cmix_rg_edit f;
			error = f.open(m_mix_fname);
			if (!error)
			{
				for (auto& i : m_index_selected)
					f.erase(find_ref(m_index, get_id(i)).name);
				error = f.write_index();
				f.close();
			}
			break;
		}
		default:
			Cmix_edit f;
			error = f.open(m_mix_fname);
			if (!error)
			{
				for (auto& i : m_index_selected)
					f.erase(get_id(i));
				error = f.write_index();
				f.close();
			}
		}
		edit_reopen(!error);
		if (!editing_nested())
			OnPopupCompact();
	}
	else if (!del_into_mix)
	{
		for (auto& i : m_index_selected)
		{
			const t_index_entry& index = find_ref(m_index, get_id(i));
			if (index.ft == ft_dir)
			{
				if (!DeleteDirectory(m_dir + index.name, 1))
					error = 1;
			}
			else if (!DeleteFile((m_dir + index.name).c_str()))
				error = 1;
		}
	}
	else
	{
		// Deleting inside a MIX but the lazy nested extract failed -- nothing
		// changed; the on-disk files must not be touched.
		error = 1;
	}
	set_msg(error ? "Delete failed" : "Delete done");
	update_list();
}

void CXCCMixerView::OnUpdatePopupDelete(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_edit() && can_delete());
}

t_game CXCCMixerView::get_game()
{
	t_game r = GetMainFrame()->get_game();
	return r == -1 ? m_game : r;
}

void CXCCMixerView::OnPopupOpen()
{
	open_item(get_current_id());
}

void CXCCMixerView::OnUpdatePopupOpen(CCmdUI* pCmdUI)
{
	int id = get_current_id();
	if (id != -1)
	{
		switch (find_ref(m_index, id).ft)
		{
		case ft_aud:
		case ft_ogg:
		case ft_voc:
		case ft_wav:
			pCmdUI->Enable(!!GetMainFrame()->get_ds());
			return;
		case ft_big:
		case ft_dir:
		case ft_drive:
		case ft_lnkdir:
		case ft_mix:
		case ft_mix_rg:
		case ft_pak:
		case ft_shp_ts:
		case ft_vqa:
		case ft_wsa:
			pCmdUI->Enable(true);
			return;
		}
	}
	pCmdUI->Enable(false);
}

// "Open With..." — show the Windows app picker for the selected entry. Gated
// by theme::use_external_programs() so the gesture is coherent with the rest
// of the external-programs flow: when the toggle is off, every double-click
// and Open routes through the built-in viewer; turning the toggle on enables
// both the existing default-app open AND this picker. MIX-source entries are
// extracted to %TEMP%\xcc_mixer\ first (same flow as extract_and_open) so the
// picker has a real filesystem path to launch against.
void CXCCMixerView::OnPopupOpenWith()
{
	int id = get_current_id();
	if (id == -1)
		return;
	const t_index_entry& index = find_ref(m_index, id);
	if (index.name.empty())
		return;
	if (m_mix_f)
	{
		CWaitCursor wait;
		Cvirtual_binary d = m_mix_f->get_vdata(id);
		if (d.size())
			ext_open::extract_and_open_with(m_hWnd, index.name, d);
	}
	else
	{
		ext_open::open_with_path(m_hWnd, m_dir + index.name);
	}
	// SHOpenWithDialog leaves white strips across our dark listview rows
	// after it dismisses. Cause: the picker's own WM_DESTROY chain keeps
	// draining paints from the queue *after* control returns to us, so an
	// inline apply_theme_to_children + RedrawWindow gets overpainted by
	// stragglers. Post the reapply to the main frame instead so it runs
	// after the picker's dismiss paints have fully drained, on the next
	// idle of our message loop.
	if (CMainFrame* mf = GetMainFrame())
		mf->PostMessage(WM_USER + 0x102);
}

void CXCCMixerView::OnUpdatePopupOpenWith(CCmdUI* pCmdUI)
{
	if (!theme::use_external_programs())
	{
		pCmdUI->Enable(false);
		return;
	}
	int id = get_current_id();
	if (id == -1)
	{
		pCmdUI->Enable(false);
		return;
	}
	const t_index_entry& index = find_ref(m_index, id);
	// Containers / nav-style entries don't make sense to "open with" — they
	// open into the pane, they're not files an external app can handle.
	const bool is_container =
		index.ft == ft_mix || index.ft == ft_big || index.ft == ft_mix_rg
		|| index.ft == ft_pak || index.ft == ft_dir || index.ft == ft_drive
		|| index.ft == ft_lnkdir;
	pCmdUI->Enable(!is_container && !index.name.empty());
}

void CXCCMixerView::OnPopupExplore()
{
	ShellExecute(m_hWnd, "open", m_dir.c_str(), NULL, NULL, SW_SHOW);
}

void CXCCMixerView::OnUpdatePopupExplore(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(!m_mix_f);
}

// Right-click bookmark toggle. Operates on THIS pane (the one right-clicked,
// which is the active view by the time the popup shows): bookmarks its current
// MIX root path / folder, or removes it if already bookmarked.
void CXCCMixerView::OnPopupBookmark()
{
	std::string cur = nav_current_path();
	if (cur.empty())
		return;
	CMainFrame* mf = GetMainFrame();
	if (bookmarks::contains(cur))
	{
		bookmarks::remove(cur);
		if (mf) mf->set_msg(("Removed bookmark: " + cur).c_str());
	}
	else
	{
		bookmarks::add(cur);
		if (mf) mf->set_msg(("Bookmarked: " + cur).c_str());
	}
}

void CXCCMixerView::OnUpdatePopupBookmark(CCmdUI* pCmdUI)
{
	std::string cur = nav_current_path();
	if (cur.empty())
	{
		pCmdUI->Enable(FALSE);
		pCmdUI->SetText("Bookmark This Location");
		return;
	}
	pCmdUI->Enable(TRUE);
	pCmdUI->SetText(bookmarks::contains(cur)
		? "Remove This Location from Bookmarks"
		: "Bookmark This Location");
}

void CXCCMixerView::OnPopupRefresh()
{
	update_list();
}

void CXCCMixerView::OnUpdatePopupRefresh(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_accept());
}

int CXCCMixerView::resize(int id)
{
	Cfname fname = get_dir() + find_ref(m_index, id).name;
	switch (find_ref(m_index, id).ft)
	{
	case ft_jpeg:
	case ft_pcx:
	case ft_png:
	case ft_tga:
		{
			Cvirtual_image s = get_vimage_id(id);
			CResizeDlg dlg;
			dlg.set_size(s.cx(), s.cy());
			if (IDOK != dlg.DoModal())
				return 1;
			int cb_pixel = s.cb_pixel();
			Cvirtual_image s_palette = s;
			s.cb_pixel(4);
			Cvirtual_image d(NULL, dlg.get_cx(), dlg.get_cy(), 4, s_palette.palette());
			if (s.cx() < d.cx())
				resize_image_up(reinterpret_cast<const t_palette32_entry*>(s.image()), reinterpret_cast<t_palette32_entry*>(d.image_edit()), s.cx(), s.cy(), d.cx(), d.cy());
			else
				resize_image_down(reinterpret_cast<const t_palette32_entry*>(s.image()), reinterpret_cast<t_palette32_entry*>(d.image_edit()), s.cx(), s.cy(), d.cx(), d.cy());
			d.cb_pixel(cb_pixel, s_palette.palette());
			return d.save(fname, find_ref(m_index, id).ft);
		}
	case ft_shp_ts:
		{
			Cshp_ts_file f;
			int error = open_f_index(f, get_current_index());
			if (error)
				return error;
			const int global_cx = f.cx();
			const int global_cy = f.cy();
			CResizeDlg dlg;
			dlg.set_size(global_cx, global_cy);
			if (IDOK != dlg.DoModal())
				return 1;
			const int global_cx_d = dlg.get_cx();
			const int global_cy_d = dlg.get_cy();
			const int c_images = f.cf();
			t_palette palette;
			convert_palette_18_to_24(get_default_palette(), palette);
			palette[0].r = palette[0].b = 0xff;
			palette[0].g = 0;
			Cvirtual_binary rp;
			if (global_cx_d * global_cy_d * c_images > 1 << 18)
				create_downsample_table(palette, rp.write_start(1 << 18));
			Cvirtual_binary d8(NULL, global_cx_d * global_cy_d * c_images);
			t_palette32_entry* d32 = new t_palette32_entry[global_cx_d * global_cy_d];
			Cvirtual_binary image8(NULL, global_cx * global_cy);
			t_palette32_entry* image32 = new t_palette32_entry[global_cx * global_cy];
			for (int i = 0; i < c_images; i++)
			{
				set_msg("Resize: " + n(i * 100 / c_images) + "%");
				const int cx = f.get_cx(i);
				const int cy = f.get_cy(i);
				byte* image = new byte[cx * cy];
				const byte* r;
				if (f.is_compressed(i))
				{
					RLEZeroTSDecompress(f.get_image(i), image, cx, cy);
					r = image;
				}
				else
					r = f.get_image(i);
				memset(image8.data_edit(), 0, global_cx * global_cy);
				byte* w = image8.data_edit() + f.get_x(i) + global_cx * f.get_y(i);
				for (int y = 0; y < cy; y++)
				{
					memcpy(w, r, cx);
					r += cx;
					w += global_cx;
				}
				delete[] image;
				if (global_cx == global_cx_d && global_cy == global_cy_d)
					memcpy(d8.data_edit() + global_cx_d * global_cy_d * i, image8.data(), global_cx * global_cy);
				else
				{
					upsample_image(image8.data(), image32, global_cx, global_cy, palette);
					if (global_cx < global_cx_d)
						resize_image_up(image32, d32, global_cx, global_cy, global_cx_d, global_cy_d);
					else
						resize_image_down(image32, d32, global_cx, global_cy, global_cx_d, global_cy_d);
					if (rp.size())
						downsample_image(d32, d8.data_edit() + global_cx_d * global_cy_d * i, global_cx_d, global_cy_d, rp.data());
					else
						downsample_image(d32, d8.data_edit() + global_cx_d * global_cy_d * i, global_cx_d, global_cy_d, palette);
				}
			}
			if (dlg.m_fix_shadows && ~c_images & 1)
			{
				int count = global_cx_d * global_cy_d * c_images >> 1;
				for (byte* w = d8.data_edit() + count; count--; w++)
				{
					if (*w)
						*w = 1;
				}
			}
			delete[] image32;
			delete[] d32;
			f.close();
			return shp_ts_file_write(d8.data(), global_cx_d, global_cy_d, c_images).save(fname);
		}
	}
	return 1;
}

void CXCCMixerView::OnPopupResize()
{
	int id = get_current_id();
	Cfname fname = get_dir() + find_ref(m_index, id).name;
	int error = resize(id);
	set_msg("Resize " + fname.get_ftitle() + (error ? " failed, error " + n(error) : " succeeded"));
	update_list();
}

void CXCCMixerView::OnUpdatePopupResize(CCmdUI* pCmdUI)
{
	int id = get_current_id();
	if (can_accept() && id != -1)
	{
		switch (find_ref(m_index, id).ft)
		{
		case ft_jpeg:
		case ft_pcx:
		case ft_png:
		case ft_shp_ts:
		case ft_tga:
			pCmdUI->Enable(true);
			return;
		}
	}
	pCmdUI->Enable(false);
}

void CXCCMixerView::OnPopupClipboardCopy()
{
	Cvirtual_image vi = get_vimage(get_current_index());
	// 4bpp RGBA vimage means the source path produced an alpha-bearing image
	// (currently only the SHP_TS shadow_style_transparent_png branch). Route
	// to CF_PNG so alpha survives; alpha-blind apps get the CF_DIB fallback.
	if (vi.cb_pixel() == 4)
		vi.set_clipboard_png();
	else
		vi.set_clipboard();
}

void CXCCMixerView::OnPopupCopyName()
{
	string text;
	for (auto& i : m_index_selected)
	{
		int id = get_id(i);
		auto it = m_index.find(id);
		if (it == m_index.end())
			continue;
		const string& n = it->second.name;
		if (n.empty() || n == ".." || n == "Browse...")
			continue;
		if (!text.empty())
			text += "\r\n";
		text += n;
	}
	if (text.empty())
	{
		int id = get_current_id();
		if (id == -1)
			return;
		auto it = m_index.find(id);
		if (it == m_index.end())
			return;
		text = it->second.name;
		if (text.empty() || text == ".." || text == "Browse...")
			return;
	}
	set_clipboard_text(text);
}

void CXCCMixerView::set_clipboard_text(const string& text)
{
	if (text.empty() || !OpenClipboard())
		return;
	EmptyClipboard();
	size_t cb = text.size() + 1;
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cb);
	if (h)
	{
		void* p = GlobalLock(h);
		if (p)
		{
			memcpy(p, text.c_str(), cb);
			GlobalUnlock(h);
			SetClipboardData(CF_TEXT, h);
		}
		else
			GlobalFree(h);
	}
	CloseClipboard();
}

void CXCCMixerView::OnUpdatePopupCopyName(CCmdUI* pCmdUI)
{
	int id = get_current_id();
	bool ok = false;
	if (id != -1)
	{
		auto it = m_index.find(id);
		if (it != m_index.end())
		{
			const string& n = it->second.name;
			ok = !n.empty() && n != ".." && n != "Browse...";
		}
	}
	pCmdUI->Enable(ok);
}

void CXCCMixerView::OnUpdatePopupClipboardCopy(CCmdUI* pCmdUI)
{
	int id = get_current_id();
	pCmdUI->Enable(id != -1 && can_convert(find_ref(m_index, id).ft, ft_clipboard));
}

int CXCCMixerView::get_paste_fname(string& fname, t_file_type ft, const char* extension, const char* filter)
{
	int id = get_current_id();
	bool replace = id != -1 && find_ref(m_index, id).ft == ft;
	CFileDialog dlg(false, extension, replace ? (m_dir + find_ref(m_index, id).name).c_str() : NULL, OFN_HIDEREADONLY | OFN_PATHMUSTEXIST, filter, this);
	if (!replace)
		dlg.m_ofn.lpstrInitialDir = m_dir.c_str();
	if (IDOK != dlg.DoModal())
		return 1;
	fname = dlg.GetPathName();
	return 0;
}

void CXCCMixerView::paste_as_image(t_file_type ft, const char* extension, const char* filter)
{
	Cvirtual_image image;
	int error = image.get_clipboard();
	if (!error)
	{
		string fname;
		if (get_paste_fname(fname, ft, extension, filter))
			return;
		image.save(fname, ft);
		update_list();
	}
}

void CXCCMixerView::OnPopupClipboardPasteAsJpeg()
{
	paste_as_image(ft_jpeg, "jpeg", "JPEG files (*.jpeg;*.jpg)|*.jpeg;*.jpg|");
}

void CXCCMixerView::OnPopupClipboardPasteAsPcx()
{
	paste_as_image(ft_pcx, "pcx", "PCX files (*.pcx)|*.pcx|");
}

void CXCCMixerView::OnPopupClipboardPasteAsPng()
{
	paste_as_image(ft_png, "png", "PNG files (*.png)|*.png|");
}

void CXCCMixerView::OnPopupClipboardPasteAsTga()
{
	paste_as_image(ft_tga, "tga", "TGA files (*.tga)|*.tga|");
}

void CXCCMixerView::OnUpdatePopupClipboardPasteAsImage(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_accept() && IsClipboardFormatAvailable(CF_DIB));
}

void CXCCMixerView::OnPopupClipboardPasteAsShpTs()
{
	Cvirtual_image image;
	int error = image.get_clipboard();
	if (!error)
	{
		if (image.cb_pixel() == 3 || GetMainFrame()->use_palette_for_conversion())
		{
			t_palette p;
			memcpy(p, get_default_palette(), sizeof(t_palette));
			convert_palette_18_to_24(p);
			if (image.cb_pixel() == 3)
				image.decrease_color_depth(1, p);
			else
			{
				byte rp[256];
				if (GetMainFrame()->convert_from_td())
					create_rp(image.palette(), p, rp, game_td);
				else if (GetMainFrame()->convert_from_ra())
					create_rp(image.palette(), p, rp, game_ra);
				else
					create_rp(image.palette(), p, rp);
				apply_rp(image.image_edit(), image.cx() * image.cy(), rp);
			}
		}
		string fname;
		if (!get_paste_fname(fname, ft_shp_ts, "shp", "SHP files (*.shp)|*.shp|"))
		{
			Cshp_properties_dlg dlg;
			Cshp_ts_file f;
			int split_shadows = GetMainFrame()->split_shadows();
			int id = get_current_id();
			if (id != -1 && !open_f_id(f, id))
			{
				if (f.is_valid())
				{
					int c_images = f.cf();
					if (c_images < 2)
						split_shadows = false;
					dlg.set_size(f.cx(), f.cy(), c_images >> split_shadows);
				}
				else
					dlg.set_size(image.cx(), image.cy(), 1);
				f.close();
			}
			else
				dlg.set_size(image.cx(), image.cy(), 1);
			if (IDOK == dlg.DoModal())
			{
				int cblocks_x = image.cx() / dlg.get_cx();
				int cblocks_y = image.cy() / dlg.get_cy();
				int c_blocks = dlg.get_c_frames();
				shp_split_frames(image, cblocks_x, cblocks_y);
				if (split_shadows)
				{
					shp_split_shadows(image);
					c_blocks <<= 1;
				}
				shp_ts_file_write(image.image(), dlg.get_cx(), dlg.get_cy(), c_blocks, GetMainFrame()->enable_compression()).save(fname);
				update_list();
			}
		}
	}
}

void CXCCMixerView::OnUpdatePopupClipboardPasteAsVideo(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(can_accept() && IsClipboardFormatAvailable(CF_DIB));
}

BOOL CXCCMixerView::OnIdle(LONG lCount)
{
	if (m_reading)
	{
		for (auto& i : m_index)
		{
			t_index_entry& e = i.second;
			if (e.ft != -1)
				continue;
			Ccc_file f(false);
			if (f.open(m_dir + e.name))
			{
				e.ft = ft_unknown;
				e.size = -1;
			}
			else
			{
				e.ft = f.get_file_type();
				e.size = totalSize(f.get_size());
			}

			if (e.ft == ft_unknown)
			{
				Cfname fname = to_lower(e.name);
				if (fname.get_fext() == ".mix")
				{
					//e.ft = f.get_file_type_ext();
					e.ft = ft_mix;
					e.size = totalSize(f.get_size());
				}
			}
			CListCtrl& lc = GetListCtrl();
			LVFINDINFO lvf;
			lvf.flags = LVFI_PARAM;
			lvf.lParam = i.first;
			lc.Update(lc.FindItem(&lvf, -1));
			m_sort_column = -1;
			return true;
		}
		sort_list(0, false);
		sort_list(1, false);
		autosize_colums();
		m_reading = false;
	}
	return false;
}

string CXCCMixerView::report() const
{
	string page;
	ULARGE_INTEGER available, total, free;
	if (GetDiskFreeSpaceEx(m_dir.c_str(), &available, &total, &free))
		page += "<tr><td>" + m_dir + "<td align=right>" + n(available.QuadPart) + "<td align=right>" + n(total.QuadPart) + "<td>&nbsp;";
	page += "<tr><th>Name<th>Type<th>Size<th>Description";
	CListCtrl& lc = GetListCtrl();
	for (int i = 0; i < lc.GetItemCount(); i++)
	{
		const t_index_entry& e = find_ref(m_index, lc.GetItemData(i));
		page += "<tr><td>" + e.name + "<td>" + ft_name[e.ft] + "<td align=right>" + (e.size == "" ? "&nbsp;" : e.size) + "<td>" + (e.description.empty() ? "&nbsp;" : e.description);
	}
	return "<table border=1 width=100%>" + page + "</table>";
}

void CXCCMixerView::extract_open_audio_pak(const string& bag, const string& idx) const
{
	string bag_path = m_dir + bag;
	string idx_path = m_dir + idx;
	if (m_mix_f && (!Cfname(bag_path).exists() || !Cfname(idx_path).exists()))
	{
		int idx_id, bag_id;
		idx_id = Cmix_file::get_id(game_ra2_yr, idx);
		bag_id = Cmix_file::get_id(game_ra2_yr, bag);
		Ccc_file f_bag(false);
		if (!open_f_id(f_bag, bag_id))
			f_bag.extract(bag_path);
		Ccc_file f_idx(false);
		if (!open_f_id(f_idx, idx_id))
			f_idx.extract(idx_path);
	}

	CXSE_dlg dlg(game_ra2_yr);
	dlg.bag_file(bag_path);
	dlg.idx_file(idx_path);
	dlg.DoModal();
}

void CXCCMixerView::open_item(int id)
{
	const t_index_entry& index = find_ref(m_index, id);
	// Audio files always play via XCC's built-in player on double-click,
	// regardless of the external-programs setting — the user explicitly
	// wants XCC's player and not Windows Media Player or similar. Pressing
	// Space while focused on the same item toggles pause via the
	// PreTranslateMessage path, which calls the same play_audio_id below.
	if (index.ft == ft_aud || index.ft == ft_ogg ||
		index.ft == ft_voc || index.ft == ft_wav)
	{
		play_audio_id(id);
		return;
	}
	// External-programs mode: leaf files (anything that's not a MIX/dir
	// container) get either shell-opened in place (filesystem source) or
	// extracted to %TEMP%\xcc_mixer\ and shell-opened (MIX source). MIX
	// containers and dirs always keep their navigate-into behavior.
	if (theme::use_external_programs())
	{
		const bool is_container =
			index.ft == ft_mix || index.ft == ft_big || index.ft == ft_mix_rg
			|| index.ft == ft_pak || index.ft == ft_dir || index.ft == ft_drive
			|| index.ft == ft_lnkdir;
		if (!is_container && !index.name.empty())
		{
			if (m_mix_f)
			{
				CWaitCursor wait;
				Cvirtual_binary d = m_mix_f->get_vdata(id);
				if (d.size() && ext_open::extract_and_open(m_hWnd, index.name, d))
					return;
				// Fall through to default behavior on failure.
			}
			else
			{
				::ShellExecute(m_hWnd, "open", (m_dir + index.name).c_str(),
					NULL, NULL, SW_SHOW);
				return;
			}
		}
	}
	switch (index.ft)
	{
	case ft_aud:
	case ft_ogg:
	case ft_voc:
	case ft_wav:
		// Unreachable: audio is intercepted at the top of open_item and
		// routed to play_audio_id (XCC's built-in player). Kept as an
		// explicit no-op so a stray double-click doesn't fall through to
		// the default ShellExecute branch in case the early-return guard
		// is ever changed.
		break;
	case ft_dir:
		{
			string name = index.name;
			if (name == "..")
			{
				nav_go_up();
			}
			else
			{
				close_location(false);
				open_location_dir(m_dir + name + '\\');
			}
			break;
		}
	case ft_lnkdir:
	{
		close_location(false);
		Cfname lnkdir(index.name);
		CString cs;
		resolveShortcutTarget(m_hWnd, (m_dir + lnkdir.get_fname()).c_str(), cs);
		m_dir = cs + '\\';
		update_list();
		break;
	}
	case ft_drive:
		{
			string name = index.name;
			if (name == "Browse...")
			{
				CFolderPickerDialog dlg(m_dir.c_str(), NULL, this);
				if (IDOK == dlg.DoModal())
				{
					close_all_locations();
					open_location_dir(static_cast<string>(dlg.GetPathName() + '\\'));
				}
				break;
			}
			close_location(false);
			open_location_dir(name);
			break;
		}
	case ft_big:
	case ft_mix:
	case ft_mix_rg:
	case ft_pak:
		{
			if (m_mix_f)
				open_location_mix(id);
			else
				open_location_mix(m_dir + index.name);
			break;
		}
	case ft_csf:
	{
		CXSTE_dlg dlg2(game_unknown);
		if (m_mix_f)
		{
			std::string path = ext_open::temp_dir() + ext_open::sanitize(index.name);
			get_vdata_id(id).save(path);
			ext_open::g_temp_files.push_back(path);
			dlg2.open(path);
		}
		else
			dlg2.open(m_dir + index.name);
		dlg2.DoModal();
		break;
	}
	case ft_unknown:
		{
			Cfname unknown_file(index.name);
			t_index_entry* ptr_entry;
			if (unknown_file.get_fext() == ".bag")
			{
				extract_open_audio_pak(unknown_file, unknown_file.get_ftitle() + ".idx");
			}
			else if (unknown_file.get_fext() == ".idx")
			{
				extract_open_audio_pak(unknown_file.get_ftitle() + ".bag", unknown_file);
			}
			else if (!m_mix_f)
			{
				ShellExecute(m_hWnd, "open", (m_dir + unknown_file.get_fname()).c_str(), NULL, NULL, SW_SHOW);
			} 
			break;
		}
	case ft_shp:
	case ft_shp_ts:
	case ft_vqa:
	case ft_wsa_dune2:
	case ft_wsa:
		{
			Cvideo_decoder* decoder = NULL;
			switch (index.ft)
			{
			case ft_shp:
				{
					Cshp_file f;
					f.load(get_vdata_id(id));
					decoder = f.decoder(get_default_palette());
				}
				break;
			case ft_shp_ts:
				{
					Cshp_ts_file f;
					f.load(get_vdata_id(id));
					decoder = f.decoder(get_default_palette());
				}
				break;
			case ft_vqa:
				{
					Cvqa_file f;
					f.load(get_vdata_id(id));
					decoder = f.decoder();
				}
				break;
			case ft_wsa_dune2:
				{
					Cwsa_dune2_file f;
					f.load(get_vdata_id(id));
					decoder = f.decoder(get_default_palette());
				}
				break;
			case ft_wsa:
				{
					Cwsa_file f;
					f.load(get_vdata_id(id));
					decoder = f.decoder();
				}
				break;
			}
			if (!decoder)
				break;
			Cdlg_shp_viewer dlg;
			dlg.write(decoder);
			// For VQA: pre-decode audio + frame rate and hand them to the
			// dialog so it can drive the timer at the correct fps, play
			// sound through xap_play, and display elapsed/total duration.
			if (index.ft == ft_vqa)
			{
				Cvqa_file fa;
				fa.load(get_vdata_id(id));
				Cvirtual_binary wav;
				if (fa.decode_audio_to_wav(wav))
					wav = Cvirtual_binary();
				Cvqa_file ff;
				ff.load(get_vdata_id(id));
				dlg.write_av(wav, ff.frame_rate(), index.name);
			}
			dlg.DoModal();
			delete decoder;
			break;
		}
	default:
		{
			if (!m_mix_f)
			{
				Cfname unknown_file(index.name);
				ShellExecute(m_hWnd, "open", (m_dir + unknown_file.get_fname()).c_str(), NULL, NULL, SW_SHOW);
			}
			break;
		}
	}
}

// Play/toggle the audio file with id `id` via xap_play. Bound to Space in
// PreTranslateMessage and double-click in open_item. xap_play handles the
// "same-file = stop" toggle natively; xap_get_progress / xap_is_paused let
// the player dialog reflect transport state without us tracking it here.
void CXCCMixerView::play_audio_id(int id)
{
	if (!GetMainFrame()->get_ds())
		return;
	const t_index_entry& index = find_ref(m_index, id);
	if (index.ft != ft_aud && index.ft != ft_ogg && index.ft != ft_voc && index.ft != ft_wav)
		return;
	CWaitCursor wait;
	Ccc_file f(true);
	if (open_f_id(f, id))
		return;
	// xap_play treats "same currently-playing file" as a stop-toggle.
	// Sample the global xapFilePlaying *before* the call so we can detect
	// that case without racing the worker thread that sets xapFilePlaying
	// asynchronously after the new playback starts.
	const std::string prev_playing = xapFilePlaying;
	xap_play(GetMainFrame()->get_ds(), f.vdata(), index.name);
	const bool was_toggle_stop = (prev_playing == index.name);
	if (was_toggle_stop)
	{
		// Stopped the same file — hide the dialog so it doesn't linger.
		if (m_audio_dlg && m_audio_dlg->GetSafeHwnd())
			m_audio_dlg->ShowWindow(SW_HIDE);
	}
	else
	{
		// Either fresh play or switching files: show the mini-player and
		// re-bind the filename + duration. Duration may briefly read -1 if
		// the worker thread hasn't published it yet; the dialog's poll
		// timer picks it up on the next tick.
		if (CAudioPlayerDlg* dlg = ensure_audio_dlg())
			dlg->on_playback_started(index.name);
	}
}

CAudioPlayerDlg* CXCCMixerView::ensure_audio_dlg()
{
	if (!m_audio_dlg)
	{
		m_audio_dlg = std::make_unique<CAudioPlayerDlg>(GetMainFrame());
		// Modeless: Create() returns immediately. Parent is the main frame
		// rather than this listview so the dialog floats over the whole
		// app and survives if the file list is re-populated.
		if (!m_audio_dlg->Create(IDD_AUDIO_PLAYER, GetMainFrame()))
		{
			m_audio_dlg.reset();
			return nullptr;
		}
	}
	return m_audio_dlg.get();
}

BOOL CXCCMixerView::PreTranslateMessage(MSG* pMsg)
{
	// Route configurable list-view hotkeys through keybinds. Caught here
	// (not OnKeyDown) so it overrides the listview's default key handling.
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == GetListCtrl().GetSafeHwnd())
	{
		bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
		bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
		// Tab / Ctrl+Tab focus moves are handled globally in
		// CMainFrame::PreTranslateMessage (work regardless of focus).
		UINT action = 0;
		if (keybinds::match_view(keybinds::scope_list_view, (UINT)pMsg->wParam, ctrl, shift, alt, action))
		{
			// Filter auto-repeat: holding the key generates ~30 KEYDOWNs per
			// second; each would spawn a new playback thread and trash the
			// shared DirectSound buffer state. lParam bit 30 = 1 means the
			// key was already down before this event.
			if (pMsg->lParam & (1 << 30))
				return TRUE;
			if (action == keybinds::vact_play_audio)
			{
				int id = get_current_id();
				if (id != -1)
				{
					const t_index_entry& index = find_ref(m_index, id);
					if (index.ft == ft_aud || index.ft == ft_ogg
						|| index.ft == ft_voc || index.ft == ft_wav)
					{
						play_audio_id(id);
						return TRUE;
					}
				}
			}
		}
	}
	return CListView::PreTranslateMessage(pMsg);
}

void CXCCMixerView::OnEditSelectAll()
{
	CListCtrl& lc = GetListCtrl();
	for (int index = 0; index < lc.GetItemCount(); index++)
	{
		switch (find_ref(m_index, lc.GetItemData(index)).ft)
		{
		case ft_dir:
		case ft_drive:
		case ft_lnkdir:
			lc.SetItemState(index, 0, LVIS_SELECTED);
			break;
		default:
			lc.SetItemState(index, LVIS_SELECTED, LVIS_SELECTED);
		}
	}
}
