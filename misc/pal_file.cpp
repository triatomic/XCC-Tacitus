#include "stdafx.h"
#include "pal_file.h"
#include "string_conversion.h"

#include <fstream>

bool Cpal_file::is_valid() const
{
	if (get_size() != sizeof(t_palette))
		return false;
	const t_palette_entry* p = get_palette();
	for (int i = 0; i < 256; i++)
	{
		if ((p[i].r | p[i].g | p[i].b) & 0xc0)
			return false;
	}
	return true;
}

ostream& Cpal_file::extract_as_pal_jasc(ostream& os, bool shift_left) const
{
	os << "JASC-PAL" << endl
		<< "0100" << endl
		<< "256" << endl;
	t_palette palette;
	if (shift_left)
		convert_palette_18_to_24(get_palette(), palette);
	else
		memcpy(palette, get_palette(), sizeof(t_palette));
	for (int i = 0; i < 256; i++)
		os << static_cast<int>(palette[i].r) << ' ' << static_cast<int>(palette[i].g) << ' ' << static_cast<int>(palette[i].b) << endl;
	return os;
}

ostream& Cpal_file::extract_as_text(ostream& os) const
{
	t_palette palette;
	memcpy(palette, get_palette(), sizeof(t_palette));
	for (int i = 0; i < 256; i++)
		os << nwzl(2, palette[i].r) << ", " << nwzl(2, palette[i].g) << ", " << nwzl(2, palette[i].b) << " - #" << nh(2, palette[i].r * 255 / 63) << nh(2, palette[i].g * 255 / 63) << nh(2, palette[i].b * 255 / 63) << endl;
	return os;
}
