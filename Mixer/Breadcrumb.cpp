#include "stdafx.h"
#include "Breadcrumb.h"
#include "theme.h"

// Layout metrics. Mixer is fixed-DPI, so plain pixel constants are fine (same
// assumption the dark edit-border subclass makes).
namespace
{
	const int kPadX = 6;       // left/right padding inside the bar
	const int kSegGap = 2;     // gap between a label and its chevron
	const int kChevronW = 14;  // width reserved for the ">" separator
	const wchar_t kChevron = L'\x203A';  // single right-pointing angle quote
	const char* kEllipsis = "...";

	COLORREF text_color()      { return theme::is_dark() ? theme::text()     : GetSysColor(COLOR_WINDOWTEXT); }
	COLORREF dim_color()       { return theme::is_dark() ? theme::text_dim() : GetSysColor(COLOR_GRAYTEXT); }
	COLORREF hot_text_color()  { return theme::is_dark() ? theme::text()     : GetSysColor(COLOR_HIGHLIGHTTEXT); }
	COLORREF hot_bk_color()    { return theme::is_dark() ? theme::menu_hot() : GetSysColor(COLOR_HIGHLIGHT); }
	COLORREF border_color()    { return theme::is_dark() ? theme::border()   : GetSysColor(COLOR_3DSHADOW); }
	COLORREF bar_bg_color()    { return theme::is_dark() ? theme::bg_alt()   : GetSysColor(COLOR_3DFACE); }
	COLORREF accent_color()    { return theme::is_dark() ? theme::accent()   : GetSysColor(COLOR_HIGHLIGHT); }

	void fill_bg(CDC* dc, const CRect& rc)
	{
		if (theme::is_dark())
			::FillRect(dc->GetSafeHdc(), rc, theme::bg_brush());
		else
			::FillRect(dc->GetSafeHdc(), rc, GetSysColorBrush(COLOR_WINDOW));
	}
}

// ===================== CPathEdit =====================

BEGIN_MESSAGE_MAP(CPathEdit, CEdit)
	ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

BOOL CPathEdit::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		CBreadcrumbBar* bar = static_cast<CBreadcrumbBar*>(GetParent());
		if (pMsg->wParam == VK_RETURN)
		{
			if (bar) bar->on_edit_commit();
			return TRUE;   // swallow (don't beep / insert newline)
		}
		if (pMsg->wParam == VK_ESCAPE)
		{
			if (bar) bar->on_edit_cancel();
			return TRUE;
		}
	}
	return CEdit::PreTranslateMessage(pMsg);
}

void CPathEdit::OnKillFocus(CWnd* pNewWnd)
{
	CEdit::OnKillFocus(pNewWnd);
	// Losing focus cancels edit mode (Explorer behavior). Guard against the
	// kill-focus that fires while we're being hidden by a successful commit.
	CBreadcrumbBar* bar = static_cast<CBreadcrumbBar*>(GetParent());
	if (bar && bar->in_edit_mode())
		bar->exit_edit_mode();
}

// ===================== CBreadcrumbBar =====================

