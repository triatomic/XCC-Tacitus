#include "stdafx.h"

#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include "dds_file.h"
#include "image_file.h"
#include "jpeg_file.h"
#include "pcx_file.h"
#include "pcx_file_write.h"
#include "png_file.h"
#include "tga_file.h"
#include "virtual_image.h"

using namespace Gdiplus;

Cvirtual_image::Cvirtual_image(const Cvirtual_binary& image, int cx, int cy, int cb_pixel, const t_palette_entry* palette, bool inflate)
{
	load(image, cx, cy, cb_pixel, palette, inflate);
}

Cvirtual_image::Cvirtual_image(const void* image, int cx, int cy, int cb_pixel, const t_palette_entry* palette, bool inflate)
{
	load(image, cx, cy, cb_pixel, palette, inflate);
}

const Cvirtual_image& Cvirtual_image::palette(const t_palette_entry* palette, bool inflate)
{
	if (palette)
	{
		memcpy(m_palette.write_start(sizeof(t_palette)), palette, sizeof(t_palette));
		if (inflate)
			convert_palette_18_to_24(reinterpret_cast<t_palette_entry*>(m_palette.data_edit()));
	}
	else
		m_palette.clear();
	return *this;
}

void Cvirtual_image::load(const Cvirtual_binary& image, int cx, int cy, int cb_pixel, const t_palette_entry* p, bool inflate)
{
	assert(cb_pixel == 1 || cb_pixel == 3 || cb_pixel == 4 || cb_pixel == 6 || cb_pixel == 8);
	m_cx = cx;
	m_cy = cy;
	mcb_pixel = cb_pixel;
	if (image.size() == cb_image())
		m_image = image;
	else
		m_image.write_start(cb_image());
	palette(p, inflate);
}

void Cvirtual_image::load(const void* image, int cx, int cy, int cb_pixel, const t_palette_entry* p, bool inflate)
{
	assert(cb_pixel == 1 || cb_pixel == 3 || cb_pixel == 4 || cb_pixel == 6 || cb_pixel == 8);
	m_cx = cx;
	m_cy = cy;
	mcb_pixel = cb_pixel;
	m_image.write_start(cb_image());
	if (image)
		memcpy(m_image.data_edit(), image, cb_image());
	palette(p, inflate);
}

int Cvirtual_image::load(const Cvirtual_binary& s)
{
	Cpng_file png_f;
	Cpcx_file pcx_f;
	Cdds_file dds_f;
	Ctga_file tga_f;
	if (png_f.load(s), png_f.is_valid())
		return png_f.decode(*this);
	else if (pcx_f.load(s), pcx_f.is_valid())
		*this = pcx_f.vimage();
	else if(dds_f.load(s), dds_f.is_valid())
		* this = dds_f.vimage();
	else if (tga_f.load(s), tga_f.is_valid())
		return tga_f.decode(*this);
	else
	{
		IStream* is = SHCreateMemStream(s.data(), s.size());
		Gdiplus::Bitmap bmp(is);
		is->Release();
		if (bmp.GetLastStatus() != Ok)
			return 1;
		PixelFormat pf = bmp.GetPixelFormat();
		if (bmp.GetPixelFormat() & PixelFormatIndexed)
		{
			load(NULL, bmp.GetWidth(), bmp.GetHeight(), 1, NULL);
			BitmapData d;
			d.Stride = bmp.GetWidth();
			d.Scan0 = image_edit();
			bmp.LockBits(NULL, ImageLockModeRead | ImageLockModeUserInputBuf, PixelFormat8bppIndexed, &d);
			bmp.UnlockBits(&d);
		}
		else
		{
			load(NULL, bmp.GetWidth(), bmp.GetHeight(), 3, NULL);
			BitmapData d;
			d.Stride = bmp.GetWidth() * 3;
			d.Scan0 = image_edit();
			bmp.LockBits(NULL, ImageLockModeRead | ImageLockModeUserInputBuf, PixelFormat24bppRGB, &d);
			bmp.UnlockBits(&d);
			swap_rb();
		}
	}
	return 0;
}

int Cvirtual_image::load(const Cvirtual_file& f)
{
	return load(f.read());
}

int Cvirtual_image::load(const string& fname)
{
	Cvirtual_binary s;
	int error = s.load(fname);
	if (!error)
		error = load(s);
	return error;
}

int Cvirtual_image::save(Cvirtual_file& f, t_file_type ft) const
{
	return image_file_write(f, ft, image(), palette(), m_cx, m_cy, mcb_pixel);
}

Cvirtual_file Cvirtual_image::save(t_file_type ft) const
{
	return image_file_write(ft, image(), palette(), m_cx, m_cy, mcb_pixel);
}

int Cvirtual_image::save(const string& fname, t_file_type ft) const
{
	return image_file_write(fname, ft, image(), palette(), m_cx, m_cy, mcb_pixel);
}

