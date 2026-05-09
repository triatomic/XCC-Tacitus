XCC-Tacitus v0.01 — Dark mode + performance
============================================

A fork of XCC Mixer with a Win11-style dark mode, an in-pane SHP/WSA/VXL
player, faster file-type detection, and improved search & navigation.

Built from: triatomic/XCC-Tacitus (master)
Binary:     XCC Mixer.exe (Win32, statically linked, no sidecar DLLs)


Highlights
----------

Dark mode
- Full Win11-style dark theming via uxtheme + DWM immersive dark.
- Owner-draw menus (bar + popups), themed splitter / status bar / list-view
  headers, dialog walker, dark SysHeader32 subclass for embedded headers.
- Switchable from the Theme menu: Ctrl+1 = light, Ctrl+2 = dark.
- Theme-aware list-view gridlines (custom-drawn in dark to avoid the system's
  unthemable light-gray grid).

Performance optimizations
- Magic-byte short-circuit in the file-type probe: PNG / OGG / DDS / BINK /
  VOC / JPEG dispatch on prefix without running the full ~30-format probe.
- looks_like_ini heuristic gates the heavy text/INI parser tail so plain
  text/CSV/log files don't pay 5 full-buffer parses to be classified.
- mix_cache "probe-v2" sentinel auto-invalidates stale on-disk format-table
  caches across upgrades.
- Search dialogs: sort-only repopulate via SetItemData (no row churn /
  WM_NOTIFY storm on header click).

In-pane SHP / WSA / VXL player
- Press P on a SHP/WSA/VXL to enter player mode without leaving the file
  pane. Play/pause, reverse, frame slider, FPS control.
- SHP extras: shadow pair-mode, BG toggle (palette index 0 -> checkerboard),
  8 side-color preset swatches + custom color, isometric game grid (TS 48px /
  RA2 60px) drawn into the source DIB before scaling.
- VXL: 3dsmax-style orbit viewer with a Gaussian-footprint splat for smooth
  silhouettes during continuous rotation.

Image interpolation modes
- Nearest / Bilinear / Bicubic / Lanczos-3 (hand-rolled CPU separable
  resampler), selectable from the Theme menu, persisted per-app.

SHP transparency toggle
- Palette index 0 paints the alpha checkerboard instead of the literal
  palette color. Applies to SHP / PCX / CPS / WSA / TMP and the VXL
  background.

Search
- Ctrl+F   - Search current MIX/folder. Regex toggle, sortable Name/Size,
             multi-select.
- Ctrl+Shift+F - Search recursively across both panes. Source / File / Size
             / Path columns, results grouped by source MIX chain. Captures
             the originating MIX path so extract still works after the user
             navigates the panes.

Navigation
- Per-pane forward/back stack. XButton1 (mouse back), the ".." row, and
  File -> Close all route through the same nav stack.
- "Browse..." and ".." pinned to the top of list views regardless of sort.
- List-view size column sorts numerically (raw bytes), not by formatted
  string.

Other
- Batch extract (flat or preserving source MIX path).
- Window placement persisted across sessions (restored-rect + maximized
  state both remembered).
- One-pane / Two-pane layout toggle from the Theme menu, persisted.
- Folder-loaded palettes in CSelectPaletteDlg ("Load Folder...");
  Ctrl+[ / Ctrl+] cycle siblings of the current palette.
- Embedded global-mix-database fallback compiled into the EXE as RCDATA.
  Title bar shows [DB: on-disk] / [DB: embedded] / [DB: missing]. On-disk
  always wins, so user/mod-shipped dats override the embedded copy.


Lineage
-------
Original: Olaf van der Spek's XCC Utilities (saralmira/xcc).
Fork base: Vodrix/xcc ("Mayan Pyramid Edition") - many ergonomic and
           stability improvements: better filetype detection, working
           fileview scroll, KB/MB/GB sizes, sane hotkeys, no more crashes
           on corrupted MIX headers, merged Tomsons/Omniblade fixes,
           cleaner global mix database.
This fork: triatomic/XCC-Tacitus.

Credit for the underlying app and the bulk of the modern improvements
goes to Olaf van der Spek and Vodrix respectively.


Requirements
------------
- Windows 10 / 11 (32-bit binary, runs on x64 Windows).
- No additional runtime DLLs needed (statically linked MFC + vcpkg deps).


Known limitations
-----------------
- Copy-as-JPEG is intentionally disabled (libjpeg-turbo destination-buffer
  fragility caused crashes on SHP -> JPEG; not yet re-enabled).
- ComboBox controls don't fully dark-paint despite uxtheme styling
  (system limitation; accepted blemish, owner-draw used where it matters
  e.g. the Game Grid combobox in the player).
