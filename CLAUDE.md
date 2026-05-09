# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

XCC is a suite of Windows MFC tools for inspecting, extracting, and editing data from Westwood RTS games (Dune 2, Tiberian Dawn/Sun, Red Alert 1/2, Yuri's Revenge): MIX containers, SHP/TMP/VXL/PCX images, palettes, AUD/VQA media, INI rules, maps, CSF strings, etc. Most apps are MFC GUIs; the `Library` project is the shared static library that every app links against.

## Build

- **Toolchain**: Visual Studio 2022 (`v143` toolset), Win32 only (the projects are 32-bit MFC; do not retarget to x64), C++ standard `stdcpplatest`.
- **Dependencies**: vcpkg manifest mode (`vcpkg.json` at root). Run `vcpkg install` once in the repo root, or use the explicit list in `vcpkg-install.txt` (`boost-algorithm boost-crc bzip2 libjpeg-turbo libpng libvorbis lzo`). `Release|Win32` uses static vcpkg triplet (`VcpkgUseStatic=true`).
- **MFC linkage**: `Release` uses **static MFC**, `Debug` uses **dynamic MFC**. Keep this split when adding configurations.
- **Master solution**: `XCC.sln` builds only six projects — `XCC Library`, `XCC Mixer`, `XCC MIX Editor`, `XCC AV Player`, `XCC TMP Editor`, `XCC Editor`. The other apps (Map Decoder/Encoder, Mod Launcher/Creator, RA2 Map Updater, RA2 Radar Customizer, XSE CLT, XIF Editor, etc.) each have their own `.sln` and must be opened/built individually.
- **Shared MSBuild props**: `XCC.props` is imported by every `.vcxproj`. It adds `..\misc` to the include path, sets `_HAS_STD_BYTE=0;NOMINMAX`, disables warnings 4018/4201/4267/4554/4806, and links `dsound.lib;gdiplus.lib`. Edit project-wide compile/link settings here, not in individual `.vcxproj` files.
- **Output layout**: Each app builds into its own `Release/` or `Debug/` next to its project file (e.g. `Mixer\Release\XCC Mixer.exe`). Both directories are gitignored.
- **Installer**: `XCC.nsi` (and per-game `.nsi` files) bundle the built `Release` binaries plus files in `data\` into NSIS installers. `create_zips.bat` is a legacy packaging script with hard-coded `e:\vc\xcc\...` paths — it does not work as-is from this checkout, treat it as reference only.
- **CLI build**: `msbuild XCC.sln /p:Configuration=Release /p:Platform=Win32 /m`. For a single app outside the master solution: `msbuild "Mixer\XCC Mixer.vcxproj" /p:Configuration=Release /p:Platform=Win32`. Always pass `/p:Platform=Win32` — the projects do not have an x64 configuration.
- **Static linking (Mixer + Library only)**: `Mixer/XCC Mixer.vcxproj` and `Library/XCC Library.vcxproj` set `<VcpkgUseStatic>true</VcpkgUseStatic>` for `Release|Win32` and reference the `..\vcpkg_installed\x86-windows-static\include` headers directly. Final `XCC Mixer.exe` is ~4.2 MB with no sidecar DLLs (vcpkg's AppLocalFromInstalled becomes a no-op). The other apps in the suite still build dynamic (you'll see `bz2.dll`, `jpeg62.dll`, etc. in their Release dirs); to flip another app, mirror Mixer's two `<PropertyGroup Label="Vcpkg">` blocks and update the include path. Static triplet must be installed first: `vcpkg install --triplet=x86-windows-static` from the repo root.

There is no test suite, no linter config beyond `.editorconfig` (UTF-8, LF, tab indent, size 2), and no CI. "Build succeeds" is the only automated check.

## Code architecture

### Layered structure

1. **`Library/` (XCC Library, static lib)** — all format codecs, file I/O, INI readers, image/audio decoders, and shared utilities. Every GUI app depends on this. Headers from here are referenced by short name (`#include "mix_file.h"`, `#include <id_log.h>`) because `XCC.props` puts `misc/` on the include path and each app puts `..\Library` on its own include path. When adding shared functionality, put it in `Library/` rather than in an app.
2. **`misc/`** — third-party-style headers and small helpers (`xcc/data_ref.h`, `xcc/find_ptr.h`, `xcc/string_view.h`). Globally included via `XCC.props`.
3. **App projects** — one MFC executable per directory (`Mixer/`, `MIX Editor/`, `Editor/`, `TMP Editor/`, `AV Player/`, `Map Decoder/`, ...). Each has its own `StdAfx.h/cpp`, resource script, dialogs, and a thin `App` class that wires up MFC, calls `xcc_dirs::load_from_registry()`, loads `mix_database`, and shows the main window/dialog.

### Format families in Library

Files are grouped by container/format, typically as a triple `<name>_file.h` (struct + read), `<name>_file_write.h/.cpp` (writer), and a decoder/encoder (`<name>_decode.cpp` / `<name>_encode.cpp`):

- **Containers**: `mix_file*` (MIX, with read/write/edit variants and `mix_cache`, `mix_decode`, `mix_sfl`), `big_file*`, `pak_file`, `mix_rg_*` (RG MIX variant).
- **Images**: `shp_*` (Dune2/TD/RA + TS/RA2 variants), `tmp_ra*` and `tmp_ts*` (terrain), `vxl_file` + `hva_file` (voxels), `cps_file`, `wsa_*`, `dds_file`, `pcx_*`, `png_file`, `jpeg_file`, `tga_file`, `bmp_file`, `vqa_file` + `vqa_decode`/`vqa_play`. `virtual_image*` is the in-memory image abstraction; `image_tools` and `palet*` handle palette conversion.
- **Audio**: `aud_*`, `voc_file`, `wav_*`, `mp3_*`, `ogg_file`, `ima_adpcm_wav_*`. `virtual_audio` is the playback abstraction.
- **Text/INI/strings**: `ini_reader` plus per-game subclasses (`map_ra_ini_reader`, `map_td_ini_reader`, `map_ts_ini_reader`, `pkt_ts_ini_reader`, `rules_ts_ini_reader`, `theme_ts_ini_reader`, `sound_ts_ini_reader`, `itc_ini_reader`, `neat_ini_reader`, `fs_ini_file`). `csf_file` is the Westwood string-table format. `xif_*` is XCC's own structured format used for the data files in `data/` (`infantry.xif`, `structures.xif`, `units.xif`, `theater.xif`, `overlays.xif`).
- **Game logic helpers**: `xcc_level`, `xcc_mod`, `xcc_structures`, `xcc_units`, `xcc_infantry`, `xcc_overlays`, `xcc_templates`, `xcc_cell*`, `xcc_apps`, `xcc_dirs` (resolves install dirs from registry), `xcc_dsb`, `extract_object` (the cross-format extractor used by Mixer's "extract as ...").
- **Crypto/util**: `blowfish` (Westwood-style WSKey), `crc`, `string_conversion`, `fname` (path helper used everywhere instead of `std::filesystem`), `virtual_binary` (refcounted byte buffer), `stream_reader`/`stream_writer`.

### Cross-cutting conventions

- **Precompiled header**: every project uses `StdAfx.h`/`stdafx.h`. The Library's `stdafx.h` does `using namespace std;` and pulls in Boost + the `xcc/` helpers — code in `Library/` and apps relies on `std::` names being unqualified. New headers should still qualify `std::` rather than depend on this transitively.
- **Naming**: classes are prefixed `C` (MFC style) for GUI types and lowercase `Cname` (e.g. `Cmix_file`, `Cfname`, `Cvirtual_image`) for Library types. Free functions and namespaces use snake_case (`xcc_dirs::load_from_registry`, `mix_database::load`).
- **Settings**: apps call `SetRegistryKey("XCC")` and persist via `xcc_dirs` / `reg_key`. Look there before introducing any new config storage.
- **Logging**: `xcc_log::attach_file("<App> log.txt")` writes a per-app log next to the exe. `id_log.h` is the lower-level logger.
- **MIX database**: `mix_database` (loaded from `<data dir>/global mix database.dat`, which defaults to the exe's own folder when no registry override) maps CRC-hashed filenames inside MIX archives back to readable names. Two entry points in `misc/id_log.{h,cpp}`: `mix_database::load()` reads the on-disk file; `mix_database::load_from_buffer(data, size)` parses an in-memory blob. Apps call `load()` at startup; if it fails they call `xcc_dirs::reset_data_dir()` and retry — preserve this fallback when modifying startup code.
- **Mixer's embedded mix-database fallback**: source-of-truth dat lives at repo `data/global mix database.dat` and is compiled into `XCC Mixer.exe` as `RCDATA` resource `GLOBAL_MIX_DATABASE` (declared in `Mixer/XCC Mixer.rc`). `CXCCMixerApp::InitInstance` chains: on-disk attempt 1 → `reset_data_dir` → on-disk attempt 2 → `FindResource(GLOBAL_MIX_DATABASE)` + `load_from_buffer`. `g_mix_db_source` (declared in `Mixer/XCC Mixer.h`, set in InitInstance, read by `CMainFrame::OnCreate`) drives a `[DB: on-disk]` / `[DB: embedded]` / `[DB: missing]` window-title suffix so the user can see where names came from at a glance. **On-disk wins**: any user-edited or mod-shipped dat overrides the embedded copy.
- **mix_cache format-CRC sentinel**: `misc/mix_cache.cpp::get_ft_crc()` mixes a `"probe-v2"` string into the CRC that identifies the on-disk format-table cache. Bump the sentinel string when changing `magic_dispatch` / probe order in `cc_file.cpp` so older caches built by a different probe path are auto-invalidated on next launch — otherwise users see stale (and possibly wrong) classifications.
- **`Ccc_file::get_file_type` magic-byte short-circuit**: `magic_dispatch()` in `misc/cc_file.cpp` runs before the ~30-format probe loop and dispatches immediately for formats with a unique-enough prefix: PNG (8-byte exact), OGG/DDS/BINK (u32 exact), VOC (19-byte exact), JPEG (`FF D8 FF`). Header-validated formats (SHP/TMP/AUD/VXL/HVA/VQP/FNT/CPS/MIX/PAK/BIG/WSA/ST/BIN/CSF/VQA/XCC/XIF/W3D/MP3) still go through the full probe so their `is_valid()` size/version gates run. **MP3 is intentionally not shortcut** — `ID3` is only 3 bytes and the existing `Cmp3_file` probe validates the first audio frame header, not the ID3 tag, so any file beginning with bytes `49 44 33` would otherwise be misclassified.
- **`looks_like_ini` heuristic**: also in `misc/cc_file.cpp`. Gates the heavy text/INI parser tail (`Cnull_ini_reader` + four `Cmap_*_ini_reader` / `Cpkt_ts_ini_reader` parses, each scanning the whole buffer) so plain text/CSV/log files aren't paying 5 full-buffer parses to be classified as `ft_text`. Looks for `[Letter` after a newline within the first 4 KiB. Don't loosen the heuristic without remembering this is the dominant first-open cost on text-heavy MIXes.
- **GDI+**: apps that render images call `Gdiplus::GdiplusStartup` in `InitInstance`. PNG/JPEG paths in Library go through GDI+, libpng, or libjpeg-turbo depending on the call site.
- **Image color order (gotcha)**: not uniform across decoders. PNG paths deliver **RGBA**, raw TGA-32 in the file/buffer is **BGRA**, and DDS-decoded `Color8888` (in `dds_file`) is **RGBA** (`r,g,b,a` byte order). The `t_palette32bgr_entry` struct is BGRA in memory (BMP order). When passing 32-bit pixels to view code (e.g. `CXCCFileView::draw_image32` takes a `bgra` flag), pick the flag based on the source decoder, not by guessing.
- **Library → app theme hooks**: `Library/` and `misc/` code can't depend on Mixer's `theme.cpp`, so theming is delivered via function-pointer hooks installed at app startup. `misc/ListCtrlEx.h` exposes `CListCtrlEx_theme` (is_dark / row_bg / row_bg_alt / text / grid / show_grid) set via `CListCtrlEx_set_theme(...)`; `misc/ETSLayout.h` exposes `extern HBRUSH (*ETSLayout_theme_brush)()` consulted before falling back to `GCL_HBRBACKGROUND`. Mixer's `XCC Mixer.cpp` installs both during `InitInstance`. Reuse this pattern (hook in shared code, install in app) instead of pulling theme symbols into `misc/` or `Library/`.
- **SEH around Copy as / dispatch**: `CXCCMixerView::dispatch_copy_as` is invoked through a static `seh_call_dispatch(...)` helper because `__try`/`__except` and C++ object unwinding can't share a frame. `dispatch_copy_as` takes `const Cfname&` to keep copy-construction out of the SEH frame. New crash-prone Copy-as paths should use this wrapper rather than introducing local `__try` blocks.
- **Copy as JPEG is intentionally disabled**: `OnUpdatePopupCopyAsJpeg` / `OnUpdatePopupCopyAsJpegSingle` early-return with `pCmdUI->Enable(FALSE)`. libjpeg-turbo destination-buffer handling proved fragile (SHP→JPEG crashed in `empty_output_buffer`). Don't re-enable without first reproducing and fixing the original crash.

### Mixer dark-mode subsystem

`Mixer/theme.{h,cpp}` is a cross-cutting Win11-style dark mode for the Mixer app. Don't reinvent it — extend it.

- **Persistence**: registry section `"Theme"` under `SetRegistryKey("XCC")`, holds `mode` (light/dark), `show_grid` (listview gridlines), and `alpha_color` (checkerboard color for transparency display). Defaults: light mode, grid off, alpha green.
- **API**: `theme::load/save`, `theme::get/set_mode`, `theme::is_dark`, `theme::show_grid/set_show_grid`, `theme::alpha_color/set_alpha_color`, `theme::checker_a/checker_b`. Color helpers `bg/bg_alt/text/text_dim/accent/border/menu_bg/menu_hot` and matching brushes. `theme::apply_window/apply_listview/apply_titlebar/apply_app_mode` wire dark mode into HWNDs via undocumented uxtheme ordinals (`SetPreferredAppMode`=135, `AllowDarkModeForWindow`=133, `FlushMenuThemes`=136) and `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`.
- **Custom controls**: `CThemedSplitterWnd` (override `OnDrawSplitter` only — do NOT add `OnEraseBkgnd`, it wipes child panes), `CThemedStatusBar` (custom `OnPaint` per pane), `CThemedHeaderCtrl` (NM_CUSTOMDRAW reflection for header text color; signature is `void OnCustomDraw(NMHDR*, LRESULT*)`).
- **Plain `SysHeader32` in dialogs**: dialog list-views own a header that isn't wrapped in `CThemedHeaderCtrl`. MFC's `NM_CUSTOMDRAW` reflection chain didn't reliably reach those headers from a `CListCtrlEx` parent, so `theme::install_dark_header_subclass(HWND)` installs a Win32 `GWLP_WNDPROC` subclass (`dark_header_proc`) that paints `WM_PAINT` directly with `bg_alt`/`text`/`border`. Use this for any embedded header in a dialog rather than reflecting through MFC.
- **Dialog body / SysTreeView32 / ComboBox quirks**: `apply_dialog(HWND)` walks children and themes per class. `ETSLayoutDialog::OnEraseBkgnd` consults the `ETSLayout_theme_brush` hook before `GCL_HBRBACKGROUND`, otherwise the dialog body paints white in dark mode. `apply_treeview` themes `SysTreeView32`. **ComboBox special-case**: `apply_window` calls `SetWindowTheme(h, NULL, NULL)` for `ComboBox`/`ComboLBox` (skips `DarkMode_Explorer`); uxtheme refused to dark-paint comboboxes in this process even with styles matching apps where it works.
- **Owner-draw menu**: `theme::on_measure_menu_item` / `on_draw_menu_item` handle `WM_MEASUREITEM` / `WM_DRAWITEM` for the menu bar. They split labels on `\t` and right-align the shortcut portion — don't change the format string convention. Item data is wrapped in a `theme_menu_data` struct (magic + `is_bar` flag + label bytes) by `Mixer/MainFrm.cpp::set_menu_owner_draw`; theme code reads via `theme::menu_item_label(data, &is_bar)`. Bar items skip the popup checkmark gutter and get `+12px` symmetric pad to match light-mode visual width; popup items keep the gutter for checkmark alignment.
- **Menu bar background brush**: `MENUINFO::hbrBack` is set to `theme::menu_bg_brush()` in dark / NULL in light by `CMainFrame::rebuild_menu_owner_draw`. The bar HMENU and every popup HMENU is walked manually (`apply_menu_background_recursive`) — `MIM_APPLYTOSUBMENUS` with a NULL hbrBack does **not** reliably clear a previously-set dark brush from already-attached popups on all Windows builds, which caused a dark→light switch to leave the Theme popup painting white-on-white until the user toggled again. After flipping items between owner-draw and string, also call uxtheme ord 136 `FlushMenuThemes` so popup theme caches built while items were owner-draw are discarded. Without the brush itself, the strip behind the menu bar paints with `COLOR_MENU` regardless of dark mode (the long-standing white band at the right end of the bar).
- **Theme switch must invalidate cached menu-bar widths**: after `set_menu_owner_draw` flips items between `MFT_OWNERDRAW` and `MFT_STRING`, `DrawMenuBar` alone doesn't recompute widths. `rebuild_menu_owner_draw` does `SetMenu(NULL) + SetMenu(hm)` to force a re-measure; otherwise the previous mode's widths persist (e.g. dark→light keeps the wider owner-draw measurements until the user toggles again). Removing the cycle makes things *worse*, not better — confirmed empirically.
- **Theme rebuild must be posted, not called inline**: `OnThemeLight` / `OnThemeDark` `PostMessage(WM_USER + 0x101)` to schedule `rebuild_menu_owner_draw` instead of calling it inline. When the command was invoked from clicking the Theme popup, calling `SetMenuItemInfo` against the still-mid-dismiss popup's items silently fails for that popup. Deferring the rebuild past the click's menu-dismiss loop fixes both light↔dark directions.
- **Accelerators**: Ctrl+1 = light, Ctrl+2 = dark. M (when `CXCCFileView` has focus) toggles alpha-only grayscale view of 32-bit images.

### Reserved resource IDs (Mixer)

`Mixer/resource.h` reserves `41000+` for theme commands. Currently used: `ID_THEME_LIGHT`=41000, `ID_THEME_DARK`=41001, `ID_THEME_SHOW_GRID`=41002, `ID_THEME_ALPHA_COLOR`=41003, `ID_THEME_INTERP_NEAREST`=41004, `ID_THEME_INTERP_BILINEAR`=41005, `ID_THEME_INTERP_BICUBIC`=41006, `ID_THEME_INTERP_LANCZOS`=41007, `ID_THEME_SHP_TRANSPARENCY`=41008, `ID_THEME_INTERP_ANISOTROPIC`=41009 (XCCD2D only), `ID_THEME_USE_CHECKERBOARD`=41010, `ID_THEME_USE_EXTERNAL_PROGRAMS`=41011, `ID_THEME_PANES_ONE`=41012, `ID_THEME_PANES_TWO`=41013. New theme IDs continue at 41014+.

Other ID conventions in `Mixer/resource.h`: child-control IDs `1100–1199` are used by dialogs and the in-pane SHP/WSA player controls. Player range so far: `IDC_REGEX_TOGGLE`=1100, `IDC_PLAYER_*`=1101–1111, `IDC_LOAD_FOLDER`=1110, `IDC_PLAYER_SHADOWS`=1112, `IDC_PLAYER_BG`=1113, `IDC_PLAYER_SIDE0..7`=1114–1121, `IDC_PLAYER_SIDE_CUSTOM`=1122, `IDC_PLAYER_GRID_SEL`=1123. Command IDs `33132+` are used for newer menu/context items (`ID_FILE_SEARCH_IN_MIX`, `ID_POPUP_COPY_NAME`, `ID_VIEW_PALETTE_PREV/NEXT_SIBLING`, `ID_POPUP_BATCH_EXTRACT`, `ID_POPUP_BATCH_EXTRACT_PRESERVE`).

### Mixer navigation & view subsystems

These pieces are cross-cutting in the Mixer app and easy to break if you don't know they exist:

- **Per-pane forward/back stack** (`CXCCMixerView`): `nav_go_up()` / `nav_go_forward()` plus `m_nav_forward` (stack of `t_nav_entry`) and `m_entered_ids` (parallel stack of the entry id used to descend into nested MIXes). All "go up" call sites — `OnFileClose`, the `..` listview row, mouse XButton1 — must route through `nav_go_up()` so the forward stack stays consistent. Any non-back/forward navigation calls `nav_clear_forward()`.
- **Pinned anchor rows in sort**: `Browse...` and `..` are sentinels that must always pin to the top of the listview regardless of column or direction. The sort comparator hoists them **before** the `m_sort_reverse` swap, not after — putting the hoist after the reverse flips them to the bottom.
- **Numeric size sort**: `t_index_entry::size_bytes` (long long) is the source of truth for size sorting; `size` (formatted string like "10 KB") is display only. Populate `size_bytes` for both MIX entries (`m_mix_f->get_size`) and filesystem entries (`WIN32_FIND_DATA`).
- **In-pane SHP/WSA player** (`CXCCFileView`): a fixed control band at the bottom of the file-info pane (Play/Pause, `<<` reverse toggle, Grid, Native, seek slider, frame label, FPS edit+spin). Pressing `P` on a SHP/WSA enters player mode; pressing again returns to grid. Switching files exits. Per-format decode handled in `player_decode_frames` (SHP TD/RA, SHP Dune2, SHP TS, WSA, WSA Dune2). Timer-driven repaints must invalidate **only** the image area above the controls (with `bErase=FALSE`) — invalidating the whole pane causes flicker.
- **Player controls in dark mode**: child controls need `theme::apply_window` at creation and a `WM_CTLCOLOR` handler returning `theme::bg_brush()` and setting `theme::text()` / `theme::bg()`. Win32 buttons/sliders won't be fully Win11-dark; that's accepted. On theme switch, `CMainFrame::apply_theme_to_children` calls `CXCCFileView::reapply_player_theme()`, which re-runs `apply_window` on every player-band HWND (including the combobox's internals via `GetComboBoxInfo`) and forces a repaint. Add new player-band controls to that walk.
- **Game Grid combobox = owner-draw, intentional**: `m_player_iso_grid` is created with `CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_CLIPSIBLINGS | WS_CLIPCHILDREN` and painted in `CXCCFileView::OnDrawItem` for `ODT_COMBOBOX`/`IDC_PLAYER_GRID_SEL` using `theme::bg`/`text`/`accent`. Matching working apps' uxtheme styles via WinSpy did **not** make uxtheme dark-paint the combobox in this process. Don't revert to system-drawn unless you have a concrete uxtheme fix.
- **OPAQUE text mode in CXCCFileView**: the file-info text labels paint with `SetBkMode(OPAQUE)` so they stay legible when overlapping zoomed images. Don't switch to TRANSPARENT.

### Mixer window & pane state

- **Window placement persistence**: `CMainFrame::OnDestroy` writes `WINDOWPLACEMENT::rcNormalPosition` (left/top/right/bottom) plus a `win_maximized` flag (from `showCmd == SW_SHOWMAXIMIZED`) under `MainFrame\win_*`. `CXCCMixerApp::InitInstance` reads them after `ProcessShellCommand` and `SetWindowPlacement`s before `ShowWindow`. First-run sentinel is `INT_MIN` on `win_left` — when present, falls back to the historical `SW_SHOWMAXIMIZED`. `rcNormalPosition` is the **restored-state rect** even when the window is currently maximized, so closing while maximized correctly remembers both "should be maximized on next start" and "the rect to use when the user un-maximizes".
- **Pane Layout (Theme → One Pane / Two Panes)**: hides the middle MIX listview (`m_right_mix_pane`) by collapsing the splitter's column 1 via `m_wndSplitter.SetColumnInfo(1, 0, 0)` + `RecalcLayout` instead of destroying the view — the pane HWND stays alive, just gets zero space; the file-info column to the right takes the slack. Switching back uses `m_saved_middle_pane_w` (last seen non-zero width, fallback 400 px). Persisted under `MainFrame\two_panes` (default 1) and re-applied at the end of `OnCreateClient`. **`RecalcLayout` doesn't repaint** — must follow with `RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW` on the splitter subtree, otherwise the remaining panes keep their old clipped pixels until the next WM_PAINT.

### Mixer search & extract conventions

- **Two search dialogs**: `Ctrl+F` opens `CSearchInPaneDlg` (current MIX/folder only, regex toggle defaults off each session); `Ctrl+Shift+F` opens `CSearchFileDlg` (recursive across both panes). Both are resizable (`WS_THICKFRAME`) and multi-select.
- **Search results capture `top_mix_path` at search time** (`t_map_entry::top_mix_path`) so extraction works after pane navigation changes the current MIX. Don't look up the source MIX via `t_index_list` at extract time.
- **Virtual vs non-virtual list trap**: do NOT call `SetItemCount` alongside `InsertItemData` on a non-virtual `CListCtrl` — it creates phantom rows. The search dialogs use `InsertItemData` only.
- **Batch extract** uses `CFolderPickerDialog` and reuses `open_f_id` + `create_deep_dir`. The non-preserve flavor sanitizes filenames by replacing `\` and `/` with `_` to keep output flat. The preserve flavor extracts under `<chosen>/<source_mix_name>/<file>`; only enabled when the pane is inside a MIX.
- **Global search columns + group view**: `CSearchFileDlg` shows Source / File / Size / Path columns. Source/File come from splitting `t_map_entry::name` on the last ` - `; Path is the directory of `e.top_mix_path`; Size from `e.size_bytes` via `totalSize`. The list-view's header is shown (no `LVS_NOCOLUMNHEADER`) and themed via `theme::install_dark_header_subclass`. Rows additionally cluster under collapsible LVS group headers (`EnableGroupView(TRUE)` + one `LVGROUP` per unique source chain, `t_map_entry::group_id` assigned at search time). Group title text doesn't honor `NM_CUSTOMDRAW + CDDS_GROUP` for `clrText/clrTextBk` on Win10/11 — accepted blemish, don't try to recolor it.
- **Local search header + Size column**: `CSearchInPaneDlg`'s `IDD_SEARCH_IN_PANE` listview no longer has `LVS_NOCOLUMNHEADER`; "Name" + "Size" columns are visible and themed. Don't re-add the style.
- **Sortable columns idiom (both search dialogs)**: column-click handler → flip sort key/direction (Size defaults biggest-first; text columns A→Z) → `apply_sort()` over an `m_order` vector of map keys (or sort `m_matches` directly) → `repopulate_list()` that wraps in `SetRedraw(FALSE/TRUE)` and, **when the row count is unchanged (sort-only, not a new search)**, rewrites each row's `lParam` (and `iGroupId` for the global search) via `SetItemData` / `SetItem` instead of `DeleteAllItems` + `InsertItemData`. This avoids per-row `WM_NOTIFY` churn and makes header-click sort feel instant. Reuse the pattern for any future "display order changes but row set doesn't" listview.

### Listview gridlines (theme-aware)

`LVS_EX_GRIDLINES` paints a hardcoded light gray that's unthemable. The codebase therefore:

- **Light mode**: when `theme::show_grid()` is on, the extended style flag is set normally.
- **Dark mode**: the flag is **stripped** (`apply_grid` clears it regardless of `show_grid`); gridlines are drawn manually in `OnCustomDraw`'s `CDDS_ITEMPOSTPAINT` branch using `theme::grid()` over the themed row background.
- `CXCCMixerView::OnInitialUpdate` only sets `LVS_EX_GRIDLINES` when `theme::show_grid() && !theme::is_dark()` — without the dark check, gridlines flash gray on startup before the theme settles.
- `CMainFrame::apply_theme_to_children` calls `apply_grid` on both panes after `apply_listview` so toggling light↔dark fixes up gridlines immediately.

Two custom-draw paths exist: `CListCtrlEx::OnCustomDraw` (dialog list-views, driven by the `CListCtrlEx_theme` hook) and `CXCCMixerView::OnCustomDraw` (the main UI's `CListView`, which is not a `CListCtrlEx`). Keep them in sync if you change row colors.

### Image interpolation (Mixer + XCCD2D)

`Theme → Image interpolation` selects how scaled image previews resample. Five modes, persisted as `Theme\interpolation`:

- `interp_nearest` (0) — sharp pixels (GDI `COLORONCOLOR` in Mixer; D2D `NEAREST_NEIGHBOR` in XCCD2D). Default. Right answer for SHP/PCX/CPS sprites.
- `interp_bilinear` (1) — hand-rolled CPU separable resampler (Mixer); D2D `LINEAR` (XCCD2D). Mixer's hand-rolled path exists because GDI `HALFTONE` degenerates to nearest on integer-clean upscale.
- `interp_bicubic` (2) — GDI+ `HighQualityBicubic` (Mixer); D2D `HIGH_QUALITY_CUBIC` via `ID2D1DeviceContext` QI (XCCD2D).
- `interp_lanczos` (3) — hand-rolled Lanczos-3, separable, single-precision floats, scratch buffers reused across calls.
- `interp_anisotropic` (4) — XCCD2D only (D2D `ANISOTROPIC`).

Mixer's older `interp_ewa` (=4) is gone; out-of-range persisted values fall back to `interp_nearest`. Do not reintroduce EWA — it was too compute-heavy.

### SHP/PCX transparency toggle

`Theme → SHP transparency` (`theme::shp_transparency`, default off). When on, paletted `draw_image8` callers paint palette index 0 as the alpha checkerboard instead of as the literal palette color. Applies to SHP/PCX/CPS/WSA/TMP and to the VXL splat path (the smooth-VXL framebuffer init reads the same flag so toggling interpolation doesn't shift the VXL background color).

### Mixer SHP-player extras (ASE-parity)

The SHP player band has a second row of controls when the file is an SHP/WSA (hidden for VXL):

- **Shadows**: pair-mode toggle. Only enabled when `m_player_cf` is even and ≥ 2. When on, navigation halves to `cf/2`; each frame draws `frame[i]` with `frame[i + cf/2]` composited at 47% black tint (engine convention). Slider range, frame counter, manual nav, and auto-advance all clamp to the halved range.
- **BG**: when off, palette index 0 paints the alpha checkerboard in the player view (overrides the global `theme::shp_transparency` for the player only). Default on.
- **8 side-color preset swatches** + **9th custom swatch**: indices 16–31 (Westwood remap range) get retinted via `r,g,b = preset * (max(r,g,b)/255 * 1.25)`. The custom swatch opens `CColorDialog`. Click the active swatch to clear; only one active at a time.
- **Game Grid combobox**: None / TS Grid (48 px) / RA2 Grid (60 px). Renders an isometric u/v grid where `u = dx + 2·dy`, `v = 2·dy − dx`. Drawn into the source DIB before scaling so the grid lines participate in the chosen interpolation.

When migrating SHP(TS) frames into the player, **use `f.get_x(i)` / `f.get_y(i)` per-frame offsets**, not `(player_cx − cx) / 2` centering. Body and shadow frames have different offsets — that's how the engine aligns them; centering breaks shadow alignment.

### VXL viewer & splat

Pressing `P` on a `.vxl` enters a 3dsmax-style orbit viewer (not an animation player). Left-click-drag rotates: horizontal = yaw, vertical = pitch (~0.4°/px, pitch clamped ±89°). Default camera = 0 yaw, +30° pitch.

Two splat paths share the cached point cloud:

- **Nearest interpolation** → 8-bit point splat with short z-buffer (sharp blocky voxels, original behavior).
- **Any other interpolation** → 3×3 Gaussian footprint (σ=0.7) into 32-bit BGRA accumulator with **strict z-test on float depth** (closer fragment supersedes; equal-depth coplanar voxels accumulate; no epsilon-blend). Result is anti-aliased silhouettes during continuous rotation. The result is cached on the view (`m_vxl_splat_cache`) keyed on yaw/pitch/canvas/transparency — slider scrubs and focus repaints reuse the cached buffer.

The smooth-path background follows `theme::shp_transparency`: off = palette index 0 color (matches Nearest); on = checkerboard. Without this, switching interpolation modes flickered the VXL background to black.

### XCCD2D — parallel Direct2D port of Mixer (parked experiment)

**Status: parked experiment, not the primary target.** XCCD2D was a port to evaluate Direct2D + DirectWrite + WIC against the GDI/GDI+ Mixer. The verdict: not worth it for this app's hot path. Mixer's dominant view is a grid of small paletted sprites that each require CPU palette lookup; the per-image BindDC/BeginDraw/upload overhead in D2D is pure tax over GDI's `StretchBlt`, and Lanczos still resamples on CPU in both. Real wins (GPU bicubic/anisotropic on single large images) are narrow. The "GDI exceptions intentionally not migrated" list below is a tell that the abstraction leaks. **Don't invest further work in reaching parity — keep Mixer as the source of truth.** Only revisit if a future feature genuinely needs GPU compositing (e.g. real-time VXL rotation at 60fps).

`XCCD2D/` (sibling of `Mixer/`) is the same app rendered with Direct2D + DirectWrite + WIC instead of GDI/GDI+. Both apps coexist; XCCD2D writes to registry app key `"XCC D2D Mixer"` so settings don't trample. Class renames: `CXCCFileView` → `CD2DFileView`, app class → `CXCCD2DMixerApp`. Other classes share names with Mixer (separate `.exe`, no symbol clash).

- **Build**: `XCCD2D\XCCD2D.sln`, Win32 only. Links `d2d1.lib;dwrite.lib;windowscodecs.lib` on top of XCC.props. Output: `XCCD2D\Release\XCC D2D Mixer.exe`.
- **Process-wide singletons** in `d2d_factory.{h,cpp}`: `ID2D1Factory1`, `IDWriteFactory`, `IWICImagingFactory`. Init in `InitInstance` after `AfxOleInit`; release in `ExitInstance`.
- **Render targets**: per-paint `CD2DDcTarget` (RAII around `ID2D1DCRenderTarget` + `BindDC` + `BeginDraw`/`EndDraw`) for owner-draw / themed controls. View-owned `CD2DHwndTarget` is wired but not yet used; current paint flow goes through DC bridges.
- **`theme::stretch_image`**: D2D-only signature `(rt, ID2D1Bitmap*, src_pixels_bgra, sw, sh, D2D1_RECT_F dst)`. Lanczos still resamples on the CPU and uploads a fresh bitmap; Bicubic and Anisotropic require `ID2D1DeviceContext` QI off the DCRenderTarget.
- **Documented GDI exceptions in XCCD2D** (intentionally not migrated):
  1. `OnCtlColor` returns in `MainFrm` and `CD2DFileView` — Windows-driven mechanism for static-text labels in the player band; HBRUSH-only, no D2D path. The HBRUSH cache in `theme.cpp` exists exclusively to feed these two callers.
  2. `theme::on_draw_menu_item` — owner-draw menu painter. Tried `CD2DDcTarget` around `dis->hDC`; the menu compositor uses a layered popup window and D2D bind succeeds but paints blank. Stays direct GDI.
  3. `CThemedHeaderCtrl::OnCustomDraw` `CDRF_NEWFONT` text-color hand-off — the system continues painting the header text after the handler returns, so `SetTextColor`/`SetBkMode` must be GDI. The background fill and right-edge separator are D2D.
  4. `theme::on_measure_menu_item`'s `GetTextExtentPoint32` — measurement before a paint we don't own.

When changing rendering in XCCD2D, **only touch the GDI exceptions if you have a specific Windows mechanism in mind**. The CPU pixel logic in `draw_image*` and `player_draw` is identical to Mixer's; only the upload + blit changes.

### Mixer palette navigation

- `CSelectPaletteDlg` supports both MIX-loaded palettes (the original behavior) and folder-loaded palettes via "Load Folder...". Folder-loaded palettes mutate `CMainFrame::m_pal_list` / `m_pal_map_list` for the session via `pal_list_mut()` / `pal_map_list_mut()` accessors.
- `Ctrl+[` / `Ctrl+]` cycle siblings (palettes sharing the current palette's parent tree node). They are not hardcoded to a game; they walk whatever tree was loaded.

## Working in this codebase

- Treat `Library/` as the source of truth for any format work — the MFC apps are thin shells over it. Adding a new format means: header + reader + writer + (optional) decoder in `Library/`, then a viewer/dialog in the relevant app.
- When editing a `.vcxproj` to add a source file, also add it to the matching `<ItemGroup>` in the project file by hand or via the IDE; there is no glob.
- The repo intentionally targets Win32/MFC and uses several legacy idioms (`using namespace std;` in PCH, `C`-prefixed classes, registry-backed settings). Don't "modernize" these wholesale — match the surrounding style.
