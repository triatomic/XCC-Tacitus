#include "stdafx.h"
#include "ListCtrlEx.h"

static const CListCtrlEx_theme* g_listctrl_theme = nullptr;

void CListCtrlEx_set_theme(const CListCtrlEx_theme* hook)
{
	g_listctrl_theme = hook;
}

BEGIN_MESSAGE_MAP(CListCtrlEx, CListCtrl)
	//{{AFX_MSG_MAP(CListCtrlEx)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CListCtrlEx::auto_size()
{
	if (!GetSafeHwnd() || !GetHeaderCtrl())
		return;
	auto count = GetHeaderCtrl()->GetItemCount();
	for (int i = 0; i < count; i++)
	{
		// to prevent crash
		switch (i)
		{
		case 0:
			SetColumnWidth(i, 160);//LVSCW_AUTOSIZE_USEHEADER);
			break;
		case 1:
			SetColumnWidth(i, 360);//LVSCW_AUTOSIZE_USEHEADER);
			break;
		default:
			SetColumnWidth(i, 200);//LVSCW_AUTOSIZE_USEHEADER);
			break;
		}
	}
}

void CListCtrlEx::set_size(int width, int column)
{
	if (!GetSafeHwnd() || !GetHeaderCtrl())
		return;
	auto count = GetHeaderCtrl()->GetItemCount();
	if (column < 0 || column >= count)
		return;
	SetColumnWidth(column, width <= 0 ? LVSCW_AUTOSIZE_USEHEADER : width);
}

void CListCtrlEx::DeleteAllColumns()
{
	while (GetHeaderCtrl()->GetItemCount())
		DeleteColumn(0);
}

DWORD CListCtrlEx::GetItemData(int nItem) const
{
	return nItem == -1 ? -1 : CListCtrl::GetItemData(nItem);
}

int CListCtrlEx::InsertItemData(int nItem, DWORD dwData)
{
	int index = InsertItem(nItem, LPSTR_TEXTCALLBACK);
	SetItemData(index, dwData);
	return index;
}

int CListCtrlEx::InsertItemData(DWORD dwData)
{
	return InsertItemData(GetItemCount(), dwData);
}

void CListCtrlEx::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	if ((GetStyle() & LVS_TYPEMASK) != LVS_REPORT)
		return;
	NMLVCUSTOMDRAW* pCustomDraw = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	const bool dark = g_listctrl_theme && g_listctrl_theme->is_dark && g_listctrl_theme->is_dark();
	switch (pCustomDraw->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		// Ask for both pre- and post-item-paint notifications so we can
		// (a) substitute theme colors in pre-paint and (b) paint our own
		// grid lines in post-paint.
		*pResult = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
		break;
	case CDDS_ITEMPREPAINT:
		// In dark mode, paint every row with the theme background and the
		// theme foreground text. Without this, the per-row text color stayed
		// at the system default (near-black) over the dark listview bg, which
		// rendered as the "ghosted" rows seen in the search dialogs. Return
		// CDRF_NEWFONT so the common control honors the new colors. Also ask
		// for an item post-paint callback so we can draw the grid lines on
		// top of the row background after the system has finished its row
		// rendering.
		if (dark)
		{
			pCustomDraw->clrText = g_listctrl_theme->text ? g_listctrl_theme->text() : RGB(0xff, 0xff, 0xff);
			pCustomDraw->clrTextBk = g_listctrl_theme->row_bg ? g_listctrl_theme->row_bg() : RGB(0x20, 0x20, 0x20);
			*pResult = CDRF_NEWFONT | CDRF_NOTIFYPOSTPAINT;
		}
		else
		{
			pCustomDraw->clrTextBk = pCustomDraw->nmcd.dwItemSpec & 1 ? RGB(0xf8, 0xf8, 0xf8) : RGB(0xff, 0xff, 0xff);
		}
		break;
	case CDDS_ITEMPOSTPAINT:
		// Dark-mode grid: LVS_EX_GRIDLINES paints with a hardcoded light gray
		// that looks ugly on dark backgrounds. We strip that style in dark
		// mode (apply_grid in the host app's theme module) and draw our own
		// row + column separators here using the theme's grid color.
		if (dark
			&& g_listctrl_theme->show_grid && g_listctrl_theme->show_grid()
			&& g_listctrl_theme->grid)
		{
			HDC hdc = pCustomDraw->nmcd.hdc;
			if (hdc)
			{
				COLORREF c = g_listctrl_theme->grid();
				HPEN pen = ::CreatePen(PS_SOLID, 1, c);
				HGDIOBJ old = ::SelectObject(hdc, pen);
				const RECT& rc = pCustomDraw->nmcd.rc;
				// Bottom row separator.
				::MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
				::LineTo(hdc, rc.right, rc.bottom - 1);
				// Per-column right-edge separators.
				const int n_cols = GetHeaderCtrl() ? GetHeaderCtrl()->GetItemCount() : 0;
				int x = rc.left;
				for (int i = 0; i < n_cols; i++)
				{
					x += GetColumnWidth(i);
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

void CListCtrlEx::OnSize(UINT nType, int cx, int cy)
{
	CListCtrl::OnSize(nType, cx, cy);
	auto_size();
}

void CListCtrlEx::PreSubclassWindow()
{
	CListCtrl::PreSubclassWindow();
	SetExtendedStyle(GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
}

BOOL CListCtrlEx::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (GetKeyState(VK_CONTROL) < 0)
		{
			switch (pMsg->wParam)
			{
			case 'A':
				if (GetStyle() & LVS_SINGLESEL)
					break;
				select_all();
				return true;
			case VK_ADD:
				auto_size();
				return true;
			}
		}
	}
	return CListCtrl::PreTranslateMessage(pMsg);
}

void CListCtrlEx::select_all()
{
	for (int i = 0; i < GetItemCount(); i++)
		SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
}

std::string& CListCtrlEx::get_buffer()
{
	return m_buffer[++m_buffer_w &= 3].erase();
}

std::string CListCtrlEx::get_selected_rows_tsv()
{
	string d;
	for (int j = 0; j < GetHeaderCtrl()->GetItemCount(); j++)
	{
		const int cb_b = 256;
		char b[cb_b];
		HDITEM item;
		item.mask = HDI_TEXT;
		item.pszText = b;
		item.cchTextMax = cb_b - 1;
		GetHeaderCtrl()->GetItem(j, &item);
		d += item.pszText;
		d += "\t";
	}
	d += "\r\n";
	for (int i = -1; (i = GetNextItem(i, LVNI_SELECTED)) != -1; )
	{
		for (int j = 0; j < GetHeaderCtrl()->GetItemCount(); j++)
			d += GetItemText(i, j) + "\t";
		d += "\r\n";
	}
	return d;
}
