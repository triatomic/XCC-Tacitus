#include "stdafx.h"

#include <afx.h>
#include <afxdlgs.h>
#include "dds_file.h"
#include "image_file.h"
#include "jpeg_file.h"
#include "pcx_file.h"
#include "pcx_file_write.h"
#include "png_file.h"
#include "tga_file.h"
#include "virtual_image.h"

int Cvirtual_image::get_clipboard()
{
	int error = 0;
	if (!OpenClipboard(NULL))
		return 0x100;
	void* h_mem = GetClipboardData(CF_DIB);
	if (!h_mem)
		error = 0x101;
	else
	{
		byte* mem = reinterpret_cast<byte*>(GlobalLock(h_mem));
		if (!mem)
			error = 0x102;
		else
		{	
			const BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(mem);
			int cb_pixel = header->biBitCount >> 3;
			if (cb_pixel != 1 && cb_pixel != 3)
				error = 0x103;
			else
			{
				t_palette_entry* palette = cb_pixel == 1 ? new t_palette : NULL;
				const RGBQUAD* r = reinterpret_cast<RGBQUAD*>(mem + header->biSize);
				if (palette)
				{
					for (int i = 0; i < (header->biClrUsed ? header->biClrUsed : 256); i++)
					{
						palette[i].r = r->rgbRed;
						palette[i].g = r->rgbGreen;
						palette[i].b = r->rgbBlue;
						r++;
					}
				}
				int cx = header->biWidth;
				int cy = header->biHeight;
				if (cx * cb_pixel & 3)
				{
					int cb_line = cx * cb_pixel;
					byte* d = new byte[cb_line * cy];
					byte* w = d;
					for (int y = 0; y < cy; y++)
					{
						memcpy(w, r, cb_line);
						r += cb_line + 3 >> 2;
						w += cb_line;
					}
					load(d, cx, cy, cb_pixel, palette);
					delete[] d;
				}
				else
					load(r, cx, cy, cb_pixel, palette);
				flip();
				if (cb_pixel == 3)
					swap_rb();
				delete palette;
			}
			GlobalUnlock(h_mem);
		}
	}
	CloseClipboard();
	return error;
}

int Cvirtual_image::set_clipboard() const
{
	int error = 0;
	int cb_line = cx() * cb_pixel();
	void* h_mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD) + (cb_line + 3 & ~3) * cy());
	if (!h_mem)
		error = 0x100;
	else
	{
		byte* mem = reinterpret_cast<byte*>(GlobalLock(h_mem));
		if (!mem)
			error = 0x101;
		else
		{
			BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(mem);
			ZeroMemory(header, sizeof(BITMAPINFOHEADER));
			header->biSize = sizeof(BITMAPINFOHEADER);
			header->biWidth = cx();
			header->biHeight = cy();
			header->biPlanes = 1;
			header->biBitCount = cb_pixel() << 3;
			header->biCompression = BI_RGB;
			RGBQUAD* palette = reinterpret_cast<RGBQUAD*>(mem + sizeof(BITMAPINFOHEADER));
			if (cb_pixel() == 1)
			{
				for (int i = 0; i < 256; i++)
				{
					palette->rgbBlue = this->palette()[i].b;
					palette->rgbGreen = this->palette()[i].g;
					palette->rgbRed = this->palette()[i].r;
					palette->rgbReserved = 0;
					palette++;
				}
			}
			const byte* r = image() + cb_image();
			byte* w = reinterpret_cast<byte*>(palette);
			for (int y = 0; y < cy(); y++)
			{
				r -= cb_line;
				if (cb_pixel() == 3)
				{
					for (int x = 0; x < cx(); x++)
					{
						const t_palette_entry* v = reinterpret_cast<const t_palette_entry*>(r) + x;
						*w++ = v->b;
						*w++ = v->g;
						*w++ = v->r;
					}
					w -= cb_line;
				}
				else
					memcpy(w, r, cb_line);
				w += cb_line + 3 & ~3;
			}
			GlobalUnlock(h_mem);
			if (!OpenClipboard(NULL))
				error = 0x102;
			else
			{
				if (EmptyClipboard() && SetClipboardData(CF_DIB, h_mem))
					h_mem = NULL;
				else
					error = 0x103;
				CloseClipboard();
			}
		}
		if (h_mem)
			GlobalFree(h_mem);
	}
	return error;
}

