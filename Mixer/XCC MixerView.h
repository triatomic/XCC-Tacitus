#pragma once

#include <mix_file.h>
#include <stack>
#include "fname.h"
#include "virtual_image.h"
#include "xap.h"
#include "xm_types.h"


class CXCCFileView;

struct t_index_entry
{
	string description;
	t_file_type ft;
	string name;
	string size;
	long long size_bytes = 0;
};

class CXCCMixerView : public CListView
{
protected: // create from serialization only
	CXCCMixerView();
	DECLARE_DYNCREATE(CXCCMixerView)
public:
	Cvirtual_image get_vimage_id(int id) const;
	Cvirtual_image get_vimage(int i) const;
	Cvirtual_binary get_vdata_id(int id) const;
	Cvirtual_binary get_vdata(int i) const;
	bool can_accept() const;
	bool can_copy();
	bool can_copy_as(t_file_type ft);
	bool can_delete();
	bool can_edit() const;
	void clear_list();
	void close_all_locations();
	void close_location(int reload);
	int copy(int i, Cfname fname) const;
	void copy_as(t_file_type ft);
	int copy_as_aud(int i, Cfname fname) const;
	int copy_as_avi(int i, Cfname fname) const;
	int copy_as_cps(int i, Cfname fname) const;
	int copy_as_csv(int i, Cfname fname) const;
	int copy_as_html(int i, Cfname fname) const;
	int copy_as_hva(int i, Cfname fname) const;
	int copy_as_map_ts_preview(int i, Cfname fname) const;
	int copy_as_pal(int i, Cfname fname) const;
	int copy_as_pal_jasc(int i, Cfname fname) const;
	int copy_as_pcx(int i, Cfname fname, t_file_type ft) const;
	int copy_as_shp(int i, Cfname fname) const;
	int copy_as_shp_ts(int i, Cfname fname) const;
	int copy_as_text(int i, Cfname fname) const;
	int copy_as_vxl(int i, Cfname fname) const;
	int copy_as_wav_ima_adpcm(int i, Cfname fname) const;
	int copy_as_wav_pcm(int i, Cfname fname) const;
	int copy_as_xif(int i, Cfname fname) const;
	int get_current_id() const;
	int get_current_index() const;
	int get_paste_fname(string& fname, t_file_type ft, const char* extension, const char* filter);
	void paste_as_image(t_file_type ft, const char* extension, const char* filter);
	const t_palette_entry* get_default_palette() const;
	string get_dir() const;
	void set_reg_key(const string& v);
	int get_id(int i) const;
	int compare(int a, int b) const;
	int open_f_id(Ccc_file& f, int id) const;
	int open_f_index(Ccc_file& f, int i) const;
	// Same as open_f_index but always materializes the entry into an owned
	// Cvirtual_binary first (via get_vdata_id). Use for in-MIX-prone copy_as_*
	// paths so decoders see an entry-bounded buffer instead of streaming from
	// the live mmap'd MIX region.
	int vload_f_index(Ccc_file& f, int i) const;
	int dispatch_copy_as(t_file_type ft, int i, const Cfname& fname);
	void open_location_dir(const string& name);
	void open_location_mix(const string& name);
	void open_location_mix(int id);
	void open_location_mix(t_mix_map_list::const_iterator i, int file_id, const vector<int>& sub_mix_chain = {});
	void open_location_mix(int mix_id, const vector<int>& sub_mix_chain, int file_id);
	void set_other_panes(CXCCFileView* file_view_pane, CXCCMixerView* other_pane);
	void sort_list(int i, bool reverse);
	void update_list();
	// Re-insert listview rows from m_index honoring m_filter_text. The single
	// row-visibility choke point: the anchor row (id 0, ".."/"Browse...") is
	// always kept; every other entry passes only when the filter is empty or
	// fname_filter(name, filter) matches. Callers wrap SetRedraw + sort.
	void insert_filtered_rows();
	// Set this pane's name filter (fname_filter syntax) and rebuild the rows.
	// No-op if unchanged. The filter UI (a single edit above the panes) lives
	// on CMainFrame, which calls this on the active pane as the user types.
	void set_filter(const string& filter);
	const string& filter_text() const { return m_filter_text; }
	// Move the list selection by one row (or page) without giving the list
	// focus — used by the frame's filter edit so arrow keys browse results
	// while the user keeps typing. step: +1 down, -1 up (PgUp/PgDn reuse ±1).
	void move_selection(int step);

