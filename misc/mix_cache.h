#pragma once

#include <cstdint>
#include <vector>
#include <virtual_binary.h>

namespace mix_cache
{
	struct Entry
	{
		uint8_t game = 0;        // t_game value; populated on cache hit
		std::vector<int> lmd;    // indices of ft_xcc_lmd entries (usually empty)
		Cvirtual_binary ft;      // raw t_file_type bytes, size = c_files * sizeof(t_file_type)
	};

	int load();
	int save();

	// New API — extended record with game + LMD index list.
	const Entry* get_entry(int crc);
	void set_entry(int crc, Entry e);

	// Legacy API — still works, but only sees the ft blob.
	Cvirtual_binary get_data(int crc);
	void set_data(int crc, const Cvirtual_binary&);
};
