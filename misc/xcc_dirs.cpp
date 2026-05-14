#include "xcc_dirs.h"

#include <cassert>
#include <map>
#include <windows.h>
#include "reg_key.h"
#include "string_conversion.h"
#include "xcc_registry.h"

bool g_enable_log = false;
static string dune2_dir;
static string td_primary_dir;
static string td_secondary_dir;
static string cd_dir;
static string data_dir;
static string ra_dir;
static string dune2000_dir;
static string ts_dir;
static string ra2_dir;
static string nox_dir;
static string rg_dir;
static string gr_dir;
static string gr_zh_dir;
static string ebfd_dir;
static string bfme_dir;
static string tw_dir;

bool xcc_dirs::enable_log()
{
	return g_enable_log;
}

string xcc_dirs::get_dir(t_game game)
{
	switch (game)
	{
	case game_td:
		return td_primary_dir;
	case game_ra:
		return ra_dir;
	case game_ts:
	case game_ts_fs:
		return ts_dir;
	case game_dune2:
		return dune2_dir;
	case game_dune2000:
		return dune2000_dir;
	case game_ra2:
	case game_ra2_yr:
		return ra2_dir;
	case game_nox:
		return nox_dir;
	case game_rg:
		return rg_dir;
	case game_gr:
		return gr_dir;
	case game_gr_zh:
		return gr_zh_dir;
	case game_ebfd:
		return ebfd_dir;
	case game_bfme:
		return bfme_dir;
	case game_tw:
		return tw_dir;
	}
	// Sentinel m_game = -1 (no game selected yet) takes this path during
	// startup; release builds already silently returned "" here, debug used
	// to assert. Match release behavior so the Debug config doesn't trip
	// at startup before the user has picked a game.
	return "";
}

string xcc_dirs::get_audio_mix(t_game game)
{
	switch (game)
	{
	case game_ra2:
		return "audio.mix";
	case game_ra2_yr:
		return "audiomd.mix";
	}
	assert(false);
	return "";
}

string xcc_dirs::get_csf_fname(t_game game)
{
	switch (game)
	{
	case game_ra2:
		return "ra2.csf";
	case game_ra2_yr:
		return "ra2md.csf";
	case game_gr:
	case game_gr_zh:
		return "data/english/generals.csf";
	}
	assert(false);
	return "";
}

string xcc_dirs::get_language_mix(t_game game)
{
	switch (game)
	{
	case game_ra2:
		return ra2_dir + "language.mix";
	case game_ra2_yr:
		return ra2_dir + "langmd.mix";
	case game_gr:
		return gr_dir + "english.big";
	case game_gr_zh:
		return gr_zh_dir + "englishzh.big";
	}
	assert(false);
	return "";
}

string xcc_dirs::get_local_mix(t_game game)
{
	switch (game)
	{
	case game_ts:
	case game_ra2:
		return "local.mix";
	case game_ra2_yr:
		return "localmd.mix";
	}
	assert(false);
	return "";
}

string xcc_dirs::get_main_mix(t_game game)
{
	switch (game)
	{
	case game_ra:
		return ra_dir + "redalert.mix";
	case game_ts:
		return ts_dir + "tibsun.mix";
	case game_ra2:
		return ra2_dir + "ra2.mix";
	case game_ra2_yr:
		return ra2_dir + "ra2md.mix";
	}
	assert(false);
	return "";
}

static void set_path(string s, string& path)
{
	s = to_lower(s);
	if (!s.empty() && s.back() != '\\')
		s += '\\';	
	path = s;
}

