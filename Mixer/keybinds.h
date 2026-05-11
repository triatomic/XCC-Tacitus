#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace keybinds
{
	enum EScope
	{
		scope_accel = 0,       // app-wide accelerator (translated to WM_COMMAND)
		scope_file_view = 1,   // CXCCFileView::OnKeyDown / mouse handlers
		scope_list_view = 2,   // CXCCMixerView::PreTranslateMessage
	};

	enum EViewAction
	{
		vact_none = 0,
		// keyboard / list-view
		vact_alpha_toggle,
		vact_player_toggle,
		vact_player_prev,
		vact_player_next,
		vact_player_space,
		vact_zoom_100,
		vact_play_audio,
		// mouse-driven (also bindable to keys)
		vact_zoom_in,
		vact_zoom_out,
		vact_orbit_drag,
		vact_pan_drag,
	};

	// Mouse "button" enumeration, expanded so wheel up/down are first-class
	// gestures alongside physical buttons. mb_none means "no mouse binding".
	enum EMouseBtn
	{
		mb_none = 0,
		mb_left,
		mb_right,
		mb_middle,
		mb_x1,
		mb_x2,
		mb_wheel_up,
		mb_wheel_down,
	};

	struct Binding
	{
		const char* name;
		const char* label;
		EScope scope;
		UINT action;
		// Keyboard slot. vk == 0 means "no key bound".
		BYTE vk;
		BYTE key_mods;
		// Mouse slot. btn == mb_none means "no mouse bound".
		BYTE btn;        // EMouseBtn
		BYTE mouse_mods; // FCONTROL | FSHIFT | FALT (FVIRTKEY unused here)
	};

	const std::vector<Binding>& defaults();
	const std::vector<Binding>& current();

	void load_from_registry();
	void save_to_registry();

	void set_key(int index, BYTE vk, BYTE mods);
	void set_mouse(int index, BYTE btn, BYTE mods);
	void clear_key(int index);
	void clear_mouse(int index);
	void reset_binding(int index);
	void reset_all();

	HACCEL build_accel_table();

	// Match a keyboard event against the bindings of a given scope.
	bool match_view(EScope scope, UINT vk, bool ctrl, bool shift, bool alt, UINT& out_action);

	// Match a mouse event against the bindings of a given scope. Pass one of
	// the EMouseBtn values for `btn`.
	bool match_mouse(EScope scope, BYTE btn, bool ctrl, bool shift, bool alt, UINT& out_action);

	std::string format_shortcut(BYTE vk, BYTE mods);
	std::string format_mouse(BYTE btn, BYTE mods);
	std::string scope_name(EScope s);

	// Reverse of format_shortcut / format_mouse. Both accept the empty string
	// (returns vk=0 / btn=mb_none, mods=0) and are case-insensitive on token
	// names ("ctrl+f" == "Ctrl+F"). Unknown tokens return false / leave outputs
	// at 0; callers should treat that as "fall back to default".
	bool parse_shortcut(const std::string& s, BYTE& out_vk, BYTE& out_mods);
	bool parse_mouse(const std::string& s, BYTE& out_btn, BYTE& out_mods);

	// Returns the formatted keyboard shortcut for the menu command with the
	// given command id (e.g. ID_FILE_OPEN -> "Ctrl+O"), or empty string if
	// the command isn't in the bindings table or has no key slot.
	std::string shortcut_for_command(UINT cmd);
}
