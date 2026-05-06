#include "stdafx.h"
#include "pcx_file_write.h"

#include "pcx_decode.h"

void Cpcx_file_write::set_size(int cx, int cy, int c_planes)
{
	m_cx = cx;
	m_cy = cy;
	mc_planes = c_planes;
}

int Cpcx_file_write::write_header()
{
	t_pcx_header header;
	header.manufacturer = 10;
	header.version = 5;
	header.encoding = 1;
	header.cbits_pixel = 8;
	header.xmin = 0;
	header.ymin = 0;
	header.xmax = m_cx - 1;
	header.ymax = m_cy - 1;
	header.cx_inch = 72;
	header.cy_inch = 72;
	header.c_planes = mc_planes;
	header.cb_line = m_cx;
	return write(&header, sizeof(t_pcx_header));
}

int Cpcx_file_write::write_image(const void* data, int size)
{
	return write(data, size);
}

int Cpcx_file_write::write_palette(const t_palette palette)
{
	const byte v = 12;
	int error = write(&v, 1);
	if (error)
		return error;
	return write(palette, sizeof(t_palette));
}

Cvirtual_binary pcx_file_write(const byte* image, const t_palette_entry* palette, int cx, int cy, int planes)
{
	Cvirtual_file f;
	pcx_file_write(f, image, palette, cx, cy, planes);
	return f.read();
}

void pcx_file_write(Cvirtual_file& f, const byte* image, const t_palette_entry* palette, int cx, int cy, int planes)
{
	int c_planes = palette ? 1 : planes;
	t_pcx_header header;
	header.manufacturer = 10;
	header.version = 5;
	header.encoding = 1;
	header.cbits_pixel = 8;
	header.xmin = 0;
	header.ymin = 0;
	header.xmax = cx - 1;
	header.ymax = cy - 1;
	header.cx_inch = 72;
	header.cy_inch = 72;
	header.c_planes = c_planes;
	header.cb_line = cx;
	f.write(&header, sizeof(t_pcx_header));
	byte* d = new byte[2 * cx * cy * c_planes];
	f.write(d, pcx_encode(image, d, cx, cy, c_planes));
	delete[] d;
	if (palette)
		f.write(palette, sizeof(t_palette));
}

int pcx_file_write(const string& name, const byte* image, const t_palette_entry* palette, int cx, int cy, int planes)
{
	Cpcx_file_write f;
	int error = f.open_write(name);
	if (!error)
	{
		int c_planes = palette ? 1 : 3;
		f.set_size(cx, cy, c_planes);
		error = f.write_header();
		if (!error)
		{
			byte* d = new byte[2 * cx * cy * c_planes];
			error = f.write_image(d, pcx_encode(image, d, cx, cy, palette ? 1 : planes));
			delete[] d;
			if (!error && palette)
				error = f.write_palette(palette);
		}
		f.close();
	}
	return error;
}
