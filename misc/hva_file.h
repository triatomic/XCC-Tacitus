#pragma once

#include <cc_file_sh.h>
#include <cc_structures.h>

class Chva_file : public Ccc_file_sh<t_hva_header>
{
public:
	int extract_as_csv(const string& name) const;
	bool is_valid() const;

	int get_c_frames() const
	{
		return header().c_frames;
	}

	int get_c_sections() const
	{
		return header().c_sections;
	}

	const char* get_section_id(int i) const
	{
		return reinterpret_cast<const char*>(data() + sizeof(t_hva_header) + 16 * i);
	}

	// HVA frame data on disk is laid out frame-major: for each frame, a
	// matrix per section in order. (Confirmed against Vengi's reader at
	// HVAFormat.cpp:60 which iterates frame outer, section inner.) Earlier
	// versions of this accessor used section-major indexing, which swapped
	// matrices between sections on the first non-zero frame.
	//
	// i = section index, j = frame index.
	const float* get_transform_matrix(int i, int j) const
	{
		return reinterpret_cast<const float*>(data() + sizeof(t_hva_header) + 16 * get_c_sections() + (get_c_sections() * j + i) * sizeof(t_hva_transform_matrix));
	}
};

Cvirtual_binary hva_file_write(const byte* s, int cb_s);