void xcc_dirs::set_dir(t_game game, const string &s)
{
	switch (game)
	{
	case game_td:
		set_path(s, td_primary_dir);
		break;
	case game_ra:
		set_path(s, ra_dir);
		break;
	case game_ts:
		set_path(s, ts_dir);
		break;
	case game_dune2:
		set_path(s, dune2_dir);
		break;
	case game_dune2000:
		set_path(s, dune2000_dir);
		break;
	case game_ra2:
		set_path(s, ra2_dir);
		break;
	case game_nox:
		set_path(s, nox_dir);
		break;
	case game_rg:
		set_path(s, rg_dir);
		break;
	case game_gr:
		set_path(s, gr_dir);
		break;
	case game_gr_zh:
		set_path(s, gr_zh_dir);
		break;
	case game_ebfd:
		set_path(s, ebfd_dir);
		break;
	case game_bfme:
		set_path(s, bfme_dir);
		break;
	case game_tw:
		set_path(s, tw_dir);
		break;
	default:
		assert(false);
	}
}

void xcc_dirs::set_td_secondary_dir(const string& s)
{
	set_path(s, td_secondary_dir);
}

void xcc_dirs::set_cd_dir(const string& s)
{
	set_path(s, cd_dir);
}

void xcc_dirs::set_data_dir(const string& s)
{
	set_path(s, data_dir);
}

void xcc_dirs::reset_cd_dir()
{
	int drive_map = GetLogicalDrives();
	char drive_root[] = "a:\\";
	for (int i = 0; i < 26; i++)
	{		
		if (drive_map >> i & 1 && GetDriveTypeA(drive_root) == DRIVE_CDROM)
		{
			set_cd_dir(drive_root);
			break;
		}
		drive_root[0]++;
	}
}

void xcc_dirs::reset_data_dir()
{
	set_data_dir(GetModuleFileName().get_path());
}

static std::map<int, std::vector<xcc_dirs::t_detected_source>> g_detected;

static void record_source(t_game game, xcc_dirs::t_dir_source kind, const string& path, const string& label)
{
	auto& v = g_detected[game];
	for (auto& e : v)
		if (e.path == path)
			return;
	v.push_back({kind, path, label});
}

static void read_dir(const string& key, const string& value, t_game game)
{
	Creg_key h;
	string s;
	if (ERROR_SUCCESS == h.open(HKEY_LOCAL_MACHINE, key, KEY_QUERY_VALUE)
		&& ERROR_SUCCESS == h.query_value(value, s))
	{
		string path = Cfname(s).get_path();
		record_source(game, xcc_dirs::src_retail, path, "Retail");
		if (xcc_dirs::get_dir(game).empty())
			xcc_dirs::set_dir(game, path);
	}
}

