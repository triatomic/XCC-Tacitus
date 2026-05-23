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
	DECLARE_MESSAGE_MAP()
public:
	bool nav_go_up();
	bool nav_go_forward();
	// Returns the MIX currently open in this pane (NULL when the pane is
	// browsing a filesystem directory). Used for cross-pane lookups, e.g.
	// VXL full-hierarchy auto-load checking the opposite pane's MIX for a
	// sibling `<base>tur.vxl` that's missing from the body's source MIX.
	Cmix_file* current_mix() const { return m_mix_f; }
private:
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
};
