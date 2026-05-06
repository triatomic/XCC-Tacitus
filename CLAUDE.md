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
- **MIX database**: `mix_database` (loaded from `data/global mix database.dat`) maps CRC-hashed filenames inside MIX archives back to readable names. Mixer/MIX Editor call `mix_database::load()` at startup; if it fails they call `xcc_dirs::reset_data_dir()` and retry — preserve this fallback when modifying startup code.
- **GDI+**: apps that render images call `Gdiplus::GdiplusStartup` in `InitInstance`. PNG/JPEG paths in Library go through GDI+, libpng, or libjpeg-turbo depending on the call site.

## Working in this codebase

- Treat `Library/` as the source of truth for any format work — the MFC apps are thin shells over it. Adding a new format means: header + reader + writer + (optional) decoder in `Library/`, then a viewer/dialog in the relevant app.
- When editing a `.vcxproj` to add a source file, also add it to the matching `<ItemGroup>` in the project file by hand or via the IDE; there is no glob.
- The repo intentionally targets Win32/MFC and uses several legacy idioms (`using namespace std;` in PCH, `C`-prefixed classes, registry-backed settings). Don't "modernize" these wholesale — match the surrounding style.
