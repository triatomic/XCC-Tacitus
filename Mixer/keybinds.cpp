#include "stdafx.h"
#include "keybinds.h"
#include "resource.h"

namespace keybinds
{
	namespace
	{
		// FVIRTKEY is added implicitly at HACCEL build time; store mods without it.
		// Each row carries an optional keyboard slot (vk + key_mods) and an
		// optional mouse slot (btn + mouse_mods). Either or both may be set.
		const Binding g_defaults[] =
		{
			// scope_accel
			{ "edit_select_all",          "Select All",              scope_accel, ID_EDIT_SELECT_ALL,           'A',         FCONTROL,           mb_none, 0 },
			{ "edit_copy",                "Copy",                    scope_accel, ID_EDIT_COPY,                 'C',         FCONTROL,           mb_none, 0 },
			{ "file_search_in_mix",       "Find here...",            scope_accel, ID_FILE_SEARCH_IN_MIX,        'F',         FCONTROL,           mb_none, 0 },
			{ "file_search",              "Find everywhere...",      scope_accel, ID_FILE_SEARCH,               'F',         FCONTROL | FSHIFT,  mb_none, 0 },
			{ "file_new",                 "New Archive...",          scope_accel, ID_FILE_NEW,                  'N',         FCONTROL,           mb_none, 0 },
			{ "file_open",                "Open...",                 scope_accel, ID_FILE_OPEN,                 'O',         FCONTROL,           mb_none, 0 },
			{ "palette_select",           "Palette: Select...",      scope_accel, ID_VIEW_PALETTE_SELECT,       'R',         FCONTROL,           mb_none, 0 },
			{ "palette_auto_select",      "Palette: Auto select",    scope_accel, ID_VIEW_PALETTE_AUTO_SELECT,  'Q',         FCONTROL,           mb_none, 0 },
			{ "palette_prev",             "Palette: Previous",       scope_accel, ID_VIEW_PALETTE_PREV_SIBLING, VK_OEM_4,    FCONTROL,           mb_none, 0 },
			{ "palette_next",             "Palette: Next",           scope_accel, ID_VIEW_PALETTE_NEXT_SIBLING, VK_OEM_6,    FCONTROL,           mb_none, 0 },
			{ "file_close",               "Close",                   scope_accel, ID_FILE_CLOSE,                VK_BACK,     0,                  mb_none, 0 },
			{ "popup_delete",             "Delete",                  scope_accel, ID_POPUP_DELETE,              VK_DELETE,   0,                  mb_none, 0 },
			{ "popup_refresh",            "Refresh",                 scope_accel, ID_POPUP_REFRESH,             VK_F5,       0,                  mb_none, 0 },
			{ "popup_open",               "Open (Enter)",            scope_accel, ID_POPUP_OPEN,                VK_RETURN,   0,                  mb_none, 0 },
			{ "theme_light",              "Theme: Light",            scope_accel, ID_THEME_LIGHT,               '1',         FCONTROL,           mb_none, 0 },
			{ "theme_dark",               "Theme: Dark",             scope_accel, ID_THEME_DARK,                '2',         FCONTROL,           mb_none, 0 },
			{ "theme_vxl_lighting",       "VXL Lighting...",         scope_accel, ID_THEME_VXL_LIGHTING,        'L',         FCONTROL,           mb_none, 0 },

			// scope_file_view: keyboard-only actions
			{ "view_alpha_toggle",        "Toggle alpha-only view",  scope_file_view, vact_alpha_toggle,    'M',         0,                  mb_none, 0 },
			{ "view_player_toggle",       "Player: enter/exit",      scope_file_view, vact_player_toggle,   'P',         0,                  mb_none, 0 },
			{ "view_player_prev",         "Player: previous frame",  scope_file_view, vact_player_prev,     VK_LEFT,     0,                  mb_none, 0 },
			{ "view_player_next",         "Player: next frame",      scope_file_view, vact_player_next,     VK_RIGHT,    0,                  mb_none, 0 },
			{ "view_player_space",        "Player: play/pause",      scope_file_view, vact_player_space,    VK_SPACE,    0,                  mb_none, 0 },
			{ "view_zoom_100",            "Zoom: 100%",              scope_file_view, vact_zoom_100,        '0',         FCONTROL,           mb_none, 0 },

			// scope_file_view: mouse-driven (dual-bindable to keys)
			{ "view_zoom_in",             "Zoom: in",                scope_file_view, vact_zoom_in,         0,           0,                  mb_wheel_up,   FCONTROL },
			{ "view_zoom_out",            "Zoom: out",               scope_file_view, vact_zoom_out,        0,           0,                  mb_wheel_down, FCONTROL },
			{ "view_orbit_drag",          "VXL orbit drag",          scope_file_view, vact_orbit_drag,      0,           0,                  mb_left,       0 },
			{ "view_pan_drag",            "Player pan drag",         scope_file_view, vact_pan_drag,        0,           0,                  mb_right,      0 },

			// scope_list_view
			{ "list_play_audio",          "Play/pause audio",        scope_list_view, vact_play_audio,      VK_SPACE,    0,                  mb_none, 0 },
		};

