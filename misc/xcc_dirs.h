#pragma once

#include "cc_structures.h"
#include "fname.h"
#include <vector>

class xcc_dirs
{
public:
	enum t_dir_source { src_retail, src_steam };
	struct t_detected_source
	{
		t_dir_source kind;
		string path;
		string label;
	};

	static bool enable_log();
	static void load_from_registry();
	static void save_to_registry();
	static string get_audio_mix(t_game game);
	static string get_csf_fname(t_game game);
	static string get_dir(t_game game);
	static string get_language_mix(t_game game);
	static string get_local_mix(t_game game);
	static string get_main_mix(t_game game);
	static void set_td_secondary_dir(const string& s);
	static void set_cd_dir(const string &s);
	static void set_data_dir(const string &s);
	static void set_dir(t_game game, const string& s);
	static void reset_cd_dir();
	static void reset_data_dir();
	static const string& get_td_secondary_dir();
	static const string& get_cd_dir();
	static const string& get_data_dir();
	static const std::vector<t_detected_source>& get_detected_sources(t_game game);
};
