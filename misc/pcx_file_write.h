#pragma once

#include "cc_structures.h"
#include "file32.h"
#include "palette.h"
#include "virtual_file.h"

class Cpcx_file_write : public Cfile32  
{
public:
	void set_size(int cx, int cy, int c_planes);
	int write_header();
	int write_image(const void* data, int size);
	int write_palette(const t_palette palette);
private:
	int m_cx;
	int m_cy;
	int mc_planes;
};

Cvirtual_binary pcx_file_write(const byte* image, const t_palette_entry* palette, int cx, int cy, int planes);
void pcx_file_write(Cvirtual_file& f, const byte* image, const t_palette_entry* palette, int cx, int cy, int planes);
int pcx_file_write(const string& name, const byte* image, const t_palette_entry* palette, int cx, int cy, int planes);