BEGIN_MESSAGE_MAP(CBreadcrumbBar, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONUP()
	ON_WM_SIZE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

// The path edit is a child of the bar (not the frame), so its WM_CTLCOLOREDIT
// comes here — without this it paints with the default white background in dark
// mode. Mirror CMainFrame::OnCtlColor.
HBRUSH CBreadcrumbBar::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (theme::is_dark())
	{
		pDC->SetTextColor(theme::text());
		pDC->SetBkColor(theme::bg());
		pDC->SetBkMode(TRANSPARENT);
		return theme::bg_brush();
	}
	return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CBreadcrumbBar::Create(CWnd* parent, UINT id)
{
	// Register a class with no background brush — we paint the whole client in
	// OnPaint, so a class brush would just cause flicker.
	LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
		::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	if (!CWnd::Create(cls, _T(""), WS_CHILD | WS_VISIBLE,
		CRect(0, 0, 0, 0), parent, id))
		return FALSE;
	// Inner editable path box, hidden until edit mode. ID is arbitrary (child of
	// the bar, not the frame). ES_AUTOHSCROLL so long paths scroll horizontally.
	m_path_edit.Create(WS_CHILD | ES_AUTOHSCROLL, CRect(0, 0, 0, 0), this, 1);
	m_path_edit.SetFont(GetFont() ? GetFont()
		: CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
	return TRUE;
}

void CBreadcrumbBar::refresh_theme()
{
	if (GetSafeHwnd())
		Invalidate(FALSE);
	if (m_path_edit.GetSafeHwnd())
		theme::apply_edit(m_path_edit.GetSafeHwnd());
}

void CBreadcrumbBar::set_segments(const std::vector<std::string>& segs,
	const std::string& full_path)
{
	m_full_path = full_path;
	if (segs == m_segments)
	{
		// Same path: still repaint (theme may have flipped) but skip re-layout.
		if (GetSafeHwnd())
			Invalidate(FALSE);
		return;
	}
	m_segments = segs;
	m_hot = -1;
	// A navigation while the editor is open (e.g. via the other pane) drops edit
	// mode so the bar reflects the new location.
	if (m_editing)
		exit_edit_mode();
	layout_segments();
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CBreadcrumbBar::enter_edit_mode()
{
	if (m_editing || !m_path_edit.GetSafeHwnd())
		return;
	m_editing = true;
	theme::apply_edit(m_path_edit.GetSafeHwnd());
	CRect rc;
	GetClientRect(rc);
	m_path_edit.MoveWindow(rc, FALSE);
	m_path_edit.SetWindowText(m_full_path.c_str());
	m_path_edit.ShowWindow(SW_SHOW);
	m_path_edit.SetFocus();
	m_path_edit.SetSel(0, -1);   // select all so paste / typing replaces
	Invalidate(FALSE);
}

void CBreadcrumbBar::exit_edit_mode()
{
	if (!m_editing)
		return;
	m_editing = false;
	if (m_path_edit.GetSafeHwnd())
		m_path_edit.ShowWindow(SW_HIDE);
	Invalidate(FALSE);
}

CString CBreadcrumbBar::edited_path() const
{
	CString s;
	if (m_path_edit.GetSafeHwnd())
		m_path_edit.GetWindowText(s);
	return s;
}

void CBreadcrumbBar::on_edit_commit()
{
	// Tell the parent to navigate; it reads edited_path(). If navigation
	// succeeds the parent's set_segments() will exit edit mode for us; if it
	// fails we leave the editor open so the user can fix the path.
	if (CWnd* p = GetParent())
		p->PostMessage(WM_BREADCRUMB_PATH, 0, 0);
}

void CBreadcrumbBar::on_edit_cancel()
{
	exit_edit_mode();
	// Return focus to the bar's parent so the edit doesn't keep capturing keys.
	if (CWnd* p = GetParent())
		p->SetFocus();
}

void CBreadcrumbBar::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	layout_segments();
	if (m_editing && m_path_edit.GetSafeHwnd())
	{
		CRect rc;
		GetClientRect(rc);
		m_path_edit.MoveWindow(rc, TRUE);
	}
	Invalidate(FALSE);
}

void CBreadcrumbBar::layout_segments()
{
	m_drawn.clear();
	m_first_visible = 0;
	if (!GetSafeHwnd() || m_segments.empty())
		return;

	CRect rc;
	GetClientRect(rc);
	const int avail = rc.Width() - 2 * kPadX;
	if (avail <= 0)
		return;

	CClientDC dc(this);
	CFont* old = dc.SelectObject(GetFont() ? GetFont()
		: CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));

	auto label_w = [&](const std::string& s)
	{
		CSize sz = dc.GetTextExtent(s.c_str(), static_cast<int>(s.length()));
		return sz.cx + 2 * kSegGap;
	};

	// Measure full width: every segment label + a trailing chevron after each
	// (the last segment now has one too — see layout below).
	auto total_from = [&](int first)
	{
		int w = 0;
		for (int i = first; i < static_cast<int>(m_segments.size()); i++)
			w += label_w(m_segments[i]) + kChevronW;
		return w;
	};

	// Left-elide: drop leading segments (replaced by a "..." stub) until the
	// remainder fits. Explorer keeps the deepest levels visible.
	int first = 0;
	while (first < static_cast<int>(m_segments.size()) - 1)
	{
		int w = total_from(first);
		if (first > 0)
			w += label_w(kEllipsis) + kChevronW; // room for the "..." stub
		if (w <= avail)
			break;
		first++;
	}
	m_first_visible = first;

	int x = rc.left + kPadX;
	const int nseg = static_cast<int>(m_segments.size());
	if (first > 0)
	{
		// Leading "..." stub: not a real level. Its chevron, however, drops the
		// children of the deepest hidden level so elided folders stay reachable.
		t_seg e;
		e.label = kEllipsis;
		int w = label_w(kEllipsis);
		e.label_hit = CRect(x, rc.top, x + w, rc.bottom);
		x += w;
		e.chevron_hit = CRect(x, rc.top, x + kChevronW, rc.bottom);
		x += kChevronW;
		m_drawn.push_back(e);
	}
	for (int i = first; i < nseg; i++)
	{
		t_seg s;
		s.label = m_segments[i];
		int w = label_w(s.label);
		s.label_hit = CRect(x, rc.top, x + w, rc.bottom);
		x += w;
		// Every segment gets a chevron, including the last (current) one — like
		// Explorer. The last segment's chevron drops the current location's own
		// navigable children (subfolders / nested MIXes), so you can jump into a
		// child without scrolling the list below.
		s.chevron_hit = CRect(x, rc.top, x + kChevronW, rc.bottom);
		x += kChevronW;
		m_drawn.push_back(s);
	}

	dc.SelectObject(old);
}

int CBreadcrumbBar::hit_test_label(CPoint pt) const
{
	for (int i = 0; i < static_cast<int>(m_drawn.size()); i++)
		if (m_drawn[i].label_hit.PtInRect(pt))
			return i;
	return -1;
}

int CBreadcrumbBar::hit_test_chevron(CPoint pt) const
{
	for (int i = 0; i < static_cast<int>(m_drawn.size()); i++)
		if (!m_drawn[i].chevron_hit.IsRectEmpty()
			&& m_drawn[i].chevron_hit.PtInRect(pt))
			return i;
	return -1;
}

int CBreadcrumbBar::real_index(int drawn) const
{
	if (drawn < 0)
		return -1;
	if (m_first_visible > 0)
	{
		if (drawn == 0)
			return -1;                       // the "..." stub
		return m_first_visible + (drawn - 1);
	}
	return drawn;
}

BOOL CBreadcrumbBar::OnEraseBkgnd(CDC*)
{
	return TRUE; // fully painted in OnPaint
}

void CBreadcrumbBar::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(rc);

	// In edit mode the path edit covers the client area; just fill the
	// background (the edit paints itself).
	if (m_editing)
	{
		fill_bg(&dc, rc);
		return;
	}

	// Double-buffer to kill flicker on hover repaints.
	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* oldb = mem.SelectObject(&bmp);

	fill_bg(&mem, rc);
	CFont* oldf = mem.SelectObject(GetFont() ? GetFont()
		: CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
	mem.SetBkMode(TRANSPARENT);

	for (int i = 0; i < static_cast<int>(m_drawn.size()); i++)
	{
		const t_seg& s = m_drawn[i];
		const bool is_stub = (m_first_visible > 0 && i == 0);
		const bool label_hot = (i == m_hot && !is_stub);

		if (label_hot)
		{
			CBrush hb(hot_bk_color());
			mem.FillRect(s.label_hit, &hb);
			mem.SetTextColor(hot_text_color());
		}
		else
			mem.SetTextColor(is_stub ? dim_color() : text_color());

		CRect tr = s.label_hit;
		tr.left += kSegGap;
		mem.DrawText(s.label.c_str(), static_cast<int>(s.label.length()), tr,
			DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

		// Chevron (drop point for this level's children). Highlight on hover so
		// it reads as a separate, clickable target.
		if (!s.chevron_hit.IsRectEmpty())
		{
			const bool chev_hot = (i == m_hot_chevron);
			if (chev_hot)
			{
				CBrush hb(hot_bk_color());
				mem.FillRect(s.chevron_hit, &hb);
				mem.SetTextColor(hot_text_color());
			}
			else
				mem.SetTextColor(dim_color());
			CRect cr = s.chevron_hit;
			::DrawTextW(mem.GetSafeHdc(), &kChevron, 1, &cr,
				DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
		}
	}

	mem.SelectObject(oldf);
	dc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldb);
}

void CBreadcrumbBar::OnMouseMove(UINT, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		TrackMouseEvent(&tme);
		m_tracking = true;
	}
	int lab = hit_test_label(point);
	// The leading "..." stub label isn't navigable (its chevron is, though).
	if (m_first_visible > 0 && lab == 0)
		lab = -1;
	int chev = hit_test_chevron(point);
	if (lab != m_hot || chev != m_hot_chevron)
	{
		m_hot = lab;
		m_hot_chevron = chev;
		SetCursor(::LoadCursor(NULL,
			(m_hot >= 0 || m_hot_chevron >= 0) ? IDC_HAND : IDC_ARROW));
		Invalidate(FALSE);  // segment-change gated — no 1000Hz repaint storm
	}
}

void CBreadcrumbBar::OnMouseLeave()
{
	m_tracking = false;
	if (m_hot != -1 || m_hot_chevron != -1)
	{
		m_hot = -1;
		m_hot_chevron = -1;
		Invalidate(FALSE);
	}
}

void CBreadcrumbBar::OnLButtonUp(UINT, CPoint point)
{
	CWnd* p = GetParent();
	if (!p)
		return;

	// Chevron click → child dropdown for that level (real index; the stub's
	// chevron maps to the deepest hidden level so elided folders stay reachable).
	int chev = hit_test_chevron(point);
	if (chev >= 0)
	{
		int real = (m_first_visible > 0 && chev == 0)
			? (m_first_visible - 1)      // stub chevron = deepest hidden level
			: real_index(chev);
		if (real >= 0)
		{
			CPoint scr = m_drawn[chev].chevron_hit.BottomRight();
			scr.x = m_drawn[chev].chevron_hit.left;
			ClientToScreen(&scr);
			p->PostMessage(WM_BREADCRUMB_CHEVRON, real,
				MAKELPARAM(scr.x, scr.y));
		}
		return;
	}

	// Label click → navigate to that level.
	int lab = hit_test_label(point);
	int real = real_index(lab);
	if (real >= 0)
	{
		p->PostMessage(WM_BREADCRUMB_CLICK, real, 0);
		return;
	}

	// Click on blank area (not a segment or chevron) → Explorer-style edit mode:
	// the bar becomes an editable path box you can copy from / paste into.
	enter_edit_mode();
}

// ===================== CTopbarDivider =====================

BEGIN_MESSAGE_MAP(CTopbarDivider, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SETCURSOR()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

BOOL CTopbarDivider::Create(CWnd* parent, UINT id)
{
	LPCTSTR cls = AfxRegisterWndClass(0,
		::LoadCursor(NULL, IDC_SIZEWE), NULL, NULL);
	return CWnd::Create(cls, _T(""), WS_CHILD | WS_VISIBLE,
		CRect(0, 0, 0, 0), parent, id);
}

BOOL CTopbarDivider::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void CTopbarDivider::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(rc);

	// Painted like the app's themed splitter bar: a flat bar fill flanked by 1px
	// borders on each edge, with a centered gripper. Highlight (accent) while
	// hovered or dragging so it reads obviously as a resize handle.
	const bool active = m_hot || m_dragging;
	dc.FillSolidRect(rc, bar_bg_color());

	// Edge borders.
	dc.FillSolidRect(CRect(rc.left, rc.top, rc.left + 1, rc.bottom), border_color());
	dc.FillSolidRect(CRect(rc.right - 1, rc.top, rc.right, rc.bottom), border_color());

	// Centered vertical gripper line (brighter when active).
	int cx = rc.left + rc.Width() / 2;
	COLORREF grip = active ? accent_color() : dim_color();
	CPen pen(PS_SOLID, 1, grip);
	CPen* old = dc.SelectObject(&pen);
	int inset = 3;
	dc.MoveTo(cx, rc.top + inset);
	dc.LineTo(cx, rc.bottom - inset);
	dc.SelectObject(old);
}

BOOL CTopbarDivider::OnSetCursor(CWnd*, UINT, UINT)
{
	::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
	return TRUE;
}

// Translate a mouse point (divider-client) to the tracking line's x in the
// parent's client coords, clamped so neither zone collapses. Mirrors the
// clamp in CMainFrame::layout_filter_bar (min 120 filter / 160 breadcrumb).
int CTopbarDivider::clamp_track_x(CPoint client_pt) const
{
	CWnd* parent = GetParent();
	if (!parent)
		return 0;
	CPoint pt = client_pt;
	ClientToScreen(&pt);
	parent->ScreenToClient(&pt);
	CRect pc;
	parent->GetClientRect(pc);

	const int pad = 2, divW = 7, min_filter = 120, min_crumb = 160;
	const int left = pc.left + pad;
	const int right = pc.right - pad;
	// Left/right minimums depend on which zone is on which side. Normally the
	// breadcrumb is left and the filter right; swapped flips them.
	const bool swapped = theme::topbar_swapped();
	const int min_left  = swapped ? min_filter : min_crumb;
	const int min_right = swapped ? min_crumb  : min_filter;
	int x = pt.x;                              // divider's left edge
	int min_x = left + min_left;               // keep the left zone >= min
	int max_x = right - divW - min_right;      // keep the right zone >= min
	if (max_x < min_x) max_x = min_x;          // degenerate tiny window
	if (x < min_x) x = min_x;
	if (x > max_x) x = max_x;
	return x;
}

// XOR a 2px vertical tracking line over the parent's top strip at parent-client
// x. Drawing the same line twice erases it (R2_NOTXORPEN), so a move = erase
// old + draw new. Flicker-free: nothing live-resizes during the drag.
void CTopbarDivider::draw_track_line(int parent_x)
{
	CWnd* parent = GetParent();
	if (!parent)
		return;
	CRect rc;
	GetWindowRect(rc);          // divider's screen rect gives the strip height
	int h = rc.Height();
	CDC* dc = parent->GetDC();
	int old_rop = dc->SetROP2(R2_NOTXORPEN);
	CPen pen(PS_SOLID, 2, RGB(128, 128, 128));
	CPen* old_pen = dc->SelectObject(&pen);
	CPoint top(rc.left, rc.top);
	parent->ScreenToClient(&top);
	dc->MoveTo(parent_x, top.y);
	dc->LineTo(parent_x, top.y + h);
	dc->SelectObject(old_pen);
	dc->SetROP2(old_rop);
	parent->ReleaseDC(dc);
}

void CTopbarDivider::OnLButtonDown(UINT, CPoint point)
{
	m_dragging = true;
	SetCapture();
	m_track_x = clamp_track_x(point);
	draw_track_line(m_track_x);
}

void CTopbarDivider::OnMouseMove(UINT, CPoint point)
{
	if (!m_tracking)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, GetSafeHwnd(), 0 };
		TrackMouseEvent(&tme);
		m_tracking = true;
	}
	if (!m_hot && !m_dragging)
	{
		m_hot = true;
		Invalidate(FALSE);
	}
	if (!m_dragging)
		return;
	// Classic splitter: only the tracking line moves during the drag. The
	// breadcrumb / filter never resize mid-drag (that live-resize was the source
	// of the shimmer / disappearing content). The resize commits once on mouse-up.
	int nx = clamp_track_x(point);
	if (nx != m_track_x)
	{
		draw_track_line(m_track_x);   // erase old (XOR)
		draw_track_line(nx);          // draw new
		m_track_x = nx;
	}
}