	const map<int, t_index_entry>& t_index_list() const
	{
		return m_index;
	}

	const string& current_dir() const { return m_dir; }

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CXCCMixerView)
	public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	protected:
	virtual void OnInitialUpdate(); // called first time after construct
	//}}AFX_VIRTUAL

// Implementation
public:
	int resize(int id);
	void open_item(int id);
	string report() const;
	void autosize_colums();
	BOOL OnIdle(LONG lCount);
	t_game get_game();
	~CXCCMixerView();

protected:
	void extract_open_audio_pak(const string& bag, const string& idx) const;
	void play_audio_id(int id);
	// Lazily create the modeless audio player dialog. Returns a pointer
	// owned by the view's destructor; do not delete.
	class CAudioPlayerDlg* ensure_audio_dlg();
	std::unique_ptr<class CAudioPlayerDlg> m_audio_dlg;

public:
	BOOL PreTranslateMessage(MSG* pMsg) override;
protected:

// Generated message map functions
protected:
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnFileFound(UINT ID);
	//{{AFX_MSG(CXCCMixerView)
	afx_msg void OnFileOpen();
	afx_msg void OnFileClose();
	afx_msg void OnItemchanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnFileNew();
	afx_msg void OnDblclk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	afx_msg void OnColumnclick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnPopupExtract();
	afx_msg void OnUpdatePopupExtract(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopy();
	afx_msg void OnUpdatePopupCopy(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsAUD();
	afx_msg void OnUpdatePopupCopyAsAUD(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsAVI();
	afx_msg void OnUpdatePopupCopyAsAVI(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsCPS();
	afx_msg void OnUpdatePopupCopyAsCPS(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsPCX();
	afx_msg void OnUpdatePopupCopyAsPCX(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsSHP();
	afx_msg void OnUpdatePopupCopyAsSHP(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsWSA();
	afx_msg void OnUpdatePopupCopyAsWSA(CCmdUI* pCmdUI);
	afx_msg void OnPopupDelete();
	afx_msg void OnUpdatePopupDelete(CCmdUI* pCmdUI);
	afx_msg void OnPopupOpen();
	afx_msg void OnUpdatePopupOpen(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsVXL();
	afx_msg void OnUpdatePopupCopyAsVXL(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsXIF();
	afx_msg void OnUpdatePopupCopyAsXIF(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsCSV();
	afx_msg void OnUpdatePopupCopyAsCSV(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsHVA();
	afx_msg void OnUpdatePopupCopyAsHVA(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsPAL();
	afx_msg void OnUpdatePopupCopyAsPAL(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsSHP_TS();
	afx_msg void OnUpdatePopupCopyAsSHP_TS(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsTMP_TS();
	afx_msg void OnUpdatePopupCopyAsTMP_TS(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsPAL_JASC();
	afx_msg void OnUpdatePopupCopyAsPAL_JASC(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsText();
	afx_msg void OnUpdatePopupCopyAsText(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsMapTsPreview();
	afx_msg void OnUpdatePopupCopyAsMapTsPreview(CCmdUI* pCmdUI);
	afx_msg void OnPopupRefresh();
	afx_msg void OnUpdatePopupRefresh(CCmdUI* pCmdUI);
	afx_msg void OnPopupResize();
	afx_msg void OnUpdatePopupResize(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsHTML();
	afx_msg void OnUpdatePopupCopyAsHTML(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsPNG();
	afx_msg void OnUpdatePopupCopyAsPNG(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsWavImaAdpcm();
	afx_msg void OnUpdatePopupCopyAsWavImaAdpcm(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsWavPcm();
	afx_msg void OnUpdatePopupCopyAsWavPcm(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsPcxSingle();
	afx_msg void OnUpdatePopupCopyAsPcxSingle(CCmdUI* pCmdUI);
	afx_msg void OnPopupClipboardCopy();
	afx_msg void OnUpdatePopupClipboardCopy(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsPngSingle();
	afx_msg void OnUpdatePopupCopyAsPngSingle(CCmdUI* pCmdUI);
	afx_msg void OnPopupClipboardPasteAsPcx();
	afx_msg void OnUpdatePopupClipboardPasteAsImage(CCmdUI* pCmdUI);
	afx_msg void OnPopupClipboardPasteAsShpTs();
	afx_msg void OnUpdatePopupClipboardPasteAsVideo(CCmdUI* pCmdUI);
	afx_msg void OnPopupClipboardPasteAsPng();
	afx_msg void OnPopupCopyAsJpeg();
	afx_msg void OnUpdatePopupCopyAsJpeg(CCmdUI* pCmdUI);
	afx_msg void OnPopupClipboardPasteAsJpeg();
	afx_msg void OnPopupExplore();
	afx_msg void OnUpdatePopupExplore(CCmdUI* pCmdUI);
	afx_msg void OnGetdispinfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnPopupCompact();
	afx_msg void OnUpdatePopupCompact(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsTga();
	afx_msg void OnUpdatePopupCopyAsTga(CCmdUI* pCmdUI);
	afx_msg void OnEditSelectAll();
	afx_msg void OnPopupClipboardPasteAsTga();
	afx_msg void OnUpdatePopupClipboardPasteAsTga(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsJpegSingle();
	afx_msg void OnUpdatePopupCopyAsJpegSingle(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyAsTgaSingle();
	afx_msg void OnUpdatePopupCopyAsTgaSingle(CCmdUI* pCmdUI);
	afx_msg void OnPopupCopyName();
	afx_msg void OnUpdatePopupCopyName(CCmdUI* pCmdUI);
	afx_msg void OnPopupBatchExtract();
	afx_msg void OnUpdatePopupBatchExtract(CCmdUI* pCmdUI);
	afx_msg void OnPopupBatchExtractPreserve();
	afx_msg void OnUpdatePopupBatchExtractPreserve(CCmdUI* pCmdUI);
	afx_msg void OnPopupOpenWith();
	afx_msg void OnUpdatePopupOpenWith(CCmdUI* pCmdUI);
	void batch_extract(bool preserve);
	//}}AFX_MSG
	afx_msg void OnXButtonUp(UINT nFlags, UINT nButton, CPoint point);
	afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	// Active-pane accent border (drawn in the non-client area when this pane is
	// the frame's sticky active pane). OnSetFocus marks this pane active.
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
	afx_msg void OnNcPaint();
	// Cross-pane drag & drop. LVN_BEGINDRAG starts a manual image-list drag; on
	// button-up over the OTHER mix pane the snapshotted selection is handed to
	// copy_as(-1) (the same raw-copy path the "Copy" command uses).
	afx_msg void OnBeginDrag(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	// Polls the folder-watch handle (see m_watch_handle) on a low-frequency timer.
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()
public:
	bool nav_go_up();
	bool nav_go_forward();
	// Returns the MIX currently open in this pane (NULL when the pane is
	// browsing a filesystem directory). Used for cross-pane lookups, e.g.
	// VXL full-hierarchy auto-load checking the opposite pane's MIX for a
	// sibling `<base>tur.vxl` that's missing from the body's source MIX.
	Cmix_file* current_mix() const { return m_mix_f; }

	// Re-evaluate the folder watch after the global Auto-refresh setting toggles:
	// arms a watch on the current folder when on, tears it down when off (or for
	// a MIX pane). Called by CMainFrame::OnThemeAutoRefresh for both panes.
	void apply_auto_refresh_setting() { arm_dir_watch(); }

	// Ordered, human-readable segments of this pane's current location, from
	// root (leftmost) to current (rightmost) — drives the frame's breadcrumb.
	// Filesystem: drive + folder components of m_dir. MIX: the root MIX's
	// folder path components, the root MIX filename, then each nested MIX name.
	// Empty only before the first navigation.
	std::vector<std::string> nav_segments() const;
	// Navigate this pane to the location identified by segment `level` (an index
	// into nav_segments()). Pops nested MIX locations / truncates m_dir as
	// needed, then refreshes the list. Routes through the forward-stack so Back
	// still replays the levels that were left. No-op if already at that level.
	void nav_to_segment(int level);

	// One navigable child of a breadcrumb level (a subfolder/archive on a folder
	// level, or a nested archive inside a MIX level). Drives the Explorer-style
	// chevron dropdown. `is_mix_child` distinguishes a nested-MIX entry (open via
	// its in-MIX id once the parent MIX is current) from a filesystem child
	// (open by path). `id` is the in-MIX file id for mix children; unused else.
	struct t_nav_child
	{
		std::string name;
		bool is_mix_child = false;
		int id = 0;
	};
	// Enumerate the navigable children of breadcrumb segment `level` WITHOUT
	// navigating: folder levels are read via FindFirstFile on the joined path;
	// MIX levels read the already-open Cmix_file at that depth. Returns the
	// alphabetically-sorted children (folders/archives only). Empty if the level
	// has none or can't be resolved.
	std::vector<t_nav_child> nav_children(int level) const;
	// Navigate to `level` then descend into the named child (folder or archive).
	// Used when the user picks an entry from the chevron dropdown.
	void nav_descend(int level, const t_nav_child& child);

	// Canonical copyable path string for the current location — the on-disk path
	// (folder when browsing the filesystem; the root MIX's file path when inside
	// a MIX, e.g. "D:\games\ra2.mix"). Drives the editable breadcrumb's text and
	// the "copy path" action.
	std::string nav_current_path() const;
	// Navigate the active pane to a typed/pasted path: an existing folder opens
	// as a directory; an existing archive file (.mix/.big/.dat/.pak/.pkg/.mmx/
	// .yro/.bag) opens as a MIX. Surrounding quotes are tolerated. Returns true
	// if the path resolved and navigation happened; false otherwise (caller can
	// beep / keep the editor open).
	bool nav_open_path(const std::string& path);
private:
	// Resolve the open Cmix_file at breadcrumb MIX-level `level` (or nullptr if
	// the level is a filesystem segment / out of range). Shared by nav_children.
	Cmix_file* mix_at_level(int level) const;
	// Join folder segments [0..level] of nav_segments() into a path with a
	// trailing backslash (filesystem levels only). Empty if level is a MIX.
	std::string folder_path_at_level(int level) const;
	struct t_nav_entry
	{
		enum { kind_dir, kind_disk_mix, kind_nested_mix_id } kind;
		string s;
		int id;
	};
	void nav_record_up(const t_nav_entry& e);
	void nav_clear_forward();
	stack<t_nav_entry> m_nav_forward;
	stack<int> m_entered_ids;
	bool m_nav_replaying = false;

	// Nested-MIX editing. A nested MIX has no file on disk -- it lives as a byte
	// range inside its parent. To make the existing disk-path editors
	// (Cmix_edit/Cbig_edit/Cmix_rg_edit, all of which open(path)) work on a
	// nested MIX, on descent we extract its bytes to a temp file and point the
	// current level's editable path at it. Edits accumulate in the temp; on the
	// way back up (close_location) a dirty temp is re-inserted into its parent
	// under the same entry name -- recursing up the chain since the parent may
	// itself be a temp. One disk write per session at the on-disk root.
	struct t_nested_edit
	{
		string temp_path;   // temp file holding this nested MIX's bytes
		string entry_name;  // name of this MIX's entry inside its parent
		int entry_id;       // id of this MIX's entry inside its parent
		bool dirty;         // an edit op touched temp_path since extract
	};
	// One entry per nested level, parallel to the nested portion of m_location.
	// m_nested_edit.back() describes the currently-open nested MIX (when the
	// current level is nested). Empty when at the disk-root MIX or filesystem.
	vector<t_nested_edit> m_nested_edit;
	// True while the current MIX level's editable file is a temp (i.e. nested).
	bool editing_nested() const { return !m_nested_edit.empty(); }
	// Materialize the current nested level's temp file on demand (lazy extract).
	// Called from edit_release on the first edit; no-op once extracted. Returns
	// true if a usable temp exists (m_mix_fname then points at it), false if not
	// nested or the extract failed.
	bool ensure_nested_temp();
	// Materialize the CURRENT top nested level's temp when it has none, used by
	// nested_flush_top while reducing up the chain (the top level is the parent
	// receiving a child's re-inject). Unlike ensure_nested_temp it does NOT touch
	// m_mix_fname. Covers depth >= 2 where a browse-only intermediate parent never
	// got a temp of its own. Returns true if a usable temp exists afterward.
	bool ensure_parent_temp();
	// Re-insert the top nested temp into its parent if dirty, then delete the
	// temp and pop. Recurses: marks the new parent level dirty so the change
	// propagates up to the on-disk root. Called from close_location.
	void nested_flush_top();
	// Mark the current nested level dirty (called after any edit op). No-op at
	// the disk root.
	void nested_mark_dirty() { if (!m_nested_edit.empty()) m_nested_edit.back().dirty = true; }
	// In-place edit teardown/restore. The edit ops (insert/drop/delete/compact)
	// call edit_release() before opening a disk-path editor on m_mix_fname, then
	// edit_reopen(edited) afterward. At the disk root these mirror the old
	// close_location(false)+open_location_mix(m_mix_fname) pair; at a nested
	// level they keep the temp alive and reopen it (see definitions).
	// Returns false if a nested edit could not be prepared (lazy extract failed);
	// callers must then skip the edit. Always true at the disk root.
	bool edit_release();
	void edit_reopen(bool edited);
	// Parallel to nested levels: m_mix_fname value of each PARENT level, so it
	// can be restored when close_location pops back up.
	vector<string> m_mix_fname_stack;

	// Current name filter for this pane (fname_filter syntax). Consulted by
	// insert_filtered_rows; set via set_filter() from the frame's filter edit;
	// cleared on every navigation (in update_list). Empty = show all.
	string m_filter_text;

	string m_dir;
	map<int, t_index_entry> m_index;
	vector<int> m_index_selected;
	stack<Cmix_file*> m_location;
	Cmix_file* m_mix_f = nullptr;
	string m_mix_fname;
	CXCCFileView* m_file_view_pane;
	CXCCMixerView* m_other_pane;
	t_game m_game;
	t_palette m_palette;
	CString m_reg_key;
	bool m_palette_loaded;
	string m_buffer[4];
	int m_buffer_w;
	int m_sort_column;
	bool m_sort_reverse;
	bool m_reading = false;

	// --- Cross-pane drag & drop ---------------------------------------------
	// Drag selected entries from this pane and drop them on the OTHER mix pane.
	// On drop the existing raw-copy path (copy_as(-1)) runs, so a folder source
	// extracts to the destination folder and a MIX destination inserts the
	// bytes -- all four source/dest combinations reuse copy_as. Implemented with
	// a manual CImageList drag (locked to the desktop so the ghost crosses the
	// splitter) rather than OLE: no temp files, no CF_HDROP marshalling.
	bool m_dragging = false;
	CImageList* m_drag_image = nullptr;
	vector<int> m_drag_sel;          // listview indices snapshotted at drag start
	// Which mix pane (this or m_other_pane) sits under a screen point, or null.
	CXCCMixerView* pane_under_point(CPoint screen) const;
	// Tear down the drag visual (image list + m_dragging). Does NOT perform a drop.
	void cancel_drag_visual();

	// --- Auto-refresh (folder views) ----------------------------------------
	// Folder panes watch their directory and rebuild the list when files appear,
	// vanish, are renamed or resized on disk -- no manual Refresh needed. Uses a
	// FindFirstChangeNotification handle polled from a low-frequency timer (no
	// worker thread); a one-tick debounce coalesces bursts (e.g. a large copy or
	// an archive extraction). MIX panes are NOT watched: the archive is held in
	// memory and in-Mixer edits already refresh the view, while reloading an open
	// MIX from disk could fight pending edits.
	HANDLE m_watch_handle = INVALID_HANDLE_VALUE;
	string m_watch_dir;            // directory m_watch_handle currently watches
	bool m_watch_pending = false;  // a change was seen; refresh on next quiet tick
	bool m_watch_timer = false;    // the polling timer is running
	// While true, OnItemchanged skips opening the preview -- set around the
	// programmatic re-selection in refresh_preserving so a background folder's
	// auto-refresh doesn't steal the preview pane from the active pane.
	bool m_suppress_sel_preview = false;
	// Point the watch at the current location: folder => (re)arm on m_dir, MIX or
	// pre-navigation empty state => stop. Idempotent for the same folder.
	void arm_dir_watch();
	void stop_dir_watch();
	// Fill m_index from the on-disk m_dir (the folder branch of update_list,
	// factored out so the auto-refresh can re-read without resetting the filter).
	void read_dir_into_index();
	// Rebuild a folder list in place, preserving selection + focus (by id), the
	// active filter, the sort order and the scroll position.
	void refresh_preserving();
};
