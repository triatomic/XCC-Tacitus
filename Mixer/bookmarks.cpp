#include "stdafx.h"
#include "bookmarks.h"

#include <algorithm>

namespace
{
	constexpr int k_hard_cap = 256;
	constexpr const char* k_section = "Bookmarks";

	// Highest slot count written this session. persist() clears any trailing
	// slots between the new count and this so a remove/clear doesn't strand old
	// entries in the INI (which load() would otherwise resurrect on restart).
	int g_persisted = 0;

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

	bool same_path(const std::string& a, const std::string& b)
	{
		return _stricmp(a.c_str(), b.c_str()) == 0;
	}

	void persist()
	{
		CWinApp* a = AfxGetApp();
		if (!a)
			return;
		const auto& v = mut_list();
		int n = static_cast<int>(v.size());
		int upto = n > g_persisted ? n : g_persisted;
		if (upto > k_hard_cap) upto = k_hard_cap;
		for (int i = 0; i < upto; i++)
		{
			CString val = (i < n) ? CString(v[i].c_str()) : CString();
			a->WriteProfileString(k_section, CString(key_for(i).c_str()), val);
		}
		g_persisted = n;
	}
}

int bookmarks::hard_cap()
{
	return k_hard_cap;
}

void bookmarks::load()
{
	auto& v = mut_list();
	v.clear();
	g_persisted = 0;
	CWinApp* a = AfxGetApp();
	if (!a)
		return;
	int high = 0;
	for (int i = 0; i < k_hard_cap; i++)
	{
		CString s = a->GetProfileString(k_section, CString(key_for(i).c_str()), "");
		if (s.IsEmpty())
			continue;   // tolerate a hand-edited gap; load() compacts on next persist
		v.push_back(static_cast<const char*>(s));
		high = i + 1;
	}
	g_persisted = high;
}

bool bookmarks::add(const std::string& path)
{
	if (path.empty())
		return false;
	auto& v = mut_list();
	for (const auto& s : v)
		if (same_path(s, path))
			return false;
	if (static_cast<int>(v.size()) >= k_hard_cap)
		return false;
	v.push_back(path);
	persist();
	return true;
}

void bookmarks::remove(const std::string& path)
{
	auto& v = mut_list();
	auto it = std::remove_if(v.begin(), v.end(),
		[&](const std::string& s) { return same_path(s, path); });
	if (it != v.end())
	{
		v.erase(it, v.end());
		persist();
	}
}

bool bookmarks::contains(const std::string& path)
{
	for (const auto& s : mut_list())
		if (same_path(s, path))
			return true;
	return false;
}

const std::vector<std::string>& bookmarks::list()
{
	return mut_list();
}

void bookmarks::clear()
{
	mut_list().clear();
	persist();
}