void CTopbarDivider::OnMouseLeave()
{
	m_tracking = false;
	if (m_hot && !m_dragging)
	{
		m_hot = false;
		Invalidate(FALSE);
	}
}

void CTopbarDivider::OnLButtonUp(UINT, CPoint)
{
	if (!m_dragging)
		return;
	m_dragging = false;
	ReleaseCapture();
	if (m_track_x >= 0)
		draw_track_line(m_track_x);   // erase the line
	CWnd* parent = GetParent();
	if (parent && m_track_x >= 0)
	{
		CRect pc;
		parent->GetClientRect(pc);
		const int pad = 2, divW = 7;
		// m_track_x is the divider's left edge. The filter's width is the zone on
		// its own side: right side normally, left side when swapped.
		int filter_w;
		if (theme::topbar_swapped())
			filter_w = m_track_x - (pc.left + pad);          // filter is left of the divider
		else
			filter_w = (pc.right - pad) - m_track_x - divW;  // filter is right of the divider
		if (filter_w < 0)
			filter_w = 0;
		parent->SendMessage(WM_TOPBAR_DIVIDER_DRAG, filter_w, 0);  // re-layout once
		parent->SendMessage(WM_TOPBAR_DIVIDER_DONE, 0, 0);          // persist
	}
	m_track_x = -1;
	Invalidate(FALSE);   // drop the active highlight
}
