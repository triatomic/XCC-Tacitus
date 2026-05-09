#pragma once

#include <cc_structures.h>
#include <string>

namespace mix_database
{
	void add_name(t_game, const std::string& name, const std::string& description);
	std::string get_name(t_game, int id);
	std::string get_description(t_game, int id);
	int load();
	// Parse the dat blob from an in-memory buffer (e.g. an embedded
	// resource) instead of <data dir>/global mix database.dat. Same on-disk
	// layout. Returns 0 on success, 1 on failure (size < 16). Lets apps
	// fall back to a baked-in copy when the on-disk file is missing.
	int load_from_buffer(const void* data, int size);
};
