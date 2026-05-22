#include "stdafx.h"
#include <id_log.h>

#include <fstream>
#include <windows.h>
#include <mix_file.h>
#include <string_conversion.h>
#include <xcc_dirs.h>

struct t_idinfo
{
	string name;
	string description;
};

using t_id_list = map<int, t_idinfo>;

t_id_list test_list;

static t_id_list& get_list(t_game game)
{
		return test_list;
}

static void read_list(t_game game, const char*& s)
{
	t_id_list& d = test_list;
	int count = *reinterpret_cast<const int*>(s);
	s += 4;
	t_idinfo idinfo;
	while (count--)
	{
		idinfo.name = s;
		s += idinfo.name.length() + 1;
		idinfo.description = s;
		s += idinfo.description.length() + 1;
		d[Cmix_file::get_id(game, idinfo.name)] = idinfo;
	}
}

int mix_database::load_from_buffer(const void* data, int size)
{
	if (!data || size < 16)
		return 1;
	const char* base = reinterpret_cast<const char*>(data);
	const char* p = base;
	read_list(game_td, p);	//td for default id value (td, ra)
	p = base;				//refresh
	read_list(game_ts, p);	//ts for ts and ra2 value
	return 0;
}

int mix_database::load()
{
	Cvirtual_binary f;
	if (f.load(xcc_dirs::get_data_dir() + "global mix database.dat"))
		return 1;
	return load_from_buffer(f.data(), f.size());
	char name[12] = "scg00ea.bin";	//i have no idea what's going on here, ra and td isn't my expertise
	const char char1[] = "bgjm";
	const char char2[] = "ew";
	const char char3[] = "abc";
	for (int i = 0; i < 2; i++)
	{
		if (i)
			strcpy(name + 8, "ini");
		for (int j = 0; j < 4; j++)
		{
			name[2] = char1[j];
			for (int k = 0; k < 100; k++)
			{
				memcpy(name + 3, nwzl(2, k).c_str(), 2);
				for (int l = 0; l < 2; l++)
				{
					name[5] = char2[l];
					for (int m = 0; m < 3; m++)
					{
						name[6] = char3[m];
						mix_database::add_name(game_td, name, "");
						mix_database::add_name(game_ra, name, "");
						mix_database::add_name(game_ts, name, "");
					}
				}
			}
		}
	}
	return 0;
}

void mix_database::add_name(t_game game, const string& name, const string& description)
{
	t_idinfo idinfo;
	idinfo.name = name;
	idinfo.description = description;
	get_list(game)[Cmix_file::get_id(game, name)] = idinfo;
}

const string& mix_database::get_name(t_game game, int id)
{
	static const string empty;
	auto i = find_ptr(get_list(game), id);
	return i ? i->name : empty;
}

const string& mix_database::get_description(t_game game, int id)
{
	static const string empty;
	auto i = find_ptr(get_list(game), id);
	return i ? i->description : empty;
}

void mix_database::clear()
{
	test_list.clear();
}

int mix_database::reload_with_fallback(int* source_out)
{
	if (source_out)
		*source_out = mix_database::load_source_none;
	// Try the primary data dir first (whatever xcc_dirs currently
	// resolves to). On failure, fall back to a reset of the data dir
	// (mirrors CXCCMixerApp::InitInstance's startup chain) and retry.
	if (mix_database::load() == 0)
	{
		if (source_out)
			*source_out = mix_database::load_source_on_disk;
		return 0;
	}
	xcc_dirs::reset_data_dir();
	if (mix_database::load() == 0)
	{
		if (source_out)
			*source_out = mix_database::load_source_on_disk;
		return 0;
	}
	// On-disk dat is missing or malformed. Fall back to the dat blob
	// baked into the executable as RCDATA so panes still show
	// human-readable names instead of 8-hex-digit IDs.
	HRSRC res = ::FindResource(NULL, "GLOBAL_MIX_DATABASE", RT_RCDATA);
	if (res)
	{
		HGLOBAL g = ::LoadResource(NULL, res);
		DWORD sz = ::SizeofResource(NULL, res);
		if (g && sz)
		{
			if (mix_database::load_from_buffer(::LockResource(g), static_cast<int>(sz)) == 0)
			{
				if (source_out)
					*source_out = mix_database::load_source_embedded;
				return 0;
			}
		}
	}
	return 1;
}