		std::vector<Binding> g_defaults_vec;
		std::vector<Binding> g_current;

		void ensure_init()
		{
			if (!g_defaults_vec.empty())
				return;
			const int n = sizeof(g_defaults) / sizeof(g_defaults[0]);
			g_defaults_vec.assign(g_defaults, g_defaults + n);
			g_current = g_defaults_vec;
		}

		DWORD pack_key(BYTE vk, BYTE mods)  { return (DWORD)vk | ((DWORD)mods << 8); }
		DWORD pack_mouse(BYTE btn, BYTE mods) { return (DWORD)btn | ((DWORD)mods << 8); }

		BYTE only_relevant_mods(BYTE m) { return (BYTE)(m & (FCONTROL | FSHIFT | FALT)); }
	}

	const std::vector<Binding>& defaults() { ensure_init(); return g_defaults_vec; }
	const std::vector<Binding>& current()  { ensure_init(); return g_current; }

	void set_key(int index, BYTE vk, BYTE mods)
	{
		ensure_init();
		if (index < 0 || index >= (int)g_current.size()) return;
		g_current[index].vk = vk;
		g_current[index].key_mods = only_relevant_mods(mods);
	}

	void set_mouse(int index, BYTE btn, BYTE mods)
	{
		ensure_init();
		if (index < 0 || index >= (int)g_current.size()) return;
		g_current[index].btn = btn;
		g_current[index].mouse_mods = only_relevant_mods(mods);
	}

	void clear_key(int index)   { set_key(index, 0, 0); }
	void clear_mouse(int index) { set_mouse(index, mb_none, 0); }

	void reset_binding(int index)
	{
		ensure_init();
		if (index < 0 || index >= (int)g_current.size()) return;
		g_current[index].vk         = g_defaults_vec[index].vk;
		g_current[index].key_mods   = g_defaults_vec[index].key_mods;
		g_current[index].btn        = g_defaults_vec[index].btn;
		g_current[index].mouse_mods = g_defaults_vec[index].mouse_mods;
	}

	void reset_all()
	{
		ensure_init();
		g_current = g_defaults_vec;
	}

	void load_from_registry()
	{
		ensure_init();
		CWinApp* app = AfxGetApp();
		for (size_t i = 0; i < g_current.size(); i++)
		{
			std::string base = g_current[i].name;
			std::string k_name = base + "_key";
			std::string m_name = base + "_mouse";

			// Defaults rendered the same way the dialog/INI show them, so when
			// the user hasn't touched a binding the INI line round-trips
			// cleanly ("file_search_in_mix_key=Ctrl+F", not packed ints).
			std::string k_def = format_shortcut(g_defaults_vec[i].vk, g_defaults_vec[i].key_mods);
			std::string m_def = format_mouse(g_defaults_vec[i].btn,  g_defaults_vec[i].mouse_mods);

			CString k_v = app->GetProfileString("Keybinds", k_name.c_str(), k_def.c_str());
			CString m_v = app->GetProfileString("Keybinds", m_name.c_str(), m_def.c_str());

			BYTE vk = 0, km = 0, btn = 0, mm = 0;
			if (!parse_shortcut(std::string(k_v), vk, km))
			{
				vk = g_defaults_vec[i].vk;
				km = g_defaults_vec[i].key_mods;
			}
			if (!parse_mouse(std::string(m_v), btn, mm))
			{
				btn = g_defaults_vec[i].btn;
				mm  = g_defaults_vec[i].mouse_mods;
			}
			g_current[i].vk         = vk;
			g_current[i].key_mods   = only_relevant_mods(km);
			g_current[i].btn        = btn;
			g_current[i].mouse_mods = only_relevant_mods(mm);
		}
	}

