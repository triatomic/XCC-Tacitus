#pragma once

#include <string>
#include <vector>

// Recent MIX/archive file tracker. Pushes paths opened from disk onto a
// most-recent-first list capped at recents::max_items(); persisted to the
// active settings.ini under [Recents] via AfxGetApp()->WriteProfileString.
// The File > Recents submenu reads list() lazily on popup, so adding an
// entry doesn't need to touch the menu directly.
namespace recents
{
	// User-tunable cap, persisted as [Recents] max_items in settings.ini.
	// Default 20, hard ceiling 256, floor 1. Edit the INI to change.
	int max_items();
	int hard_cap();

	// Load from INI once at app start. Idempotent.
	void load();

	// Push `path` to the front. Removes any prior occurrence (case-insensitive)
	// and trims to max_items(). Persists immediately. No-op on empty path.
	void push(const std::string& path);

	// Most-recent-first list. Empty until load() has run.
	const std::vector<std::string>& list();

	// Clear the list and persist. Used by File > Recents > Clear.
	void clear();
}
