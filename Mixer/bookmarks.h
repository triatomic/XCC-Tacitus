#pragma once

#include <string>
#include <vector>

// User-curated bookmarks: MIX/archive files and folders the user has pinned for
// quick re-opening. Unlike recents (auto-pushed, most-recent-first, capped), the
// bookmark list is explicitly managed by the user and kept in stable insertion
// order. Persisted to the active settings.ini under [Bookmarks] as path0..pathN.
// File > Bookmarks reads list() lazily on popup; clicking an entry opens it in
// the focused pane.
namespace bookmarks
{
	// Hard ceiling on stored entries; matches the contiguous menu-id range
	// ID_FILE_BOOKMARK_00 .. ID_FILE_BOOKMARK_LAST.
	int hard_cap();

	// Load from INI once at app start. Idempotent.
	void load();

	// Append `path` if not already present (case-insensitive). Returns true if a
	// new entry was added; false on empty path, duplicate, or a full list.
	// Persists immediately.
	bool add(const std::string& path);

	// Remove any entry matching `path` (case-insensitive). Persists if changed.
	void remove(const std::string& path);

	// Case-insensitive membership test.
	bool contains(const std::string& path);

	// Insertion-ordered list. Empty until load() has run.
	const std::vector<std::string>& list();

	// Drop every entry and persist. Used by File > Bookmarks > Clear.
	void clear();
}
