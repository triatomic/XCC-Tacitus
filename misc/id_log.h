#pragma once

#include <cc_structures.h>
#include <string>

namespace mix_database
{
	void add_name(t_game, const std::string& name, const std::string& description);
	const std::string& get_name(t_game, int id);
	const std::string& get_description(t_game, int id);
	int load();
	// Parse the dat blob from an in-memory buffer (e.g. an embedded
	// resource) instead of <data dir>/global mix database.dat. Same on-disk
	// layout. Returns 0 on success, 1 on failure (size < 16). Lets apps
	// fall back to a baked-in copy when the on-disk file is missing.
	int load_from_buffer(const void* data, int size);

	// Wipe the in-memory name map. Used by the runtime "reload" action
	// before re-running load(). After clear() the map has zero entries;
	// any subsequent get_name() returns "" until names are re-added (via
	// load() or LMD walks).
	void clear();

	// Source sentinel values for reload_with_fallback(). Mirror of the
	// mix_db_source_* enum in Mixer\XCC Mixer.h; redeclared here so the
	// shared library doesn't depend on Mixer headers.
	enum t_load_source
	{
		load_source_none = 0,
		load_source_on_disk = 1,
		load_source_embedded = 2,
	};

	// Run the full DB load chain shared by app startup and the runtime
	// reload command: try on-disk first, then reset_data_dir + retry on
	// failure, then fall back to the embedded RCDATA blob
	// "GLOBAL_MIX_DATABASE" if both on-disk attempts failed or returned
	// malformed bytes. *source_out (may be NULL) is set to whichever
	// path actually succeeded. Returns 0 on success, 1 if nothing
	// loaded (DB now empty).
	int reload_with_fallback(int* source_out);
};
