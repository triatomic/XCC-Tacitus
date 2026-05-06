#pragma once

#include <cc_file_small.h>
#include <palette.h>

class Cpal_file : public Ccc_file_small
{
public:
	ostream& extract_as_pal_jasc(ostream&, bool shift_left = true) const;
	ostream& extract_as_text(ostream&) const;
	bool is_valid() const;

	void decode(t_palette& palette) const
	{
		convert_palette_18_to_24(get_palette(), palette);
	}

	const t_palette_entry* get_palette() const
	{
		return reinterpret_cast<const t_palette_entry*>(data());
	}
};
