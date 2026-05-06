#include "stdafx.h"
#include "png_file.h"

#include <cstdint>
#include <png.h>
#include <setjmp.h>
#include "fname.h"

void user_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
	longjmp(png_jmpbuf(png_ptr), 1);
}

int Cpng_file::decode(Cvirtual_image& d) const
{
	png_image img;
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&img, data(), size()))
		return 1;
	int cx = img.width;
	int cy = img.height;
	Cvirtual_binary t;
	switch (img.format)
	{
	case PNG_FORMAT_RGB_COLORMAP:
		t_palette palette;
		if (!png_image_finish_read(&img, NULL, t.write_start(cx * cy), cx, palette))
			return 1;
		d.load(t, cx, cy, 1, palette);
		return 0;
	case PNG_FORMAT_RGB:
		if (!png_image_finish_read(&img, NULL, t.write_start(3 * cx * cy), 3 * cx, NULL))
			return 1;
		d.load(t, cx, cy, 3, NULL);
		return 0;
	case PNG_FORMAT_RGBA:
		if (!png_image_finish_read(&img, NULL, t.write_start(4 * cx * cy), 4 * cx, NULL))
			return 1;
		d.load(t, cx, cy, 4, NULL);
		return 0;
	case PNG_FORMAT_LINEAR_RGB:
		if (!png_image_finish_read(&img, NULL, t.write_start(3 * 2 * cx * cy), 3 * cx, NULL))
			return 1;
		d.load(t, cx, cy, 3 * 2, NULL);
		return 0;
	case PNG_FORMAT_LINEAR_RGB_ALPHA:
		if (!png_image_finish_read(&img, NULL, t.write_start(4 * 2 * cx * cy), 4 * cx, NULL))
			return 1;
		d.load(t, cx, cy, 4 * 2, NULL);
		return 0;
	default:
		png_image_free(&img);
		return 1;
	}
}

int png_file_write(Cvirtual_file& f, const byte* image, const t_palette_entry* palette, int cx, int cy, int pixel)
{
	string temp_fname = get_temp_fname();
	int error = png_file_write(temp_fname, image, palette, cx, cy, pixel);
	if (!error)
	{
		error = f.load(temp_fname);
	}
	delete_file(temp_fname);
	return error;
}

int png_file_write(const string& name, const byte* image, const t_palette_entry* palette, int cx, int cy, int pixel)
{
	png_image img;
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	img.width = cx;
	img.height = cy;
	if (palette)
	{
		img.format = PNG_FORMAT_RGB_COLORMAP;
		img.colormap_entries = 256;
		return !png_image_write_to_file(&img, name.c_str(), false, image, cx, palette);
	}
	else
	{
		int row_stride = pixel * cx;
		switch (pixel)
		{
		case 3:
			img.format = PNG_FORMAT_RGB;
			break;
		case 4:
			img.format = PNG_FORMAT_RGBA;
			break;
		case 6:
			img.format = PNG_FORMAT_LINEAR_RGB;
			img.flags = PNG_IMAGE_FLAG_16BIT_sRGB;
			row_stride >>= 1;
			break;
		case 8:
			img.format = PNG_FORMAT_LINEAR_RGB_ALPHA;
			img.flags = PNG_IMAGE_FLAG_16BIT_sRGB;
			row_stride >>= 1;
			break;
		default:
			return 1;
		}
		return !png_image_write_to_file(&img, name.c_str(), false, image, row_stride, NULL);
	}
}
