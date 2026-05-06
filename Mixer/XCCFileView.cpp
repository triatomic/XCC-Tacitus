#include "stdafx.h"
#include "MainFrm.h"
#include "XCCFileView.h"
#include <csf_file.h>
#include <aud_file.h>
#include <big_file.h>
#include <cmath>
#include <cps_file.h>
#include <dds_file.h>
#include <fname.h>
#include <fnt_file.h>
#include <fstream>
#include <hva_file.h>
#include <id_log.h>
#include <map_ra_ini_reader.h>
#include <map_td_ini_reader.h>
#include <map_ts_ini_reader.h>
#include <mp3_file.h>
#include <pak_file.h>
#include <pal_file.h>
#include <pcx_decode.h>
#include <pcx_file.h>
#include <pkt_ts_ini_reader.h>
#include <shp_decode.h>
#include <shp_dune2_file.h>
#include <shp_file.h>
#include <shp_images.h>
#include <shp_ts_file.h>
#include <sstream>
#include <st_file.h>
#include <string_conversion.h>
#include <tga_file.h>
#include <theme_ts_ini_reader.h>
#include <tmp_file.h>
#include <tmp_ra_file.h>
#include <tmp_ts_file.h>
#include <virtual_tfile.h>
#include <voc_file.h>
#include <vqa_file.h>
#include <vxl_file.h>
#include <wav_file.h>
#include <wsa_dune2_file.h>
#include <wsa_file.h>
#include <png_file.h>
#include <numbers>
#include "theme.h"

IMPLEMENT_DYNCREATE(CXCCFileView, CListView)

CXCCFileView::CXCCFileView()
{
}

CXCCFileView::~CXCCFileView()
{
}

BEGIN_MESSAGE_MAP(CXCCFileView, CScrollView)
	ON_UPDATE_COMMAND_UI(ID_FILE_NEW, OnDisable)
	ON_UPDATE_COMMAND_UI(ID_FILE_OPEN, OnDisable)
	ON_UPDATE_COMMAND_UI(ID_FILE_CLOSE, OnDisable)
	ON_WM_MOUSEWHEEL()
	ON_WM_MOUSEHWHEEL()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CXCCFileView::OnEraseBkgnd(CDC* pDC)
{
	GetClientRect(clientRect);
	if (theme::is_dark())
		pDC->FillSolidRect(clientRect, theme::bg());
	else
		pDC->FillRect(clientRect, &test_brush);
	return TRUE;
}

BOOL CXCCFileView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CPoint position = GetScrollPosition();
	SHORT shiftState = GetAsyncKeyState(VK_SHIFT);

	if (shiftState)
	{
		ScrollToPosition(CPoint(position.x - zDelta, position.y));
		return false;
	}
	ScrollToPosition(CPoint(position.x, position.y - zDelta));
	return false;
}

void CXCCFileView::OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CPoint position = GetScrollPosition();
	SHORT shiftState = GetAsyncKeyState(VK_SHIFT);

	if (shiftState)
	{
		ScrollToPosition(CPoint(position.x, position.y + zDelta));
		return;
	}
	ScrollToPosition(CPoint(position.x + zDelta, position.y));
	return;
}

void CXCCFileView::OnInitialUpdate()
{
	CScrollView::OnInitialUpdate();
	test_brush.CreateSolidBrush(m_colour);
	//m_font.CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
	m_font.CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Lucida Console");
	//m_font.CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Consolas");
	//m_font.CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, ""); //default font, but if it isn't monospace it sucks
}

void CXCCFileView::draw_image8(const byte* s, int cx_s, int cy_s, CDC* pDC, int x_d)
{
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{	
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
			mp_dib[x + cx_s * y] = m_color_table[s[x + cx_s * y]];
	}
	pDC->BitBlt(x_d, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, x_d + cx_s);
}

void CXCCFileView::draw_image24(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{	
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = *s++;
			v.g = *s++;
			v.b = *s++;
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	pDC->BitBlt(offset, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_s);
}

void CXCCFileView::draw_image32(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	const byte* r = s;
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = *s++;
			v.g = *s++;
			v.b = *s++;
			v.a = *s++;
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	pDC->BitBlt(offset, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_s);
}

void CXCCFileView::draw_image48(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	auto r = reinterpret_cast<const unsigned short*>(s);
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = linear2sRGB(*r++);
			v.g = linear2sRGB(*r++);
			v.b = linear2sRGB(*r++);
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	pDC->BitBlt(offset, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_s);
}

void CXCCFileView::draw_image64(const byte* s, int cx_s, int cy_s, CDC* pDC)
{
	CDC mem_dc;
	mem_dc.CreateCompatibleDC(pDC);
	void* old_bitmap;
	{
		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(BITMAPINFO));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = cx_s;
		bmi.bmiHeader.biHeight = -cy_s;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * -bmi.bmiHeader.biHeight * (bmi.bmiHeader.biBitCount >> 3);
		mh_dib = CreateDIBSection(*pDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&mp_dib), 0, 0);
	}
	old_bitmap = mem_dc.SelectObject(mh_dib);
	auto r = reinterpret_cast<const unsigned short*>(s);
	t_palette32bgr_entry v{};
	for (unsigned int y = 0; y < cy_s; y++)
	{
		for (unsigned int x = 0; x < cx_s; x++)
		{
			v.r = linear2sRGB(*r++);
			v.g = linear2sRGB(*r++);
			v.b = linear2sRGB(*r++);
			v.a = linear2sRGB(*r++);
			mp_dib[x + cx_s * y] = v.v;
		}
	}
	pDC->BitBlt(offset, m_y, cx_s, cy_s, &mem_dc, 0, 0, SRCCOPY);
	mem_dc.SelectObject(old_bitmap);
	DeleteObject(mh_dib);
	m_x = max(m_x, offset + cx_s);
}

