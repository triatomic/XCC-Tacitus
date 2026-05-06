#pragma once

#include "palette.h"

class Cvideo_decoder  
{
public:
	virtual int cb_pixel() const = 0;
	virtual int cf() const = 0;
	virtual int cx() const = 0;
	virtual int cy() const = 0;
	virtual int decode(void* d) = 0;
	virtual const t_palette_entry* palette() const;
	virtual int seek(int f) = 0;
	virtual ~Cvideo_decoder();

	int cb_image() const
	{
		return cb_pixel() * cx() * cy();
	}

	int cb_video() const
	{
		return cf() * cb_image();
	}
};