	void save_to_registry()
	{
		ensure_init();
		CWinApp* app = AfxGetApp();
		for (size_t i = 0; i < g_current.size(); i++)
		{
			std::string base = g_current[i].name;
			std::string k_name = base + "_key";
			std::string m_name = base + "_mouse";
			std::string k_v = format_shortcut(g_current[i].vk, g_current[i].key_mods);
			std::string m_v = format_mouse(g_current[i].btn,  g_current[i].mouse_mods);
			app->WriteProfileString("Keybinds", k_name.c_str(), k_v.c_str());
			app->WriteProfileString("Keybinds", m_name.c_str(), m_v.c_str());
		}
	}

	HACCEL build_accel_table()
	{
		ensure_init();
		std::vector<ACCEL> a;
		a.reserve(g_current.size());
		for (const Binding& b : g_current)
		{
			if (b.scope != scope_accel) continue;
			if (b.vk == 0) continue;
			ACCEL e = {};
			e.fVirt = b.key_mods | FVIRTKEY;
			e.key = b.vk;
			e.cmd = (WORD)b.action;
			a.push_back(e);
		}
		if (a.empty()) return NULL;
		return CreateAcceleratorTable(a.data(), (int)a.size());
	}

	bool match_view(EScope scope, UINT vk, bool ctrl, bool shift, bool alt, UINT& out_action)
	{
		ensure_init();
		BYTE mods = 0;
		if (ctrl) mods |= FCONTROL;
		if (shift) mods |= FSHIFT;
		if (alt) mods |= FALT;
		for (const Binding& b : g_current)
		{
			if (b.scope != scope) continue;
			if (b.vk == 0) continue;
			if (b.vk == vk && b.key_mods == mods)
			{
				out_action = b.action;
				return true;
			}
			// vact_zoom_100 historically also matches VK_NUMPAD0 when the
			// primary key is '0'. Preserve that convenience.
			if (b.action == vact_zoom_100 && vk == VK_NUMPAD0 && b.vk == '0' && b.key_mods == mods)
			{
				out_action = b.action;
				return true;
			}
		}
		return false;
	}

	bool match_mouse(EScope scope, BYTE btn, bool ctrl, bool shift, bool alt, UINT& out_action)
	{
		ensure_init();
		BYTE mods = 0;
		if (ctrl) mods |= FCONTROL;
		if (shift) mods |= FSHIFT;
		if (alt) mods |= FALT;
		for (const Binding& b : g_current)
		{
			if (b.scope != scope) continue;
			if (b.btn == mb_none) continue;
			if (b.btn == btn && b.mouse_mods == mods)
			{
				out_action = b.action;
				return true;
			}
		}
		return false;
	}

	std::string format_shortcut(BYTE vk, BYTE mods)
	{
		if (vk == 0) return "";
		std::string s;
		if (mods & FCONTROL) s += "Ctrl+";
		if (mods & FSHIFT)   s += "Shift+";
		if (mods & FALT)     s += "Alt+";

		switch (vk)
		{
		case VK_BACK:    s += "Backspace"; return s;
		case VK_TAB:     s += "Tab"; return s;
		case VK_RETURN:  s += "Enter"; return s;
		case VK_ESCAPE:  s += "Esc"; return s;
		case VK_SPACE:   s += "Space"; return s;
		case VK_DELETE:  s += "Del"; return s;
		case VK_INSERT:  s += "Ins"; return s;
		case VK_HOME:    s += "Home"; return s;
		case VK_END:     s += "End"; return s;
		case VK_PRIOR:   s += "PgUp"; return s;
		case VK_NEXT:    s += "PgDn"; return s;
		case VK_LEFT:    s += "Left"; return s;
		case VK_RIGHT:   s += "Right"; return s;
		case VK_UP:      s += "Up"; return s;
		case VK_DOWN:    s += "Down"; return s;
		case VK_OEM_4:   s += "["; return s;
		case VK_OEM_6:   s += "]"; return s;
		case VK_OEM_1:   s += ";"; return s;
		case VK_OEM_2:   s += "/"; return s;
		case VK_OEM_3:   s += "`"; return s;
		case VK_OEM_5:   s += "\\"; return s;
		case VK_OEM_7:   s += "'"; return s;
		case VK_OEM_COMMA:  s += ","; return s;
		case VK_OEM_PERIOD: s += "."; return s;
		case VK_OEM_MINUS:  s += "-"; return s;
		case VK_OEM_PLUS:   s += "="; return s;
		}
		if (vk >= VK_F1 && vk <= VK_F24)
		{
			char buf[8];
			_snprintf_s(buf, _TRUNCATE, "F%d", vk - VK_F1 + 1);
			s += buf;
			return s;
		}
		if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
		{
			char buf[12];
			_snprintf_s(buf, _TRUNCATE, "Num%d", vk - VK_NUMPAD0);
			s += buf;
			return s;
		}
		if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
		{
			s += (char)vk;
			return s;
		}
		char buf[16];
		_snprintf_s(buf, _TRUNCATE, "VK_%02X", vk);
		s += buf;
		return s;
	}