static CMainFrame* GetMainFrame()
{
	return reinterpret_cast<CMainFrame*>(AfxGetMainWnd());
}

const t_palette_entry* CXCCFileView::get_default_palette()
{
	const t_palette_entry* p = GetMainFrame()->get_pal_data();
	if (p)
		return p;
	if (m_palette)
		return m_palette;
	return GetMainFrame()->get_game_palette(m_game);
}

void CXCCFileView::load_color_table(const t_palette palette, bool convert_palette)
{
	t_palette p;
	if (!palette)
	{
		convert_palette = true;
		palette = get_default_palette();
	}
	memcpy(p, palette, sizeof(t_palette));
	if (convert_palette)
		convert_palette_18_to_24(p);
	t_palette32bgr_entry* color_table = reinterpret_cast<t_palette32bgr_entry*>(m_color_table);
	for (unsigned short i = 0; i < 256; i++)
	{
		color_table[i].r = p[i].r;
		color_table[i].g = p[i].g;
		color_table[i].b = p[i].b;
	}
}

static string t2s(const string& v)
{
	string r;
	for (int i = 0; i < v.length(); i++)
	{
		char c = v[i];
		if (c == '\t')
		{
			do
				r += ' ';
			while (r.length() & 3);
		}
		else
			r += c;
	}
	return r;
}

void CXCCFileView::draw_info(string n, string d)
{
	if (!m_text_cache_valid)
	{
		n = t2s(n);
		d = t2s(d);
		t_text_cache_entry e;

		CSize size;
		size.SetSize(1, 1);

		e.text_extent = CRect(CPoint(offset, m_y + offset), size);
		e.t = n;
		m_text_cache.push_back(e);
		if (!d.empty())
		{
			e.text_extent = CRect(CPoint(offset * 32, m_y + offset), size);
			e.t = d;
			m_text_cache.push_back(e);
			m_x = max<int>(m_x, (offset * 32) + e.text_extent.right);
		}
		else
			m_x = max<int>(m_x, e.text_extent.right);
	}
	m_y += m_y_inc;
}


void CXCCFileView::OnDisable(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(false);
}

void fnt_decode(const byte* r, byte* w, int cx, int cy)
{
	while (cy--)
	{
		for (int x = 1; x < cx; x += 2)
		{
			*w++ = *r & 0xf;
			*w++ = *r++ >> 4;
		}
		if (cx & 1)
			*w++ = *r++ & 0xf;
	}
}

void fnt_adjust(byte* d, int size)
{
	while (size--)
	{
		byte& v = *d++;
		v = v ? 0 : 0xff;
	}
}

struct t_vector
{
	double x;
	double y;
	double z;
};

t_vector rotate_x(t_vector v, double a)
{
	double l = sqrt(v.y * v.y + v.z * v.z);
	double d_a = atan2(v.y, v.z) + a;
	t_vector r;
	r.x = v.x;
	r.y = l * sin(d_a);
	r.z = l * cos(d_a);
	return r;
}

t_vector rotate_y(t_vector v, double a)
{
	double l = sqrt(v.x * v.x + v.z * v.z);
	double d_a = atan2(v.x, v.z) + a;
	t_vector r;
	r.x = l * sin(d_a);
	r.y = v.y;
	r.z = l * cos(d_a);
	return r;
}

const char* dump_four_cc(DWORD four_cc)
{
	static char r[5];
	r[0] = four_cc & 0xff;
	r[1] = four_cc >> 8 & 0xff;
	r[2] = four_cc >> 16 & 0xff;
	r[3] = four_cc >> 24 & 0xff;
	r[4] = 0;
	return r;
}

int get_size(unsigned int v)
{
	int r = 0;
	while (v)
	{
		if (v & 1)
			r++;
		v >>= 1;
	}
	return r;
}