void Cvirtual_image::swap_rb()
{
	int count = m_cx * m_cy;
	t_palette_entry* r = reinterpret_cast<t_palette_entry*>(m_image.data_edit());
	while (count--)
	{
		swap(r->r, r->b);
		r++;
	}
}

static void flip_frame(const byte* s, byte* d, int cx, int cy, int cb_pixel)
{
	int cb_line = cx * cb_pixel;
	const byte* r = s;
	byte* w = d + cb_line * cy;
	while (cy--)
	{
		w -= cb_line;
		memcpy(w, r, cb_line);
		r += cb_line;
	}
}

void Cvirtual_image::flip()
{
	Cvirtual_binary t = m_image;
	flip_frame(t.data(), image_edit(), cx(), cy(), cb_pixel());
}


void Cvirtual_image::cb_pixel(int cb_pixel, const t_palette_entry* palette)
{
	if (cb_pixel < mcb_pixel)
		decrease_color_depth(cb_pixel, palette);
	else if (cb_pixel > mcb_pixel)
		increase_color_depth(cb_pixel);
}

void Cvirtual_image::decrease_color_depth(int new_cb_pixel, const t_palette_entry* palette)
{
	if (new_cb_pixel == 3)
	{
		remove_alpha();
		return;
	}
	assert(new_cb_pixel == 1);
	int old_cb_pixel = cb_pixel();
	Cvirtual_binary t = m_image;
	load(NULL, cx(), cy(), new_cb_pixel, palette);
	byte* w = image_edit();
	int count = m_cx * m_cy;
	if (old_cb_pixel == 3)
	{
		const t_palette_entry* r = reinterpret_cast<const t_palette_entry*>(t.data());
		while (count--)
		{
			*w++ = find_color(r->r, r->g, r->b, palette);
			r++;
		}
	}
	else
	{
		assert(old_cb_pixel == 4);
		const t_palette32_entry* r = reinterpret_cast<const t_palette32_entry*>(t.data());
		while (count--)
		{
			*w++ = r->a < 0x80 ? find_color(r->r, r->g, r->b, palette) : 0;
			r++;
		}
	}
}

static t_palette32_entry p32e(int r, int g, int b, int a = 0)
{
	t_palette32_entry e;
	e.r = r;
	e.g = g;
	e.b = b;
	e.a = a;
	return e;
}

static t_palette32_entry p32e(t_palette_entry e)
{
	return p32e(e.r, e.g, e.b);
}

static t_palette32_entry p32e(const t_palette palette, int i)
{
	return i ? p32e(palette[i]) : p32e(0x80, 0x80, 0x80, 0xff);
}

void Cvirtual_image::increase_color_depth(int new_cb_pixel)
{
	if (cb_pixel() == 3)
	{
		if (new_cb_pixel == 4)
			add_alpha();
		return;
	}
	assert(cb_pixel() == 1);
	Cvirtual_image t = *this;
	const byte* r = t.image();
	load(NULL, cx(), cy(), new_cb_pixel, NULL);
	int count = m_cx * m_cy;
	if (cb_pixel() == 3)
	{
		t_palette_entry* w = reinterpret_cast<t_palette_entry*>(image_edit());
		while (count--)
			*w++ = t.palette()[*r++];
	}
	else
	{
		assert(cb_pixel() == 4);
		t_palette32_entry* w = reinterpret_cast<t_palette32_entry*>(image_edit());
		while (count--)
			*w++ = p32e(t.palette(), *r++);
	}
}

void Cvirtual_image::add_alpha()
{
	assert(cb_pixel() == 3);
	Cvirtual_binary t = m_image;
	load(NULL, cx(), cy(), 4, NULL);
	int count = m_cx * m_cy;
	const byte* r = t.data();
	byte* w = image_edit();
	while (count--)
	{
		*w++ = *r++;
		*w++ = *r++;
		*w++ = *r++;
		*w++ = 0;
	}
}

void Cvirtual_image::remove_alpha()
{
	if (cb_pixel() != 4)
		return;
	Cvirtual_binary t = m_image;
	load(NULL, cx(), cy(), 3, NULL);
	int count = m_cx * m_cy;
	const byte* r = t.data();
	byte* w = image_edit();
	while (count--)
	{
		*w++ = *r++;
		*w++ = *r++;
		*w++ = *r++;
		r++;
	}
}

void Cvirtual_image::increase_palette_depth()
{
	assert(false);
	Cvirtual_binary t = m_palette;
	const t_palette_entry* s = reinterpret_cast<const t_palette_entry*>(t.data());
	t_palette_entry* d = reinterpret_cast<t_palette_entry*>(t.data_edit());
	for (int i = 0; i < 256; i++)
	{
		d[i].r = (s[i].r & 63) * 255 / 63;
		d[i].g = (s[i].g & 63) * 255 / 63;
		d[i].b = (s[i].b & 63) * 255 / 63;
	}
}
