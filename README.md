# XCC-Tacitus

A fork of XCC Mixer focused on a Win11-style dark mode, an in-pane SHP/WSA/VXL player, and improved search & navigation.

## Lineage

This project descends from a chain of forks:

- **Olaf van der Spek's XCC Utilities** — the original Westwood RTS toolset (Dune 2, Tiberian Dawn/Sun, Red Alert 1/2, Yuri's Revenge): MIX containers, SHP/TMP/VXL/PCX images, palettes, AUD/VQA media, INI rules, maps, CSF strings, etc. Maintained as [`saralmira/xcc`](https://github.com/saralmira/xcc) (configured here as `upstream`).
- **Vodrix's "Mayan Pyramid Edition"** — [`Vodrix/xcc`](https://github.com/Vodrix/xcc). Many ergonomic and stability improvements (better filetype detection, working fileview scroll, KB/MB/GB sizes in listviews, sane hotkeys, no more crashes on corrupted MIX headers, merged Tomsons/Omniblade fixes, cleaner global mix database, etc.). The `defe13a Overlay Vodrix fork onto tree, re-apply dark mode` commit in this repo brings his tree in.
- **This fork (`triatomic/XCC-Tacitus`)** — built on top of Vodrix's work.

Credit for the underlying app and the bulk of the modern improvements goes to Olaf van der Spek and Vodrix respectively. This fork's contribution is the additions described below.

## What's new in this fork

- **Win11-style dark mode** — full theming via uxtheme ordinals + DWM immersive dark, owner-draw menus (bar + popups), themed splitter / status bar / list-view headers, dialog walker, dark `SysHeader32` subclass for embedded headers. Light and dark switchable via `Theme` menu (Ctrl+1 / Ctrl+2).
- **In-pane SHP/WSA/VXL player** — press `P` on a SHP/WSA/VXL to enter player mode without leaving the file pane. Play/pause, reverse, frame slider, FPS control. SHP-specific extras: shadow pair-mode, BG toggle, 8 side-color preset swatches + custom, isometric game grid (TS 48px / RA2 60px) drawn into the source DIB before scaling. VXL gets a 3dsmax-style orbit viewer with a Gaussian-footprint splat for smooth silhouettes during rotation.
- **Image interpolation modes** — Nearest / Bilinear / Bicubic / Lanczos-3 (hand-rolled CPU separable resampler) selectable from the `Theme` menu. Persisted per-app.
- **SHP transparency toggle** — palette index 0 paints the alpha checkerboard instead of the literal palette color. Applies to SHP/PCX/CPS/WSA/TMP and the VXL background.
- **Two search dialogs**:
  - `Ctrl+F` — `CSearchInPaneDlg` searches the current MIX/folder, regex toggle, sortable Name / Size, multi-select.
  - `Ctrl+Shift+F` — `CSearchFileDlg` searches recursively across both panes; results captured with `top_mix_path` so extract still works after pane navigation. List view groups results by source MIX chain.
- **Per-pane forward/back navigation stack** — XButton1 mouse, the `..` row, and `File → Close` all route through the same nav stack. Forward/back works as expected; `Browse...` and `..` rows are pinned to the top regardless of sort.
- **Numeric size sort** — listviews sort by raw bytes, not formatted size strings.
- **Batch extract** — flat (filenames sanitized) and preserve (under `<chosen>/<source_mix_name>/<file>`) variants.
- **Window placement persistence** — restored-rect + maximized state both remembered across sessions.
- **One-pane / two-pane layout toggle** — `Theme → Panes`, persisted.
- **Listview gridlines** — theme-aware. Custom-drawn in dark mode (the system `LVS_EX_GRIDLINES` paints unthemable light gray).
- **Folder-loaded palettes** — `CSelectPaletteDlg` "Load Folder..." reads palettes from disk. `Ctrl+[` / `Ctrl+]` cycle siblings of the current palette.
- **Embedded global mix database fallback** — the on-disk `data/global mix database.dat` is also compiled into the Mixer EXE as `RCDATA GLOBAL_MIX_DATABASE`. If the on-disk dat is missing, the embedded copy is used; the title bar shows `[DB: on-disk]` / `[DB: embedded]` / `[DB: missing]`. On-disk wins, so user/mod-shipped dats override the embedded one.
- **Format-probe optimizations** — magic-byte short-circuit in `Ccc_file::get_file_type` for PNG / OGG / DDS / BINK / VOC / JPEG. `looks_like_ini` heuristic gates the heavy text/INI parser tail. `mix_cache` `probe-v2` sentinel auto-invalidates stale on-disk format-table caches.

See `CLAUDE.md` (kept locally, not committed) for the architectural notes.

## Build

- Visual Studio 2022 (`v143` toolset), **Win32 only** (the projects are 32-bit MFC).
- vcpkg manifest mode (`vcpkg.json` at root). Run `vcpkg install` once. For static-linked Mixer/Library builds: `vcpkg install --triplet=x86-windows-static`.
- Master solution `XCC.sln` builds `XCC Library`, `XCC Mixer`, `XCC MIX Editor`, `XCC AV Player`, `XCC TMP Editor`, `XCC Editor`. Other apps each have their own `.sln`.
- CLI: `msbuild XCC.sln /p:Configuration=Release /p:Platform=Win32 /m`.

## License

Inherits the licensing of upstream XCC Utilities. See the lineage repos for terms.