void CXCCFileView::OnDraw(CDC* pDC)
{
	//pDC->SetTextColor(RGB(249, 245, 215));

	const char* b2a[] = {"no", "yes"};
	pDC->SelectObject(&m_font);

	if (theme::is_dark())
	{
		pDC->SetTextColor(theme::text());
		pDC->SetBkColor(theme::bg());
		pDC->SetBkMode(TRANSPARENT);
	}

	//pDC->SetBkColor(m_colour);

	if (m_is_open)
	{
		TEXTMETRIC tm;
		pDC->GetTextMetrics(&tm);
		m_dc = pDC;
		m_x = 0;
		m_y = 0;
		m_y_inc = tm.tmHeight;

		draw_info("ID:", nh(8, m_id));
		draw_info("Size:", n(m_size));
		draw_info("Type:", ft_name[m_ft]);
		bool show_binary = false;
		switch (m_ft)
		{
		case ft_aud:
			{
				Caud_file f;
				f.load(m_data);
				draw_info("Audio:", n(f.get_samplerate()) + " hz, " + n(f.get_cb_sample() << 3) + " bit, " + (f.get_c_channels() == 1 ? "Mono" : "Stereo"));
				draw_info("Samples:", n(f.get_c_samples()));
				draw_info("Compression:", nh(2, f.header().compression));
				break;
			}
		case ft_big:
			{
				Cbig_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				draw_info("Files:", n(c_files));
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					string name = f.get_name(i);
					draw_info(nwzl(4, i) + " - " + nwsl(11, f.get_size(name)) + ' ' + name, "");
				}
				break;
			}
		case ft_csf:
		{
			Ccsf_file_rd f;
			f.load(m_data, m_size);
			const int c_strs = f.header().count1;
			auto& c_strmaps = f.get_map();
			draw_info("Strings:", n(c_strs));
			m_y += m_y_inc;
			draw_info("Name", "Value\t\tExtra Value");
			for (auto i : c_strmaps)
			{
				draw_info(i.first, Ccsf_file::convert2string(i.second.value) + "\t\t" + i.second.extra_value);
			}
			break;
		}
		case ft_cps:
			{
				Ccps_file f;
				f.load(m_data);
				draw_info("Paletted:", f.palette() ? "Yes" : "No");
				m_y += m_y_inc;
				load_color_table(f.palette(), true);
				Cvirtual_image image = f.vimage();
				draw_image8(image.image(), image.cx(), image.cy(), pDC, offset);
				m_y += 200 + m_y_inc;
				break;
			}
		case ft_dds:
			{
				Cdds_file f;
				f.load(m_data);
				const DDSURFACEDESC2& ddsd = f.ddsd();
				if (ddsd.dwFlags & (DDSD_WIDTH | DDSD_HEIGHT) == DDSD_WIDTH | DDSD_HEIGHT)
					draw_info("Size: ",  n(ddsd.dwWidth) + " x " + n(ddsd.dwHeight));
				if (ddsd.dwFlags & DDSD_PITCH)
					draw_info("Pitch: ", n(ddsd.lPitch));
				if (ddsd.dwFlags & DDSD_LINEARSIZE)
					draw_info("Linear size: ", n(ddsd.dwLinearSize));
				if (ddsd.dwFlags & DDSD_DEPTH)
					draw_info("Depth: ", n(ddsd.dwDepth));
				if (ddsd.dwFlags & DDSD_MIPMAPCOUNT)
					draw_info("Mipmaps: ", n(ddsd.dwMipMapCount));
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
					draw_info("Pixel Format: ", dump_four_cc(ddsd.ddpfPixelFormat.dwFourCC));
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_RGB)
					draw_info("Pixel Format: ", n(ddsd.ddpfPixelFormat.dwRGBBitCount) +
						" bits (" + nwzl(4, 1000 * get_size(ddsd.ddpfPixelFormat.dwRGBAlphaBitMask) + 100 * get_size(ddsd.ddpfPixelFormat.dwRBitMask) + 10
							* get_size(ddsd.ddpfPixelFormat.dwGBitMask) + get_size(ddsd.ddpfPixelFormat.dwBBitMask)) + ')');		
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
				{					
					Cvirtual_image image = f.vimage();
					if (image.image())
					{
						image.remove_alpha();
						m_y += m_y_inc;
						draw_image24(image.image(), f.cx(), f.cy(), pDC);
						m_y += f.cy() + m_y_inc;
					}
				}
				if (ddsd.ddpfPixelFormat.dwFlags & DDPF_RGB && ddsd.ddpfPixelFormat.dwRGBBitCount == 24)
				{
					m_y += m_y_inc;
					draw_image24(f.image(), f.cx(), f.cy(), pDC);
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		case ft_fnt:
			{
				Cfnt_file f;
				f.load(m_data);
				const int c_chars = f.get_c_chars();
				const int cy_test = f.get_cy();
				const t_fnt_header& header = f.header();
				draw_info("Characters:", n(c_chars));
				draw_info("Size:", n(f.get_cmax_x()) + " x " + n(cy_test));
				m_y += m_y_inc;
				byte* d = new byte[f.get_cmax_x() * f.get_cy()];
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < c_chars; i++)
				{
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					if (!cx || !cy)
						continue;
					fnt_decode(f.get_image(i), d, cx, cy);
					fnt_adjust(d, cx * cy);
					draw_image8(d, cx, cy, pDC, offset);
					draw_info("", nwzl(3, i) + " - " + n(cx) + " x " + n(cy) + " at " + n(f.get_image(i) - f.get_data()));
					m_y += cy;	//add y height to make any character always be visible no matter text height
				}
				delete[] d;
				break;
			}
		case ft_hva:
			{
				Chva_file f;
				f.load(m_data, m_size);
				draw_info("Frames:", n(f.get_c_frames()));
				draw_info("Sections:", n(f.get_c_sections()));
				break;
			}
		case ft_jpeg:
		case ft_png:
			{
				Cvirtual_image image;
				if (!image.load(m_data))
				{
					const int cx = image.cx();
					const int cy = image.cy();
					draw_info("Bits/Pixel:", n(8 * image.cb_pixel()));
					draw_info("Size:", n(cx) + " x " + n(cy));
					m_y += m_y_inc;
					switch (image.cb_pixel())
					{
					case 1:
						load_color_table(image.palette(), false);
						draw_image8(image.image(), cx, cy, pDC, offset);
						break;
					case 3:
						draw_image24(image.image(), cx, cy, pDC);
						break;
					case 4:
						draw_image32(image.image(), cx, cy, pDC);
						break;
					case 6:
						draw_image48(image.image(), cx, cy, pDC);
						break;
					case 8:
						draw_image64(image.image(), cx, cy, pDC);
						break;
					default:
						break;
					}
					m_y += cy + m_y_inc;
				}
				break;
			}
		case ft_map_td:
			{
				Cvirtual_tfile tf;
				tf.load_data(m_data);
				Cmap_td_ini_reader ir;
				while (!tf.eof())
				{
					ir.process_line(tf.read_line());
				}
				const Cmap_td_ini_reader::t_map_data& md = ir.get_map_data();
				draw_info("Name:", ir.get_basic_data().name);
				draw_info("Size:", n(md.cx) + " x " + n(md.cy));
				draw_info("Theater:", ir.get_map_data().theater);
				m_y += m_y_inc;
				tf.load_data(m_data);
				while (!tf.eof())
					draw_info(tf.read_line(), "");
				break;
			}
		case ft_map_ra:
			{
				Cvirtual_tfile tf;
				tf.load_data(m_data);
				Cmap_ra_ini_reader ir;
				while (!tf.eof())
				{
					ir.process_line(tf.read_line());
				}
				const Cmap_ra_ini_reader::t_map_data& md = ir.get_map_data();
				draw_info("Name:", ir.get_basic_data().name);
				draw_info("Size:", n(md.cx) + " x " + n(md.cy));
				draw_info("Theater:", ir.get_map_data().theater);
				m_y += m_y_inc;
				tf.load_data(m_data);
				while (!tf.eof())
					draw_info(tf.read_line(), "");
				break;
			}
		case ft_map_ts:
			{
				Cvirtual_tfile tf;
				tf.load_data(m_data);
				Cmap_ts_ini_reader ir;
				while (!tf.eof())
				{
					ir.process_line(tf.read_line());
				}
				const Cmap_ts_ini_reader::t_map_data& md = ir.get_map_data();
				const Cmap_ts_ini_reader::t_preview_data& pd = ir.get_preview_data();
				const string& ppd = ir.get_preview_pack_data();
				draw_info("Name:", ir.get_basic_data().name);
				draw_info("Size:", n(md.size_right) + " x " + n(md.size_bottom));
				draw_info("Theater:", ir.get_map_data().theater);
				draw_info("Max Players:", n(ir.max_players()));

				if (pd.cx && pd.cy && ppd != "BIACcgAEwBtAMnRABAAaQCSANMAVQASAAnIABMAbQDJ0QAQAGkAkgDTAFUAEgAJyAATAG0yAsAIAXQ5PDQ5PDQ6JQATAEE6PDQ4PDI4JgBTAFEAkgAJyAATAG0AydEAEABpAJIA0wBVA")
				{
					m_y += m_y_inc;
					Cvirtual_binary s = decode64(ppd);
					Cvirtual_binary image;

					if ((pd.cx * pd.cy) / 33 > ppd.size())	//test to not try to render weirdly small (corrupted) preview images
					{
						m_y += m_y_inc;
						tf.load_data(m_data);
						while (!tf.eof())
							draw_info(tf.read_line(), "");
						break;
					}
					decode5(s.data(), image.write_start(pd.cx * pd.cy * 3), s.size(), 5);
					draw_image24(image.data(), pd.cx, pd.cy, pDC);
					m_y += m_y_inc + pd.cy;
					tf.load_data(m_data);
					while (!tf.eof())
						draw_info(tf.read_line(), "");
				}
				else
				{
					m_y += m_y_inc;
					tf.load_data(m_data);
					while (!tf.eof())
						draw_info(tf.read_line(), "");
				}
				break;
			}
		case ft_mix:
			{
				Cmix_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				const t_game game = f.get_game();
				draw_info("Files:", n(c_files));
				draw_info("Checksum:", f.has_checksum() ? "Yes" : "No");
				draw_info("Encrypted:", f.is_encrypted() ? "Yes" : "No");
				draw_info("Game:", game_name[game]);
				if (game > game_td)
				{
					draw_info("Raw Flags:", nh(8, f.rawflags()));
				}
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					int id = f.get_id(i);
					draw_info(nwzl(4, i) + " - " + nh(8, id) + nwsl(11, f.get_size(id)) + ' ' + mix_database::get_name(game, id), "");
				}
				break;
			}
		case ft_pak:
			{
				Cpak_file f;
				f.load(m_data, m_size);
				const int c_files = f.get_c_files();
				draw_info("Files:", n(c_files));
				m_y += m_y_inc;
				for (int i = 0; i < c_files; i++)
				{
					draw_info(nwzl(4, i) + " - " + nwsl(11, f.get_size(f.get_name(i))) + ' ' + f.get_name(i), "");
				}
				break;
			}
		case ft_mp3:
			{
				Cmp3_file f;
				f.load(m_data, m_size);
				const Cmp3_frame_header& header = f.header();
				draw_info("Bitrate:", n(header.bitrate()));
				draw_info("Channel Mode:", mpcm_name[header.channel_mode()]);
				draw_info("Copyright:", header.copyright() ? "Yes" : "No");
				draw_info("CRC:", header.crc() ? "Yes" : "no");
				draw_info("Emphasis:", n(header.emphasis()));
				draw_info("Layer:", n(header.layer()));
				draw_info("Mode Extension:", n(header.mode_extension()));
				draw_info("Original:", header.original() ? "Yes" : "No");
				draw_info("Padding:", header.padding() ? "Yes" : "No");
				draw_info("Sample Rate:", n(header.samplerate()));
				draw_info("Version:", mpv_name[header.version()]);
				break;
			}
		case ft_pal:
			{
				Cpal_file f;
				f.load(m_data);
				int y = m_y;
				const t_palette_entry* palette = f.get_palette();
				for (int i = 0; i < 256; i++)
				{
					CBrush box;
					CBrush brush;
					box.CreateSolidBrush(RGB(0, 0, 0));
					brush.CreateSolidBrush(RGB(palette[i].r * 255 / 63, palette[i].g * 255 / 63, palette[i].b * 255 / 63));
					y += m_y_inc;
					pDC->FillRect(CRect(CPoint(99, y + 4), CSize(26, m_y_inc * 2 / 3 + 2)), &box);
					pDC->FillRect(CRect(CPoint(100, y + 5), CSize(24, m_y_inc * 2 / 3)), &brush);
				}
				break;
			}
		case ft_pcx:
			{
				Cpcx_file f;
				f.load(m_data);
				const int c_planes = f.cb_pixel();
				const int cx = f.cx();
				const int cy = f.cy();
				draw_info("Bits/Pixel:", n(8 * c_planes));
				draw_info("Size:", n(cx) + " x " + n(cy));
				m_y += m_y_inc;
				Cvirtual_binary image;
				f.decode(image.write_start(c_planes * cx * cy));
				if (c_planes == 1)
				{
					load_color_table(*f.get_palette(), false);
					draw_image8(image.data(), cx, cy, pDC, offset);
				}
				else
					draw_image24(image.data(), cx, cy, pDC);
				m_y += cy + m_y_inc;
				break;
			}
		case ft_shp_dune2:
			{
				Cshp_dune2_file f;
				f.load(m_data);
				const int c_images = f.get_c_images();
				draw_info("Images:", n(c_images));
				draw_info("Offset Size:", n(f.get_cb_ofs()));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < c_images; i++)
				{
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					byte* image = new byte[cx * cy];
					if (f.is_compressed(i))
					{
						byte* d = new byte[f.get_image_header(i)->size_out];
						decode2(d, image, LCWDecompress(f.get_image(i), d), f.get_reference_palette(i));
						delete[] d;
					}
					else
						decode2(f.get_image(i), image, f.get_image_header(i)->size_out, f.get_reference_palette(i));
					draw_image8(image, cx, cy, pDC, offset);
					delete[] image;
					m_y += cy + m_y_inc;
				}
				break;
			}
		case ft_shp:
			{
				Cshp_file f;
				f.load(m_data);
				draw_info("Frames:", n(f.cf()));
				draw_info("Size:", n(f.cx()) + " x " + n(f.cy()));
				draw_info("X:", n(f.header().xpos));
				draw_info("Y:", n(f.header().ypos));
				draw_info("Delta Size:", n(f.header().delta));
				draw_info("Flags:", n(f.header().flags));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				Cvirtual_image image = f.vimage();
				const byte* r = image.image();
				for (int i = 0; i < f.cf(); i++)
				{
					draw_image8(r, f.cx(), f.cy(), pDC, offset);
					r += f.cb_image();
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		case ft_shp_ts:
			{
				Cshp_ts_file f;
				f.load(m_data);
				const int c_images = f.cf();
				const int cx = m_cx = f.cx();
				const int cy = m_cy = f.cy();
				const int zero = f.zero();
				draw_info("Images:", n(c_images));
				draw_info("Size:", n(cx) + " x " + n(cy));
				draw_info("Unknown:", nh(8, zero));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < c_images; i++)
				{
#ifndef NDEBUG	//this doesn't display right and i don't know if it's useful information at all
					draw_info("Radar Color:", "R:" + nwzl(3, f.get_image_header(i)->red) + " G:" + nwzl(3, f.get_image_header(i)->green) + " B:" + nwzl(3, f.get_image_header(i)->blue) + " A:" + nwzl(3, f.get_image_header(i)->alpha));
					CBrush box;
					CBrush color;
					box.CreateSolidBrush(RGB(0, 0, 0));
					color.CreateSolidBrush(RGB(f.get_image_header(i)->red, f.get_image_header(i)->green, f.get_image_header(i)->blue));
					//Draw box that will fill the background edges, needed for light colors
					pDC->FillRect(CRect(CPoint(94, m_y - 12), CSize(26, m_y_inc * 2 / 3 + 2)), &box);
					//Draw the actual color
					pDC->FillRect(CRect(CPoint(95, m_y - 11), CSize(24, m_y_inc * 2 / 3)), &color);
					draw_info("Frame Flags:", nh(8, f.get_image_header(i)->flags));
					draw_info("Unknown:", nh(8, f.get_image_header(i)->zero));
#endif
					const int cx = f.get_cx(i);
					const int cy = f.get_cy(i);
					if (cx && cy)
					{
						if (f.is_compressed(i))
						{
							Cvirtual_binary image;
							RLEZeroTSDecompress(f.get_image(i), image.write_start(cx * cy), cx, cy);
							draw_image8(image.data(), cx, cy, pDC, offset);
						}
						else
							draw_image8(f.get_image(i), cx, cy, pDC, offset);
						m_y += cy + m_y_inc;
					}
				}
				break;
			}
		case ft_tga:
			{
				Ctga_file f;
				f.load(m_data);
				Cvirtual_image image;
				if (!f.decode(image))
				{
					const int cx = image.cx();
					const int cy = image.cy();
					draw_info("Bits/Pixel:", n(8 * image.cb_pixel()));
					draw_info("Size:", n(cx) + " x " + n(cy));
					m_y += m_y_inc;
					if (image.cb_pixel() == 1)
					{
						load_color_table(image.palette(), false);
						draw_image8(image.image(), cx, cy, pDC, offset);
					}
					else if (image.cb_pixel() == 3)
						draw_image24(image.image(), cx, cy, pDC);
					m_y += cy + m_y_inc;
				}
				break;
			}
		case ft_tmp:
			{
				Ctmp_file f;
				f.load(m_data);
				const int c_tiles = f.get_c_tiles();
				draw_info("Icons:", n(c_tiles));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < c_tiles; i++)
				{
					if (f.get_index1()[i] != 0xff)
					{
						draw_image8(f.get_image(i), 24, 24, pDC, offset);
						m_y += 24 + m_y_inc;
					}
				}
				break;
			}
		case ft_tmp_ra:
			{
				Ctmp_ra_file f;
				f.load(m_data);
				const int c_tiles = f.get_c_tiles();
				static const int size = 24;
				int sx = f.cx();
				int sy = f.cy();
				int cx = f.get_cblocks_x();
				int cy = f.get_cblocks_y();
				if (cx == -1 && cy == -1)
				{
					cx = 1;
					cy = c_tiles;
				}
				draw_info("Icons:", n(c_tiles));
				draw_info("Size:", n(cx) + " x " + n(cy));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				if (cx == 1 && cy == 1)
				{
					for (int i = 0; i < c_tiles; i++)
					{
						if (f.get_index1()[i] != 0xff)
						{
							draw_image8(f.get_image(i), size, size, pDC, offset);
							m_y += size + m_y_inc;
						}
					}
				}
				else
				{
					int i = 0;
					for (int y = 0; y < cy; y++)
					{
						for (int x = 0; x < cx; x++)
						{
							if (f.get_index1()[i] != 0xff)
							{
								draw_image8(f.get_image(i), size, size, pDC, (size * x) + offset);
							}
							i++;
						}
						m_y += size;
					}
					m_y += m_y_inc;
				}
				break;
			}
		case ft_tmp_ts:
			{
				Ctmp_ts_file f;
				f.load(m_data);
				const int c_tiles = f.get_c_tiles();
				m_cx = f.get_cx();
				m_cy = f.get_cy();
				draw_info("Tiles:", n(c_tiles));
				draw_info("Size:", n(f.get_cblocks_x()) + " x " + n(f.get_cblocks_y()));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				int x, y, cx, cy;
				f.get_rect(x, y, cx, cy);
				byte* d = new byte[cx * cy];
				f.draw(d);
				draw_image8(d, cx, cy, pDC, 4);
				m_y += cy + m_y_inc;
#ifndef NDEBUG
				for (int i = 0; i < f.get_c_tiles(); i++)
				{
					if (!f.get_index()[i])
						continue;
					const t_tmp_image_header& header = *f.get_image_header(i);
					/*
					draw_info("Tile:", n(i));
					draw_info("x:", n(header.x));
					draw_info("y:", n(header.y));
					for (int j = 0; j < 3; j++)
						draw_info("unknown1[" + n(j) +"]:", n(header.unknown1[j]));
					draw_info("x_extra:", n(header.x_extra));
					draw_info("y_extra:", n(header.y_extra));
					draw_info("cx_extra:", n(header.cx_extra));
					draw_info("cy_extra:", n(header.cy_extra));
					*/
					/*
					draw_info("flags:", n(header.flags & 7));
					draw_info("height:", n(header.height));
					draw_info("terrain type:", n(header.terraintype));
					draw_info("direction:", n(header.direction));
					// draw_info("unknown2[0]:", nh(6, header.unknown2[0] & 0xffffff));
					/*
					for (j = 0; j < 3; j++)
						draw_info("unknown2[" + n(j) +"]:", nh(8, header.unknown2[j]));
					if (f.has_extra_graphics(i))
					{
						draw_image8(f.get_image(i) + 2 * 576, header.cx_extra, header.cy_extra, pDC, 0, m_y);
						m_y += header.cy_extra;
					}
					*/
					m_y += m_y_inc;
				}
#endif
				delete[] d;
				break;
			}
		case ft_voc:
			{
				Cvoc_file f;
				f.load(m_data);
				draw_info("Audio:", n(f.get_samplerate()) + " hz, 8 bit, Mono");
				draw_info("Samples:", n(f.get_c_samples()));
				break;
			}
		case ft_vqa:
			{
				Cvqa_file f;
				f.load(m_data);
				draw_info("Version:", n(f.header().version));
				draw_info("Video Flags:", nh(4, f.header().video_flags));
				draw_info("Frames:", n(f.get_c_frames()));
				draw_info("Size:", n(f.get_cx()) + " x " + n(f.get_cy()));
				draw_info("Block Size:", n(f.get_cx_block()) + " x " + n(f.get_cy_block()));
				draw_info("Audio:", n(f.get_samplerate()) + " hz, " + n(f.get_cbits_sample()) + " bit, " + (f.get_c_channels() == 1 ? "Mono" : "Stereo"));
				break;
			}
		case ft_vxl:
			{
				m_y += m_y_inc;
				Cvxl_file f;
				f.load(m_data);
				int vxl_mode = GetMainFrame()->get_vxl_mode();
				load_color_table(get_default_palette(), true);
				for (int i = 0; i < f.get_c_section_headers(); i++)
				{
					const t_vxl_section_tailer& section_tailer = *f.get_section_tailer(i);
					const int cx = section_tailer.cx;
					const int cy = section_tailer.cy;
					const int cz = section_tailer.cz;
					const int l = ceil(sqrt((cx * cx + cy * cy + cz * cz) / 4.0));
					const int cl = 2 * l;
					const double center_x = cx / 2;
					const double center_y = cy / 2;
					const double center_z = cz / 2;
					const int c_pixels = cl * cl;
					draw_info("Section " + n(i) + ':', n(cx) + " x " + n(cy) + " x " + n(cz));
					char fb[32];
					for (int ty = 0; ty < 3; ty++)
					{
						string s;
						for (int tx = 0; tx < 4; tx++)
						{
							s += _gcvt(section_tailer.transform[ty][tx], 10, fb);
							s += ' ';
						}
						draw_info(n(ty), s);
					}
					draw_info("Scale:", _gcvt(section_tailer.scale, 10, fb));
					draw_info("X min:", _gcvt(section_tailer.x_min_scale, 10, fb));
					draw_info("Y min:", _gcvt(section_tailer.y_min_scale, 10, fb));
					draw_info("Z min:", _gcvt(section_tailer.z_min_scale, 10, fb));
					draw_info("X max:", _gcvt(section_tailer.x_max_scale, 10, fb));
					draw_info("Y max:", _gcvt(section_tailer.y_max_scale, 10, fb));
					draw_info("Z max:", _gcvt(section_tailer.z_max_scale, 10, fb));
					draw_info("Normal Type:", n(section_tailer.unknown));
					byte* image = new byte[c_pixels];
					byte* image_s = new byte[c_pixels];
					char* image_z = new char[c_pixels];
					m_y += m_y_inc;
					for (int yr = 0; yr < 8; yr++)
					{
						for (int xr = 0; xr < 8; xr++)
						{
							{
								memset(image, 0, c_pixels);
								memset(image_s, 0, c_pixels);
								memset(image_z, CHAR_MIN, c_pixels);
								int j = 0;
								for (int y = 0; y < cy; y++)
								{
									for (int x = 0; x < cx; x++)
									{
										const byte* r = f.get_span_data(i, j);
										if (r)
										{
											int z = 0;
											while (z < cz)
											{
												z += *r++;
												int c = *r++;
												while (c--)
												{
													t_vector s_pixel;
													s_pixel.x = x - center_x;
													s_pixel.y = y - center_y;
													s_pixel.z = z - center_z;
													t_vector d_pixel = rotate_y(rotate_x(s_pixel, xr * std::numbers::pi / 4), yr * std::numbers::pi / 4);
													d_pixel.x += l;
													d_pixel.y += l;
													d_pixel.z += center_z;
													int ofs = static_cast<int>(d_pixel.x) + cl * static_cast<int>(d_pixel.y);
													if (d_pixel.z > image_z[ofs])
													{
														image[ofs] = *r++;
														image_s[ofs] = *r++;
														image_z[ofs] = d_pixel.z;
													}
													else
														r += 2;
													z++;
												}
												r++;
											}
										}
										j++;
									}
								}
								switch (vxl_mode)
								{
								case 0:
									draw_image8(image, cl, cl, pDC, xr * (cl + m_y_inc) + offset);
									break;
								case 1:
									{
										t_palette gray_palette;
										if (section_tailer.unknown == 2)
										{
											for (int i = 0; i < 256; i++)
												gray_palette[i].r = gray_palette[i].g = gray_palette[i].b = i * 255 / 35;
										}
										else
										{
											for (int i = 0; i < 256; i++)
												gray_palette[i].r = gray_palette[i].g = gray_palette[i].b = i;
										}
										load_color_table(gray_palette, false);
										draw_image8(image_s, cl, cl, pDC, xr * (cl + m_y_inc) + offset);
									}
									break;
								case 2:
									{
										int min_z = INT_MAX;
										int max_z = INT_MIN;
										int o;
										for (o = 1; o < c_pixels; o++)
										{
											int v = image_z[o];
											if (v == CHAR_MIN)
												continue;
											if (v < min_z)
												min_z = v;
											if (v > max_z)
												max_z = v;
										}
										for (o = 0; o < c_pixels; o++)
										{
											if (image_z[o] == CHAR_MIN)
												image_z[o] = -1;
											else
												image_z[o] -= min_z;
										}
										max_z -= min_z;
										t_palette gray_palette;
										for (int p = 0; p < max_z; p++)
											gray_palette[p].r = gray_palette[p].g = gray_palette[p].b = p * 255 / max_z;
										gray_palette[0xff].r = 0;
										gray_palette[0xff].g = 0;
										gray_palette[0xff].b = 0xff;
										load_color_table(gray_palette, false);
										draw_image8(reinterpret_cast<const byte*>(image_z), cl, cl, pDC, xr * (cl + m_y_inc) + offset);
										break;
									}
								}
							}
						}
						m_y += cl + m_y_inc;
					}					
					delete[] image_z;
					delete[] image_s;
					delete[] image;
				}
				break;
			}
		case ft_wav:
			{
				Cwav_file f;
				f.load(m_data);
				if (!f.process())
				{
					const t_riff_wave_format_chunk& format_chunk = f.get_format_chunk();
					draw_info("Audio:", n(format_chunk.samplerate) + " hz, " + n(format_chunk.cbits_sample) + " bit, " + (format_chunk.c_channels == 1 ? "mono" : "stereo"));
					draw_info("Samples:", n(format_chunk.tag == 1 ? f.get_data_header().size * 8 / (format_chunk.cbits_sample * format_chunk.c_channels) : f.get_fact_chunk().c_samples));
					draw_info("Format:", nh(4, format_chunk.tag));
				}
				break;
			}
		case ft_wsa_dune2:
			{
				Cwsa_dune2_file f;
				f.load(m_data);
				draw_info("Frames:", n(f.cf()));
				draw_info("Size:", n(f.cx()) + " x " + n(f.cy()));
				m_y += m_y_inc;
				load_color_table(get_default_palette(), true);
				Cvirtual_image image = f.vimage();
				const byte* r = image.image();
				for (int i = 0; i < f.cf(); i++)
				{
					draw_image8(r, f.cx(), f.cy(), pDC, offset);
					r += f.cb_image();
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		case ft_wsa:
			{
				Cwsa_file f;
				f.load(m_data);
				draw_info("Frames:", n(f.cf()));
				draw_info("Paletted:", f.palette() ? "Yes" : "No");
				draw_info("Position:", n(f.get_x()) + "," + n(f.get_y()));
				draw_info("Size:", n(f.cx()) + " x " + n(f.cy()));
				m_y += m_y_inc;
				load_color_table(f.palette(), true);
				Cvirtual_image image = f.vimage();
				const byte* r = image.image();
				for (int i = 0; i < f.cf(); i++)
				{
					draw_image8(r, f.cx(), f.cy(), pDC, offset);
					r += f.cb_image();
					m_y += f.cy() + m_y_inc;
				}
				break;
			}
		default:
			show_binary = true;
		}
		if (!m_text_cache_valid)
		{
			switch (m_ft)
			{
			case ft_pal:
				{
					Cpal_file f;
					f.load(m_data);
					m_y += m_y_inc;
					const t_palette_entry* palette = f.get_palette();
					for (int i = 0; i < 256; i++)
						draw_info((nh(2, i) + " - " + nwzl(2, palette[i].r) + ' '+ nwzl(2, palette[i].g) + ' ' + nwzl(2, palette[i].b)), "");
					break;
				}
			case ft_pkt_ts:
				{
					Cpkt_ts_ini_reader ir;
					ir.process(m_data);
					const Cpkt_ts_ini_reader::t_map_list& ml = ir.get_map_list();
					draw_info("Maps:", n(ml.size()));
					m_y += m_y_inc;
					for (auto& i : ml)
						draw_info(i.first, i.second.m_description + ", " + i.second.m_gamemode);
					break;
				}
			case ft_st:
				{
					Cst_file f;
					f.load(m_data);
					const int c_strings = f.get_c_strings();
					draw_info("Strings", n(c_strings));
					m_y += m_y_inc;
					for (int i = 0; i < c_strings; i++)
						draw_info(nwzl(5, i) + ' ' + f.get_string(i), "");
					break;
				}
			case ft_theme_ini_ts:
			case ft_sound_ini_ts:
			case ft_ini:
			case ft_rules_ini_ts:
			case ft_text:
				{
					m_y += m_y_inc;
					Cvirtual_tfile tf;
					tf.load_data(m_data);
					while (!tf.eof())
						draw_info(tf.read_line(), "");
					break;
				}
			case ft_xif:
				{
					m_y += m_y_inc;
					Cxif_key key;
					if (!key.load_key(m_data))
					{
						stringstream s;
						key.dump(s, m_data.size() < 1 << 20);
						string line;
						while (getline(s, line))
						{
							draw_info(line, "");
						}
					}
					break;
				}
			default:
				Cfname fname = to_lower(m_fname);
				if (fname.get_fext() == ".mix" && m_ft != ft_mix && m_ft != ft_mix_rg)
				{
					m_ft = ft_mix;
					Cmix_file_rd f;
					f.load(m_data, m_size);
					const int c_files = f.get_c_files();
					const t_game game = f.get_game();
					draw_info("Files:", n(c_files));
					draw_info("Checksum:", f.has_checksum() ? "Yes" : "No");
					draw_info("Encrypted:", f.is_encrypted() ? "Yes" : "No");
					draw_info("Game:", game_name[game]);
					if (game > game_td)
					{
						draw_info("Raw Flags:", nh(8, f.rawflags()));
					}
					for (int i = 0; i < c_files; i++)
					{
						int id = f.get_id(i);
						draw_info(nwzl(4, i) + " - " + nh(8, id) + nwsl(11, f.get_size(id)) + ' ' + mix_database::get_name(game, id), "");
					}
					m_y += m_y_inc;
					break;
					}
				if (!show_binary)
					break;
				m_y += m_y_inc;
				if (m_data.size() > 32  << 10)
					m_data.set_size(32 << 10);
				for (int r = 0; r < m_data.size(); )
				{
					string line = nwzl(5, r) + ' ';
					int line_data[16];
					for (int c = 0; c < 16; c++)
					{
						line_data[c] = r < m_data.size() ? m_data.data()[r] : -1;
						r++;
					}
					for (int c = 0; c < 16; c++)
					{
						if (!(c & 7))
							line += "- ";
						line += line_data[c] == -1 ? "   " : nh(2, line_data[c]) + ' ';
					}
					line += "- ";
					for (int c = 0; c < 16; c++)
						line += line_data[c] < 0x20 ? ' ' : line_data[c];
					draw_info(line, "");
				}
			}
			SetScrollSizes(MM_TEXT, CSize(m_x, m_y + 4));
			m_text_cache_valid = true;
		}
		for (auto& i : m_text_cache)
		{
				pDC->TextOut(i.text_extent.TopLeft().x, i.text_extent.TopLeft().y, i.t.c_str());
		}
	}
}