static bool dir_exists(const string& path)
{
	DWORD attr = GetFileAttributesA(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static string normalize_slashes(string s)
{
	for (auto& c : s) if (c == '/') c = '\\';
	if (!s.empty() && s.back() != '\\') s += '\\';
	return s;
}

static std::vector<string> parse_libraryfolders_vdf(const string& vdf_path)
{
	std::vector<string> roots;
	HANDLE h = CreateFileA(vdf_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return roots;
	DWORD size = GetFileSize(h, NULL);
	if (size == 0 || size > 1u << 20)
	{
		CloseHandle(h);
		return roots;
	}
	std::string buf;
	buf.resize(size);
	DWORD read = 0;
	if (!ReadFile(h, &buf[0], size, &read, NULL))
	{
		CloseHandle(h);
		return roots;
	}
	CloseHandle(h);
	// Extract every "path" "<value>" pair (case-sensitive, Steam writes lowercase "path").
	const char* needle = "\"path\"";
	size_t i = 0;
	while ((i = buf.find(needle, i)) != string::npos)
	{
		i += 6;
		size_t q1 = buf.find('"', i);
		if (q1 == string::npos) break;
		size_t q2 = buf.find('"', q1 + 1);
		if (q2 == string::npos) break;
		string raw = buf.substr(q1 + 1, q2 - q1 - 1);
		// VDF escapes backslashes as \\; collapse them.
		string p;
		p.reserve(raw.size());
		for (size_t k = 0; k < raw.size(); ++k)
		{
			if (raw[k] == '\\' && k + 1 < raw.size() && raw[k + 1] == '\\') { p += '\\'; ++k; }
			else p += raw[k];
		}
		roots.push_back(normalize_slashes(p));
		i = q2 + 1;
	}
	return roots;
}

static std::vector<string> discover_steam_libraries()
{
	std::vector<string> libs;
	Creg_key h;
	string steam_path;
	if (ERROR_SUCCESS != h.open(HKEY_CURRENT_USER, "Software\\Valve\\Steam", KEY_QUERY_VALUE))
		return libs;
	if (ERROR_SUCCESS != h.query_value("SteamPath", steam_path) || steam_path.empty())
		return libs;
	steam_path = normalize_slashes(steam_path);
	libs.push_back(steam_path);
	auto extra = parse_libraryfolders_vdf(steam_path + "steamapps\\libraryfolders.vdf");
	for (auto& r : extra)
	{
		bool dup = false;
		for (auto& e : libs) if (_stricmp(e.c_str(), r.c_str()) == 0) { dup = true; break; }
		if (!dup) libs.push_back(r);
	}
	return libs;
}

struct t_steam_app
{
	t_game game;
	const char* installdir;
};

// Per-app installdir as set by EA. Stable across user "move install" actions.
// Sources: SteamDB + verified appmanifest_2229850.acf / appmanifest_2229890.acf
// on a real install (2026-05-14). Note "Command and Conquer Generals" uses the
// spelled-out 'and' instead of '&' — that is EA's inconsistency, not a typo.
static const t_steam_app g_steam_apps[] =
{
	{ game_td,      "Command & Conquer" },
	{ game_ra,      "Command & Conquer Red Alert" },
	{ game_ts,      "Command & Conquer Tiberian Sun" },
	{ game_ra2,     "Command & Conquer Red Alert II" },
	{ game_rg,      "Command & Conquer Renegade" },
	{ game_gr,      "Command and Conquer Generals" },
	{ game_gr_zh,   "Command & Conquer Generals - Zero Hour" },
	{ game_tw,      "Command & Conquer 3 Tiberium Wars" },
};

static void detect_steam_sources()
{
	auto libs = discover_steam_libraries();
	if (libs.empty()) return;
	for (const auto& app : g_steam_apps)
	{
		for (const auto& lib : libs)
		{
			string p = lib + "steamapps\\common\\" + app.installdir + "\\";
			if (dir_exists(p))
			{
				record_source(app.game, xcc_dirs::src_steam, p, "Steam");
				if (xcc_dirs::get_dir(app.game).empty())
					xcc_dirs::set_dir(app.game, p);
				break;
			}
		}
	}
}

const std::vector<xcc_dirs::t_detected_source>& xcc_dirs::get_detected_sources(t_game game)
{
	return g_detected[game];
}

void xcc_dirs::load_from_registry()
{

	Creg_key kh_base;
	if (!Cxcc_registry::get_base_key(kh_base))
	{
		string s;
		if (ERROR_SUCCESS == kh_base.query_value("dune2_dir", s))
			set_dir(game_dune2, s);
		if (ERROR_SUCCESS == kh_base.query_value("dir1", s))
			set_dir(game_td, s);
		if (ERROR_SUCCESS == kh_base.query_value("dir2", s))
			set_td_secondary_dir(s);
		if (ERROR_SUCCESS == kh_base.query_value("ra_dir", s))
			set_dir(game_ra, s);
		if (ERROR_SUCCESS == kh_base.query_value("ra2_dir", s))
			set_dir(game_ra2, s);
		if (ERROR_SUCCESS == kh_base.query_value("ts_dir", s))
			set_dir(game_ts, s);
		if (ERROR_SUCCESS == kh_base.query_value("dune2000_dir", s))
			set_dir(game_dune2000, s);
		if (ERROR_SUCCESS == kh_base.query_value("nox_dir", s))
			set_dir(game_nox, s);
		if (ERROR_SUCCESS == kh_base.query_value("rg_dir", s))
			set_dir(game_rg, s);
		if (ERROR_SUCCESS == kh_base.query_value("ebfd_dir", s))
			set_dir(game_ebfd, s);
		if (ERROR_SUCCESS == kh_base.query_value("gr_dir", s))
			set_dir(game_gr, s);
		if (ERROR_SUCCESS == kh_base.query_value("gr_zh_dir", s))
			set_dir(game_gr_zh, s);
		if (ERROR_SUCCESS == kh_base.query_value("bfme_dir", s))
			set_dir(game_bfme, s);
		if (ERROR_SUCCESS == kh_base.query_value("tw_dir", s))
			set_dir(game_tw, s);

		if (ERROR_SUCCESS == kh_base.query_value("cd_dir", s))
			set_cd_dir(s);
		if (ERROR_SUCCESS == kh_base.query_value("data_dir", s))
			set_data_dir(s);

		if (ERROR_SUCCESS == kh_base.query_value("enable_log", s))
			g_enable_log = true;
	}
	if (cd_dir.empty())
		reset_cd_dir();
	if (data_dir.empty())
		reset_data_dir();
	read_dir("Software\\Westwood\\Dune 2", "InstallPath", game_dune2);
	read_dir("Software\\Westwood\\Command & Conquer Windows 95 Edition", "InstallPath", game_td);
	read_dir("Software\\Westwood\\Red Alert Windows 95 Edition", "InstallPath", game_ra);
	read_dir("Software\\Westwood\\Dune 2000", "InstallPath", game_dune2000);
	read_dir("Software\\Westwood\\Tiberian Sun", "InstallPath", game_ts);
	read_dir("Software\\Westwood\\Red Alert 2", "InstallPath", game_ra2);
	read_dir("Software\\Westwood\\Nox", "InstallPath", game_nox);
	read_dir("Software\\Westwood\\Renegade", "InstallPath", game_rg);
	read_dir("Software\\Westwood\\Emperor", "InstallPath", game_ebfd);
	/*
	read_dir("Software\\Electronic Arts\\EA Games\\Generals", "InstallPath", game_gr);
	read_dir("Software\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", "InstallPath", game_gr_zh);
	read_dir("Software\\Electronic Arts\\EA Games\\The Battle for Middle-earth", "InstallPath", game_bfme);
	read_dir("Software\\Electronic Arts\\Electronic Arts\\Command and Conquer 3", "InstallPath", game_tw);
	*/
	detect_steam_sources();
}

void xcc_dirs::save_to_registry()
{
	Creg_key kh_base;
	if (Cxcc_registry::get_base_key(kh_base))
		return;
	kh_base.set_value("dune2_dir", dune2_dir);
	kh_base.set_value("dir1", td_primary_dir);
	kh_base.set_value("dir2", td_secondary_dir);
	kh_base.set_value("ra_dir", ra_dir);
	kh_base.set_value("dune2000_dir", dune2000_dir);
	kh_base.set_value("ts_dir", ts_dir);
	kh_base.set_value("ra2_dir", ra2_dir);
	kh_base.set_value("rg_dir", rg_dir);
	kh_base.set_value("gr_dir", gr_dir);
	kh_base.set_value("gr_zh_dir", gr_zh_dir);
	kh_base.set_value("nox_dir", nox_dir);
	kh_base.set_value("ebfd_dir", ebfd_dir);
	kh_base.set_value("bfme_dir", bfme_dir);
	kh_base.set_value("tw_dir", tw_dir);

	kh_base.set_value("cd_dir", cd_dir);
};

const string& xcc_dirs::get_td_secondary_dir()
{
	return td_secondary_dir;
}

const string& xcc_dirs::get_cd_dir()
{
	return cd_dir;
}

const string& xcc_dirs::get_data_dir()
{
	return data_dir;
}