int Cvirtual_image::set_clipboard_png() const
{
	// Caller contract: cb_pixel() == 4, pixels are RGBA (R,G,B,A per pixel) so
	// png_file_write's PNG_FORMAT_RGBA path can consume the buffer directly.
	if (cb_pixel() != 4)
		return 0x200;
	int error = 0;

	// 1) Encode RGBA buffer to PNG bytes via the existing Cvirtual_file overload
	//    (uses a temp file internally; acceptable for a one-shot clipboard write).
	Cvirtual_file png_vf;
	if (png_file_write(png_vf, image(), NULL, cx(), cy(), 4))
		return 0x201;
	int png_size = png_vf.size();
	const byte* png_bytes = png_vf.data();
	if (png_size <= 0 || !png_bytes)
		return 0x202;

	static UINT cf_png = 0;
	if (!cf_png)
		cf_png = RegisterClipboardFormatA("PNG");
	if (!cf_png)
		return 0x203;

	// 2) Build CF_DIB 24bpp fallback: flatten RGBA over mid-gray so legacy
	//    paste targets see a visible image (not pure black on alpha=0).
	int cx_ = cx();
	int cy_ = cy();
	const int cb_line_24_unaligned = cx_ * 3;
	const int cb_line_24 = cb_line_24_unaligned + 3 & ~3;
	HGLOBAL h_png = GlobalAlloc(GMEM_MOVEABLE, png_size);
	HGLOBAL h_dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + cb_line_24 * cy_);
	if (!h_png || !h_dib)
		error = 0x100;
	else
	{
		// --- CF_PNG ---
		byte* mem_png = reinterpret_cast<byte*>(GlobalLock(h_png));
		if (!mem_png)
			error = 0x101;
		else
		{
			memcpy(mem_png, png_bytes, png_size);
			GlobalUnlock(h_png);
		}

		// --- CF_DIB 24bpp fallback ---
		byte* mem_dib = error ? nullptr : reinterpret_cast<byte*>(GlobalLock(h_dib));
		if (!error && !mem_dib)
			error = 0x102;
		else if (!error)
		{
			BITMAPINFOHEADER* hd = reinterpret_cast<BITMAPINFOHEADER*>(mem_dib);
			ZeroMemory(hd, sizeof(BITMAPINFOHEADER));
			hd->biSize = sizeof(BITMAPINFOHEADER);
			hd->biWidth = cx_;
			hd->biHeight = cy_;
			hd->biPlanes = 1;
			hd->biBitCount = 24;
			hd->biCompression = BI_RGB;
			const int bg = 0x80;
			byte* w = mem_dib + sizeof(BITMAPINFOHEADER);
			const int cb_line_rgba = cx_ * 4;
			const byte* r = image() + cb_line_rgba * cy_;
			for (int y = 0; y < cy_; y++)
			{
				r -= cb_line_rgba;
				for (int x = 0; x < cx_; x++)
				{
					int sr = r[x * 4 + 0];
					int sg = r[x * 4 + 1];
					int sb = r[x * 4 + 2];
					int sa = r[x * 4 + 3];
					// DIB rows are BGR; composite over mid-gray
					w[x * 3 + 0] = (sb * sa + bg * (0xff - sa) + 0x7f) / 0xff;
					w[x * 3 + 1] = (sg * sa + bg * (0xff - sa) + 0x7f) / 0xff;
					w[x * 3 + 2] = (sr * sa + bg * (0xff - sa) + 0x7f) / 0xff;
				}
				for (int p = cb_line_24_unaligned; p < cb_line_24; p++)
					w[p] = 0;
				w += cb_line_24;
			}
			GlobalUnlock(h_dib);
		}

		if (!error)
		{
			if (!OpenClipboard(NULL))
				error = 0x103;
			else
			{
				if (!EmptyClipboard())
					error = 0x104;
				else
				{
					if (SetClipboardData(cf_png, h_png))
						h_png = NULL;
					else
						error = 0x105;
					if (!error && SetClipboardData(CF_DIB, h_dib))
						h_dib = NULL;
					else if (!error)
						error = 0x106;
				}
				CloseClipboard();
			}
		}
	}
	if (h_png)
		GlobalFree(h_png);
	if (h_dib)
		GlobalFree(h_dib);
	return error;
}

int Cvirtual_image::load()
{
	const char* load_filter = "Image files (*.jpeg;*.jpg;*.pcx;*.png)|*.jpeg;*.jpg;*.pcx;*.png|JPEG files (*.jpeg;*.jpg)|*.jpeg;*.jpg|PCX files (*.pcx)|*.pcx|PNG files (*.png)|*.png|";

	CFileDialog dlg(true, NULL, NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, load_filter, NULL);
	if (IDOK == dlg.DoModal())
		return load(static_cast<string>(dlg.GetPathName()));
	return 2;
}

int Cvirtual_image::save() const
{
	const char* save_filter = "JPEG files (*.jpeg;*.jpg)|*.jpeg;*.jpg|PCX files (*.pcx)|*.pcx|PNG files (*.png)|*.png|";

	CFileDialog dlg(false, "", NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, save_filter, NULL);
	dlg.m_ofn.nFilterIndex = 2;
	if (IDOK == dlg.DoModal())
	{
		t_file_type ft = ft_pcx;
		switch (dlg.m_ofn.nFilterIndex)
		{
		case 1:
			ft = ft_jpeg;
			break;
		case 2:
			ft = ft_pcx;
			break;
		case 3:
			ft = ft_png;
			break;
		}
		return save(static_cast<string>(dlg.GetPathName()), ft);
	}
	return 2;
}
