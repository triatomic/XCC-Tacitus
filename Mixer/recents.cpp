#include "stdafx.h"
#include "recents.h"

#include <algorithm>

namespace
{
	constexpr int k_default = 20;
	constexpr int k_hard_cap = 256;
	constexpr int k_floor = 1;
	constexpr const char* k_section = "Recents";
	constexpr const char* k_max_key = "max_items";

	// Cached cap (clamped). Read from INI on load(); writable via set_max_items()
	// if we ever add a UI for it — for now the user edits the INI by hand.
	int g_max = k_default;

	std::vector<std::string>& mut_list()
	{
		static std::vector<std::string> g_list;
		return g_list;
	}

	std::string key_for(int i)
	{
		char buf[16];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "path%d", i);
		return buf;
	}

	int clamp_cap(int v)
	{
		if (v < k_floor) return k_floor;
		if (v > k_hard_cap) return k_hard_cap;
		return v;
	}

	void persist()
	{
		CWinApp* a = AfxGetApp();
		if (!a)
			return;
		const auto& v = mut_list();
		// Write current slots; clear one past the end so old entries from a
		// larger cap don't linger in the INI when the user shrinks max_items.
		int n = static_cast<int>(v.size());
		int write_through = n + 1;
		if (write_through > k_hard_cap) write_through = k_hard_cap;
		for (int i = 0; i < write_through; i++)
		{
			CString val = (i < n) ? CString(v[i].c_str()) : CString();
			a->WriteProfileString(k_section, CString(key_for(i).c_str()), val);
		}
	}

	bool same_path(const std::string& a, const std::string& b)
	{
		return _stricmp(a.c_str(), b.c_str()) == 0;
	}
}

int recents::max_items()
{
	return g_max;
}

int recents::hard_cap()
{
	return k_hard_cap;
}

void recents::load()
{
	CWinApp* a = AfxGetApp();
	auto& v = mut_list();
	v.clear();
	g_max = k_default;
	if (!a)
		return;
	g_max = clamp_cap(a->GetProfileInt(k_section, k_max_key, k_default));
	// Seed the key so users can discover/edit it without guessing.
	a->WriteProfileInt(k_section, k_max_key, g_max);
	// Scan up to the hard cap so shrinking max_items doesn't lose entries
	// stored under higher indices from a previous larger setting.
	for (int i = 0; i < k_hard_cap; i++)
	{
		CString s = a->GetProfileString(k_section, CString(key_for(i).c_str()), "");
		if (s.IsEmpty())
			continue;
		v.push_back(static_cast<const char*>(s));
		if (static_cast<int>(v.size()) >= g_max)
			break;
	}
}

void recents::push(const std::string& path)
{
	if (path.empty())
		return;
	auto& v = mut_list();
	v.erase(std::remove_if(v.begin(), v.end(),
		[&](const std::string& s) { return same_path(s, path); }), v.end());
	v.insert(v.begin(), path);
	if (static_cast<int>(v.size()) > g_max)
		v.resize(g_max);
	persist();
}

const std::vector<std::string>& recents::list()
{
	return mut_list();
}

void recents::clear()
{
	mut_list().clear();
	persist();
}
