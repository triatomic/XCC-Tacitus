#pragma once

#include <string>
#include <vector>

// Explorer-style breadcrumb bar shown in the top strip (left of the filter
// box). Owner-drawn CWnd child of CMainFrame. Displays the active pane's
// current location as clickable segments separated by chevrons; clicking a
// segment posts WM_BREADCRUMB_CLICK to the parent with the segment index in
// wParam so the frame can navigate the active pane to that level.
//
// Themed via theme:: getters in dark mode, GetSysColor() in light mode, so it
// matches the rest of the Mixer chrome without pulling theme symbols anywhere
// they don't belong (this control lives in the Mixer app, alongside theme.cpp).

// Posted to the parent frame on a segment click. wParam = segment index.
#define WM_BREADCRUMB_CLICK (WM_APP + 0x60)
// Posted when a segment's chevron is clicked, to open an Explorer-style
// dropdown of that level's navigable children. wParam = segment index,
// lParam = MAKELPARAM(screenX, screenY) of the drop point.
#define WM_BREADCRUMB_CHEVRON (WM_APP + 0x63)

class CBreadcrumbBar : public CWnd
{
public:
	CBreadcrumbBar() = default;

	// Create the control as a child of `parent` with the given id. Call once.
	BOOL Create(CWnd* parent, UINT id);

	// Replace the displayed path. Triggers a repaint + hit-rect recompute.
	// No-op (besides repaint) if the segments are unchanged.
	void set_segments(const std::vector<std::string>& segs);

	// Re-apply theme colors (just a repaint — colors are read live in OnPaint).
	void refresh_theme();

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	DECLARE_MESSAGE_MAP()

private:
	struct t_seg
	{
		std::string label;
		CRect label_hit;    // label region (click → navigate to this level)
		CRect chevron_hit;  // trailing chevron region (click → child dropdown)
	};

	// Recompute each segment's hit rects for the current width. Left-elides into
	// a leading "..." segment when the path is too wide to fit.
	void layout_segments();
	// Drawn-segment index whose label is under `pt`, or -1.
	int hit_test_label(CPoint pt) const;
	// Drawn-segment index whose chevron is under `pt`, or -1.
	int hit_test_chevron(CPoint pt) const;
	// Map a drawn-segment index to the real nav_segments() index (account for
	// the leading "..." elision stub). -1 if it's the stub itself.
	int real_index(int drawn) const;

	std::vector<std::string> m_segments;   // full path, root-first
	std::vector<t_seg> m_drawn;            // visible segments with hit rects
	int m_first_visible = 0;               // index of first non-elided segment
	int m_hot = -1;                        // hovered drawn-segment label index, or -1
	int m_hot_chevron = -1;                // hovered drawn-segment chevron index, or -1
	bool m_tracking = false;               // TrackMouseEvent armed
};

// Thin draggable divider between the breadcrumb and the filter box, modeled on
// Explorer's address-bar / search-box gripper. Manages an IDC_SIZEWE cursor and
// mouse capture; while dragging it posts WM_TOPBAR_DIVIDER_DRAG to the parent
// with the proposed new filter-box width (from the right edge) in wParam.
#define WM_TOPBAR_DIVIDER_DRAG (WM_APP + 0x61)   // wParam = proposed filter width
#define WM_TOPBAR_DIVIDER_DONE (WM_APP + 0x62)   // drag finished (persist width)

class CTopbarDivider : public CWnd
{
public:
	CTopbarDivider() = default;
	BOOL Create(CWnd* parent, UINT id);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	// Classic splitter drag: while dragging, the controls stay put and an XOR
	// tracking line follows the cursor over the parent's top strip; the resize
	// applies only on mouse-up. Flicker-free (nothing live-resizes — the stock
	// EDIT can't be cheaply double-buffered without WS_EX_COMPOSITED, which
	// blanked the strip here).
	void draw_track_line(int parent_x);          // XOR a line at parent-client x
	int  clamp_track_x(CPoint client_pt) const;  // mouse -> clamped line x

	bool m_dragging = false;
	bool m_hot = false;        // mouse over the bar (hover highlight)
	bool m_tracking = false;   // TrackMouseEvent armed
	int  m_track_x = -1;       // last drawn tracking-line x (parent client), -1 = none
};
