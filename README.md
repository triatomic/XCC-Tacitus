# XCC-Tacitus

A fork of XCC Mixer with a Win11-style dark mode, in-pane SHP/WSA/VXL player and recorder, customizable keybinds, and faster search & navigation.

<img width="2559" height="1130" alt="image" src="https://github.com/user-attachments/assets/288f76ca-4ab1-4993-92e0-b30eaa0ae68f" />

## Downloads

Download latest official release: <https://github.com/triatomic/XCC-Tacitus/releases>

## Lineage

This project descends from a chain of forks:

- **Olaf van der Spek's XCC Utilities** — the original Westwood RTS toolset (Dune 2, Tiberian Dawn/Sun, Red Alert 1/2, Yuri's Revenge): MIX containers, SHP/TMP/VXL/PCX images, palettes, AUD/VQA media, INI rules, maps, CSF strings, etc. Maintained as [`saralmira/xcc`](https://github.com/saralmira/xcc) (configured here as `upstream`).
- **Vodrix's "Mayan Pyramid Edition"** — [`Vodrix/xcc`](https://github.com/Vodrix/xcc). Many ergonomic and stability improvements (better filetype detection, working fileview scroll, KB/MB/GB sizes in listviews, sane hotkeys, no more crashes on corrupted MIX headers, merged Tomsons/Omniblade fixes, cleaner global mix database, etc.). The `defe13a Overlay Vodrix fork onto tree, re-apply dark mode` commit in this repo brings his tree in.
- **This fork (`triatomic/XCC-Tacitus`)** — built on top of Vodrix's work.

Credit for the underlying app and the bulk of the modern improvements goes to Olaf van der Spek and Vodrix respectively. This fork's contribution is the additions described below.

## What's new in this fork

  - **Win11-style dark mode** — full theming via uxtheme ordinals + DWM immersive dark, owner-draw menus (bar + popups),
   themed splitter / status bar / list-view headers, dialog walker, dark `SysHeader32` subclass for embedded headers.
  Light and dark switchable via `Theme` menu (Ctrl+1 / Ctrl+2). System Default mode follows Windows' light/dark setting
  and refreshes on `WM_SETTINGCHANGE`.
  - **In-pane SHP/WSA/VXL player** — press `P` on a SHP/WSA/VXL to enter player mode without leaving the file pane.
  Play/pause, reverse, frame slider, FPS control. SHP-specific extras: shadow pair-mode, 3-state BG toggle (Color /
  Alpha-checkerboard / Pane), 8 side-color preset swatches + custom, isometric game grid (TS 48px / RA2 60px) drawn into
   the source DIB before scaling. **Ctrl+wheel zoom + right-drag pan** in player mode (SHP/WSA and VXL).
  - **VXL orbit viewer + voxel splat** — 3dsmax-style left-drag rotates (yaw/pitch), incremental warp-back drag,
  infinite mouse travel. Voxel splat with rotation-aware footprint, OpenMP-parallel by output row, supersampling
  1×/2×/4×/8×/16×, optional FXAA post-pass, per-voxel shading with the loaded VPL (auto-detected `voxels.vpl`). Sibling
  parts (`tur`, `barl`) auto-load and play together.
  - **VXL Lighting dialog** (`Graphics → VXL Lighting…`, `Ctrl+L`) — Azimuth / Elevation / Diffuse / Ambient / Specular
  sliders with two-way edit boxes, live commits during slider drag. Normal source choice: file (Westwood normal table
  per section type) vs computed (6-face / 26-neighbor weighted / smooth Gaussian gradient with 3³ or 5³ kernel).
  Engine-faithful VPL section mapping (specular-aware two-light formula ported from vxl-renderer). Light-direction
  overlay sun indicator while the dialog is open. **Ambient occlusion** group (contact / hemisphere methods, strength
  + falloff + quality) bakes per-voxel AO into the shading pass.
  - **HVA animation playback for VXL** — `Load HVA…` button auto-matches HVAs in the same MIX by basename fuzzy match.
  Quaternion-slerp interpolation between keyframes (12 timeline steps per keyframe), Loop toggle, slider scrubbing.
  Sibling-part HVAs play independently from the body's HVA. Picking a body HVA from the menu (or Browse...) also
  auto-pairs `<base>tur.hva` / `<base>barl.hva` for any sub-VXL parts loaded by Full Hierarchy, searching both the
  source MIX and the picked HVA's disk folder — so picking a single HVA animates the whole rig, not just the body.
  - **Image interpolation modes** — Nearest / Bilinear / Bicubic / Lanczos-2 (hand-rolled CPU separable resampler)
  selectable from `Graphics → Image Interpolation`. Persisted per-app.
  - **Sharpen post-pass** — `Graphics → Sharpen` (0 / 25 / 50 / 75 / 100 %), 3×3 unsharp mask applied after resample.
  Auto-bypasses the 1:1 BitBlt fast path at Native zoom when active.
  - **Frame-rate cap + input throttle** — `Graphics → Frame Rate Cap` (30 / 60 / 120 / Unlimited / Custom…). Caps both
  paint invalidation and mouse-input handler bodies; high-poll mice no longer eat a CPU core during orbit-drag.
  - **SHP transparency toggle** — palette index 0 paints the alpha checkerboard instead of the literal palette color.
  Applies to SHP/PCX/CPS/WSA/TMP and the VXL background.
  - **Three search dialogs**:
    - `Ctrl+F` — `CSearchInPaneDlg` searches the current MIX/folder, live filter, sortable Name / Size, multi-select.
    - `Ctrl+Shift+F` — `CSearchFileDlg` searches recursively across both panes (background-threaded so the UI stays
  responsive, Search button doubles as Cancel); results captured with `top_mix_path` so extract still works after pane
  navigation. List view groups results by source MIX chain.
    - `Ctrl+Alt+F` — `CSearchOnDiskDlg` queries voidtools' **Everything** over IPC for archives anywhere on disk
  (mix/big/dat/pak/pkg/wsx) and opens the pick directly.
  - **Nested MIX editing** — add, drop, and delete files inside a MIX that is itself nested inside another MIX (not just
  top-level archives). The nested MIX is extracted to a temp on entry, edited in place, and re-injected into its parent
  when you navigate out — recursing all the way up to the on-disk root archive.
  - **Per-pane forward/back navigation stack** — XButton1 mouse, the `..` row, and `File → Close` all route through the
  same nav stack. Forward/back works as expected; `Browse...` and `..` rows are pinned to the top regardless of sort.
  - **Navigation bar** — optional top strip with a **breadcrumb** (click any segment or chevron to jump/branch through
  the folder + MIX chain) and a live **filter box** for the active pane; `Theme → Navigation Bar` toggles each and swaps
  their sides. `Alt+3` shows/hides both.
  - **Recent files** — `File → Recents` lists most-recently-opened MIX/archive paths (count tunable in the settings INI).
  - **Numeric size sort** — listviews sort by raw bytes, not formatted size strings.
  - **Batch extract** — flat (filenames sanitized) and preserve (under `<chosen>/<source_mix_name>/<file>`) variants.
  **Parallel extract** option (`Configure → Parallel Extract`, default on) — two-phase serial-read / parallel-write that
   wins on SSDs.
  - **Window placement persistence** — restored-rect + maximized state both remembered across sessions.
  - **One-pane / two-pane layout toggle** — `Theme → Pane Layout`, persisted. Hidden middle pane stays alive at zero
  width; splitter no longer accidentally re-expands it on drag.
  - **Listview gridlines** — theme-aware. Custom-drawn in dark mode (the system `LVS_EX_GRIDLINES` paints unthemable
  light gray).
  - **Folder-loaded palettes** — `CSelectPaletteDlg` "Load Folder..." reads palettes from disk recursively. `Ctrl+[` /
  `Ctrl+]` cycle siblings of the current palette. **PAL Paths dialog** — user-managed list of folders / MIX archives
  loaded at every start, with override-per-game toggle. **Load PAL** is a searchable, sortable picker (palettes from the
  list plus every `.pal` in the source MIX) with live preview as you arrow through it.
  - **Themed color picker** — a custom R/G/B + H/S/L + hex picker (replacing the system `CColorDialog`, which ignored
  dark mode) used for alpha color and SHP/VXL custom side colors.
  - **Column hide + order persistence** — right-click any list header to show/hide columns; column order and manual
  resize widths are remembered per listview across sessions. `Theme → Show Column Headers` toggles headers entirely.
  - **Customizable keybinds** — `Configure → Keybinds…` rebinds every accelerator and most context actions to keys +
  mouse buttons (incl. chord shortcuts). Persisted to settings INI. Settings Directory submenu picks between
  `%APPDATA%\XCC\Mixer\` and the EXE folder.
  - **Directories dialog overhaul** — `View → Directories…` rows are editable comboboxes. Auto-detection walks: XCC's
  saved key → Westwood retail registry → Steam libraries (parses `libraryfolders.vdf`, knows per-game appids and
  installdir names for every classic C&C SKU on Steam). All detected sources offered as dropdown choices plus
  `Custom...` folder browser.
  - **Screenshot export** (VXL/SHP player) — `Screenshot` button or `Ctrl+Shift+S`, a split-button menu: **Save As…**
  (PNG default, TGA, PCX) or **Copy to Clipboard** (`Theme → Image → Clipboard Format` picks **Indexed** 8-bit paletted
  DIB, round-trips into Mixer's Paste-as-SHP, or **RGB** 32-bit WYSIWYG composite). Real alpha channel derived from the
  indexed buffer when BG = Alpha or BG = Pane, transparent pixels zeroed in RGB. PCX uses the gamma-corrected color table
  so exports match BMP/PNG. **SHP/WSA captures honor the current zoom level** (Save As and Clipboard alike) — a 250%
  preview exports at 250%, not native pixel size.
  - **Live palette switching in player mode** — `View → Palette` retints SHP/WSA/VXL sprites on the fly while the player
  is open, instead of staying pinned to whichever palette was active when you pressed `P`.
  - **Animated Recording** (VXL/SHP/WSA player) — `Record` button captures animations to GIF or numbered PNG sequence.
  **VXL** modes: rotation only (360° turntable), HVA only, or combined rotation + HVA. **SHP/WSA**: walks the asset's
  own frame animation. Captures honor every active player setting (SS, lighting, VPL, shading, side color, BG mode,
  shadows, palette). **Downscale combobox** for VXL with supersampling on (Native / Half SS / Full SS) — the
  render-high/output-low trick gives clean GIFs at native voxel size with the SS edge cleanup baked in. **Make palette
  index 0 transparent** checkbox for SHP exports real per-frame transparency in both formats — the GIF path uses a
  custom paletted writer (the SHP's own palette as the GIF local palette, disposal=2 for clean per-frame masks). **ESC
  cancels** mid-capture, partial output is preserved.
  - **Embedded global mix database fallback** — the on-disk `data/global mix database.dat` is also compiled into the
  Mixer EXE as `RCDATA GLOBAL_MIX_DATABASE`. If the on-disk dat is missing, the embedded copy is used; the title bar
  shows `[DB: on-disk]` / `[DB: embedded]` / `[DB: missing]`. On-disk wins, so user/mod-shipped dats override the
  embedded one.
  - **Format-probe optimizations** — magic-byte short-circuit in `Ccc_file::get_file_type` for PNG / OGG / DDS / BINK /
  VOC / JPEG / Renegade MIX1. `looks_like_ini` heuristic gates the heavy text/INI parser tail. `mix_cache` extended
  record format (game + LMD locations + format-CRC sentinel) auto-invalidates stale caches and skips the per-entry LMD
  scan on warm reopen. Container indexes (MIX entry table, BIG TOC) switched to hash tables with capacity pre-reserved.
  - **RA1 / TD content fixes** — extension-driven MIX type classifier now runs for all archives (not just encrypted),
  correctly identifying `.wsa`, `.fnt`, `.icn`, `.urb`, `.cps`, `.pkt`, `.eng`/`.fre`/`.ger`, `.tem`, `.vpl`, etc.
  SHP/TMP variant respects game family (TD/RA → `shp (td)` / `tmp` / `tmp (ra)`, not the iso TS variant). RA1 `.vqa`
  framerate detection no longer collapses to 1-2 fps due to multi-second audio prefetch before the first video chunk.
  - **Video Viewer popup overhaul** — Play on SHP/WSA/VQA opens a dialog with FPS edit + spin (1-120, live timer
  restart), real Play/Pause for everything, per-tick auto-advance (no 15-second idle wait), slider scrub works during
  pause. VQA still drives video off audio progress so playback stays in sync regardless of the FPS knob.
  - **OpenMP wait-policy fix** — Mixer self-relaunches once with `OMP_WAIT_POLICY=PASSIVE` so MSVC's `vcomp140` doesn't
  busy-loop a CPU core between parallel regions during various operations

	


## Build

- Visual Studio 2022 (`v143` toolset), **Win32 only** (the projects are 32-bit MFC).
- vcpkg manifest mode (`vcpkg.json` at root). Run `vcpkg install` once. For static-linked Mixer/Library builds: `vcpkg install --triplet=x86-windows-static`.
- Master solution `XCC.sln` builds `XCC Library`, `XCC Mixer`, `XCC MIX Editor`, `XCC AV Player`, `XCC TMP Editor`, `XCC Editor`. Other apps each have their own `.sln`.
- CLI: `msbuild XCC.sln /p:Configuration=Release /p:Platform=Win32 /m`.

## License

Inherits the licensing of upstream XCC Utilities. See the lineage repos for terms.
