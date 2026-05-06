#pragma once

#include "cc_structures.h"
#include "image_file.h"
#include "palette.h"
#include "pcx_decode.h"
#include "virtual_image.h"

class Cpcx_file : public Cimage_file<t_pcx_header>
{
public:
	void decode(void*) const;
	bool is_valid() const;
	Cvirtual_image vimage() const;

	int cb_pixel() const
	{
		return header().c_planes;
	}

	int cx() const
	{
		return header().xmax - header().xmin + 1;
	}

	int cy() const
	{
		return header().ymax - header().ymin + 1;
	}

	void decode(byte* d) const
	{
		pcx_decode(get_image(), d, header());
	}

	const byte* get_image() const
	{
		return data() + sizeof(t_pcx_header);
	}

	const t_palette* get_palette() const
	{
		return reinterpret_cast<const t_palette*>(data() + size() - sizeof(t_palette));
	}
};
