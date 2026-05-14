#include "stdafx.h"
#include <map>
#include "cc_file.h"
#include "crc.h"
#include "file32.h"
#include "mix_cache.h"
#include "xcc_dirs.h"

static map<int, mix_cache::Entry> cache;

static string get_fname()
{
	return xcc_dirs::get_data_dir() + "global mix cache.dat";
}

static int get_ft_crc()
{
	// Sentinel mixed in to invalidate older caches whose entries were built
	// by a different probe path. Bump when the magic_dispatch / probe order
	// changes in cc_file.cpp so old type-tables don't get reused.
	// v5: record layout extended to carry m_game + LMD index list per crc.
	static const char* probe_version = "probe-v5-game";
	Ccrc crc;
	crc.init();
	for (int i = 0; i < ft_count; i++)
		crc.do_block(ft_name[i], strlen(ft_name[i]));
	crc.do_block(probe_version, strlen(probe_version));
	return crc.get_crc();
}

static int write_int(Cfile32& f, int32_t v)
{
  return f.write(data_ref(&v, sizeof(v)));
}

static int write_byte(Cfile32& f, uint8_t v)
{
	return f.write(data_ref(&v, sizeof(v)));
}

int mix_cache::load()
{
	Ccc_file f(true);
	if (f.open(get_fname()) || f.get_size() < 8)
		return 1;
	const byte* s = f.data();
	const byte* end = s + f.get_size();
	if (*reinterpret_cast<const int*>(s) != get_ft_crc())
		return 0;
	s += 4;
	int count = *reinterpret_cast<const int*>(s);
	s += 4;
	while (count-- && s + 8 <= end)
	{
		int crc = *reinterpret_cast<const int*>(s);
		s += 4;
		int ft_size = *reinterpret_cast<const int*>(s);
		s += 4;
		if (s + 1 + 4 > end)
			break;
		Entry e;
		e.game = *s;
		s += 1;
		int c_lmd = *reinterpret_cast<const int*>(s);
		s += 4;
		if (c_lmd < 0 || s + static_cast<ptrdiff_t>(c_lmd) * 4 + ft_size > end)
			break;
		e.lmd.resize(c_lmd);
		if (c_lmd)
			memcpy(e.lmd.data(), s, c_lmd * 4);
		s += c_lmd * 4;
		e.ft = Cvirtual_binary(s, ft_size);
		s += ft_size;
		cache[crc] = std::move(e);
	}
	return 0;
}

int mix_cache::save()
{
	Cfile32 f;
	if (f.open(get_fname(), GENERIC_WRITE))
		return 1;
	write_int(f, get_ft_crc());
	write_int(f, static_cast<int32_t>(cache.size()));
	for (auto& i : cache)
	{
		const Entry& e = i.second;
		write_int(f, i.first);
		write_int(f, e.ft.size());
		write_byte(f, e.game);
		write_int(f, static_cast<int32_t>(e.lmd.size()));
		if (!e.lmd.empty())
			f.write(data_ref(e.lmd.data(), static_cast<int>(e.lmd.size() * sizeof(int))));
		f.write(e.ft);
	}
	return 0;
}

const mix_cache::Entry* mix_cache::get_entry(int crc)
{
	auto i = cache.find(crc);
	return i == cache.end() ? nullptr : &i->second;
}

void mix_cache::set_entry(int crc, Entry e)
{
	cache[crc] = std::move(e);
}

Cvirtual_binary mix_cache::get_data(int crc)
{
	auto i = cache.find(crc);
	return i == cache.end() ? Cvirtual_binary() : i->second.ft;
}

void mix_cache::set_data(int crc, const Cvirtual_binary& v)
{
	cache[crc].ft = v;
}