	std::string format_mouse(BYTE btn, BYTE mods)
	{
		if (btn == mb_none) return "";
		std::string s;
		if (mods & FCONTROL) s += "Ctrl+";
		if (mods & FSHIFT)   s += "Shift+";
		if (mods & FALT)     s += "Alt+";
		switch (btn)
		{
		case mb_left:       s += "LeftDrag"; break;
		case mb_right:      s += "RightDrag"; break;
		case mb_middle:     s += "MiddleClick"; break;
		case mb_x1:         s += "Mouse4"; break;
		case mb_x2:         s += "Mouse5"; break;
		case mb_wheel_up:   s += "WheelUp"; break;
		case mb_wheel_down: s += "WheelDown"; break;
		default: s += "?"; break;
		}
		return s;
	}

	std::string shortcut_for_command(UINT cmd)
	{
		ensure_init();
		for (const Binding& b : g_current)
		{
			if (b.scope == scope_accel && b.action == cmd)
				return format_shortcut(b.vk, b.key_mods);
		}
		return "";
	}

	std::string scope_name(EScope s)
	{
		switch (s)
		{
		case scope_accel: return "Menu";
		case scope_file_view: return "File view";
		case scope_list_view: return "List view";
		}
		return "";
	}

	namespace
	{
		std::string trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && (unsigned char)s[a] <= ' ') a++;
			while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
			return s.substr(a, b - a);
		}

		std::string to_lower(const std::string& s)
		{
			std::string r = s;
			for (char& c : r) c = (char)::tolower((unsigned char)c);
			return r;
		}

		// Split "Ctrl+Shift+F" into modifier tokens + a final key token.
		// Trailing "+" is treated as the literal '+' key, since '+' is both
		// the separator and a valid key name. Same goes for "-".
		void tokenize(const std::string& in, std::vector<std::string>& out)
		{
			std::string cur;
			for (size_t i = 0; i < in.size(); i++)
			{
				char c = in[i];
				if (c == '+' && i + 1 < in.size())
				{
					out.push_back(trim(cur));
					cur.clear();
				}
				else
				{
					cur.push_back(c);
				}
			}
			if (!cur.empty())
				out.push_back(trim(cur));
		}

		// Returns true and applies the modifier flag, false if not a modifier.
		bool apply_mod_token(const std::string& tok, BYTE& mods)
		{
			std::string t = to_lower(tok);
			if (t == "ctrl" || t == "control") { mods |= FCONTROL; return true; }
			if (t == "shift")                  { mods |= FSHIFT;   return true; }
			if (t == "alt")                    { mods |= FALT;     return true; }
			return false;
		}

		// Reverse of format_shortcut's switch. Returns 0 on unknown token.
		BYTE parse_vk_token(const std::string& tok)
		{
			std::string t = to_lower(tok);
			if (t == "backspace") return VK_BACK;
			if (t == "tab")       return VK_TAB;
			if (t == "enter" || t == "return") return VK_RETURN;
			if (t == "esc" || t == "escape")   return VK_ESCAPE;
			if (t == "space")     return VK_SPACE;
			if (t == "del" || t == "delete")   return VK_DELETE;
			if (t == "ins" || t == "insert")   return VK_INSERT;
			if (t == "home")      return VK_HOME;
			if (t == "end")       return VK_END;
			if (t == "pgup" || t == "pageup")  return VK_PRIOR;
			if (t == "pgdn" || t == "pagedown")return VK_NEXT;
			if (t == "left")      return VK_LEFT;
			if (t == "right")     return VK_RIGHT;
			if (t == "up")        return VK_UP;
			if (t == "down")      return VK_DOWN;
			if (tok == "[")       return VK_OEM_4;
			if (tok == "]")       return VK_OEM_6;
			if (tok == ";")       return VK_OEM_1;
			if (tok == "/")       return VK_OEM_2;
			if (tok == "`")       return VK_OEM_3;
			if (tok == "\\")      return VK_OEM_5;
			if (tok == "'")       return VK_OEM_7;
			if (tok == ",")       return VK_OEM_COMMA;
			if (tok == ".")       return VK_OEM_PERIOD;
			if (tok == "-")       return VK_OEM_MINUS;
			if (tok == "=")       return VK_OEM_PLUS;
			// F1..F24
			if (t.size() >= 2 && t[0] == 'f')
			{
				int n = atoi(t.c_str() + 1);
				if (n >= 1 && n <= 24) return (BYTE)(VK_F1 + n - 1);
			}
			// Num0..Num9
			if (t.size() == 4 && t.compare(0, 3, "num") == 0)
			{
				char d = t[3];
				if (d >= '0' && d <= '9') return (BYTE)(VK_NUMPAD0 + (d - '0'));
			}
			// Plain letter or digit
			if (tok.size() == 1)
			{
				char c = tok[0];
				if (c >= 'a' && c <= 'z') return (BYTE)(c - 'a' + 'A');
				if (c >= 'A' && c <= 'Z') return (BYTE)c;
				if (c >= '0' && c <= '9') return (BYTE)c;
			}
			// "VK_XX" hex escape, for keys we don't name explicitly.
			if (t.size() == 5 && t.compare(0, 3, "vk_") == 0)
			{
				int v = (int)strtol(t.c_str() + 3, nullptr, 16);
				if (v > 0 && v < 256) return (BYTE)v;
			}
			return 0;
		}

		BYTE parse_btn_token(const std::string& tok)
		{
			std::string t = to_lower(tok);
			if (t == "leftdrag"   || t == "leftclick"  || t == "left")   return mb_left;
			if (t == "rightdrag"  || t == "rightclick" || t == "right")  return mb_right;
			if (t == "middleclick"|| t == "middledrag" || t == "middle") return mb_middle;
			if (t == "mouse4"     || t == "x1")                          return mb_x1;
			if (t == "mouse5"     || t == "x2")                          return mb_x2;
			if (t == "wheelup")                                          return mb_wheel_up;
			if (t == "wheeldown")                                        return mb_wheel_down;
			return mb_none;
		}
	}

	bool parse_shortcut(const std::string& s, BYTE& out_vk, BYTE& out_mods)
	{
		out_vk = 0;
		out_mods = 0;
		std::string trimmed = trim(s);
		if (trimmed.empty()) return true; // empty = explicit "no binding"
		std::vector<std::string> toks;
		tokenize(trimmed, toks);
		if (toks.empty()) return true;
		BYTE mods = 0;
		size_t i = 0;
		for (; i + 1 < toks.size(); i++)
		{
			if (!apply_mod_token(toks[i], mods))
				return false;
		}
		BYTE vk = parse_vk_token(toks.back());
		if (vk == 0) return false;
		out_vk = vk;
		out_mods = mods;
		return true;
	}

	bool parse_mouse(const std::string& s, BYTE& out_btn, BYTE& out_mods)
	{
		out_btn = mb_none;
		out_mods = 0;
		std::string trimmed = trim(s);
		if (trimmed.empty()) return true;
		std::vector<std::string> toks;
		tokenize(trimmed, toks);
		if (toks.empty()) return true;
		BYTE mods = 0;
		size_t i = 0;
		for (; i + 1 < toks.size(); i++)
		{
			if (!apply_mod_token(toks[i], mods))
				return false;
		}
		BYTE btn = parse_btn_token(toks.back());
		if (btn == mb_none) return false;
		out_btn = btn;
		out_mods = mods;
		return true;
	}
}