void CXCCFileView::open_f(int id, Cmix_file& mix_f, t_game game, t_palette palette)
{
	close_f();
	Ccc_file f(false);
	if (!f.open(id, mix_f))
	{
		m_fname = mix_f.get_name(id);
		m_game = game;
		m_id = id;
		m_palette = palette;
	}
	post_open(f);
}

void CXCCFileView::open_f(const string& name)
{
	close_f();
	Ccc_file f(false);
	if (!f.open(name))
	{
		m_fname = Cfname(name).get_fname();
		m_game = GetMainFrame()->get_game();
		m_id = Cmix_file::get_id(m_game, Cfname(name).get_ftitle());
		m_palette = NULL;
	}
	post_open(f);
}

void CXCCFileView::post_open(Ccc_file& f)
{
	if (f.is_open())
	{
		m_can_pick = false;
		m_cx = 0;
		m_cy = 0;
		m_ft = f.get_file_type(false);
		m_size = f.get_size();
		int cb_max_data = (m_ft == ft_dds || m_ft == ft_jpeg || m_ft == ft_map_td || m_ft == ft_map_ra
			|| m_ft == ft_map_ts || m_ft == ft_pcx || m_ft == ft_png || m_ft == ft_shp
			|| m_ft == ft_shp_ts || m_ft == ft_tga || m_ft == ft_vxl || m_ft == ft_wsa_dune2
			|| m_ft == ft_wsa || m_ft == ft_xif) ? m_size :
			(m_ft == ft_csf ? 64 << 8 : 256 << 10);
		int cb_data = m_size > cb_max_data ? cb_max_data : m_size;	
		f.read(m_data.write_start(cb_data), cb_data);
		f.close();
		m_text_cache_valid = false;
		m_is_open = true;
	}
	ScrollToPosition(CPoint(0, 0));
	Invalidate();
}

void CXCCFileView::close_f()
{
	m_is_open = false;
	m_text_cache.clear();
}

void CXCCFileView::auto_select()
{
	if (!m_can_pick)
	{
		m_palette_filter.select(m_ft, m_cx, m_cy, m_fname);
		m_can_pick = true;
	}
	t_game game;
	int i = 4;
	while (i--)
	{
		string palette = m_palette_filter.pick(game);
		if (!palette.empty() && GetMainFrame()->auto_select(game, palette))
			break;
	}
}

bool CXCCFileView::can_auto_select()
{
	return m_is_open;
}
