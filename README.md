# XCC-Tacitus

A fork of XCC Mixer with a Win11-style dark mode, in-pane SHP/WSA/VXL player and recorder, customizable keybinds, and faster search & navigation.

<img width="2559" height="1373" alt="image" src="https://github.com/user-attachments/assets/cfba70d7-af62-4785-b058-579b846ae231" />


## Downloads

Download latest official release: <https://github.com/triatomic/XCC-Tacitus/releases>

## Lineage

This project descends from a chain of forks:

- **Olaf van der Spek's XCC Utilities** — the original Westwood RTS toolset (Dune 2, Tiberian Dawn/Sun, Red Alert 1/2, Yuri's Revenge): MIX containers, SHP/TMP/VXL/PCX images, palettes, AUD/VQA media, INI rules, maps, CSF strings, etc. Maintained as [`saralmira/xcc`](https://github.com/saralmira/xcc) (configured here as `upstream`).
- **Vodrix's "Mayan Pyramid Edition"** — [`Vodrix/xcc`](https://github.com/Vodrix/xcc). Many ergonomic and stability improvements (better filetype detection, working fileview scroll, KB/MB/GB sizes in listviews, sane hotkeys, no more crashes on corrupted MIX headers, merged Tomsons/Omniblade fixes, cleaner global mix database, etc.). The `defe13a Overlay Vodrix fork onto tree, re-apply dark mode` commit in this repo brings his tree in.
- **This fork (`triatomic/XCC-Tacitus`)** — built on top of Vodrix's work.

Credit for the underlying app and the bulk of the modern improvements goes to Olaf van der Spek and Vodrix respectively. This fork's contribution is the additions described below.

## What's new in this fork

- **Dark mode** — a modern Windows 11-style dark theme across the whole app, with light/dark toggling (or follow your Windows setting automatically).
- **Built-in image & voxel viewer** — press `P` on a SHP, WSA, or VXL to preview it right in the file list: play/pause, step through frames, adjust speed, zoom with Ctrl+scroll, and drag to pan.
- **3D voxel viewer** — rotate VXL models freely, with adjustable lighting (brightness, direction, shadows), smoothing, and quality settings.
- **Animation playback for vehicles** — load a unit's animation (HVA) and watch the whole model move, including turrets and barrels that animate together.
- **Picture quality controls** — choose how images are scaled (sharp pixels or smooth), add optional sharpening, and cap the frame rate so playback doesn't strain your CPU.
- **Transparency preview** — show a sprite's transparent areas as a checkerboard instead of a solid color.
- **Save screenshots** — export the viewer image as PNG, TGA, or PCX, or copy it to the clipboard. Screenshots are saved at the zoom level you're viewing, and transparency is preserved.
- **Live palette switching** — change the color palette while previewing and watch the sprite recolor instantly.
- **Record animations** — save sprite or voxel animations as a GIF or a series of PNG images, including spinning turntables for voxels. Transparency is kept, and you can press ESC to stop early.
- **Three ways to search** — search the current archive, search recursively across both panes, or search your whole disk for game archives (via the Everything app).
- **Edit archives inside archives** — add, remove, or replace files inside a MIX that's nested within another MIX; your changes are saved back into the parent automatically.
- **Easier navigation** — forward/back history, a clickable breadcrumb trail, a quick filter box, and a list of recently opened files.
- **Smarter file extraction** — extract files in bulk, optionally keeping folder structure, with faster parallel saving.
- **Palette tools** — load palettes from a folder, cycle between related palettes, and pick palettes from a searchable list with live preview.
- **Customizable colors & columns** — a built-in color picker, plus the ability to show/hide and reorder list columns (remembered between sessions).
- **Custom keyboard shortcuts** — rebind almost any action to the keys or mouse buttons you prefer.
- **Automatic game-folder detection** — finds your installed C&C games automatically, including Steam copies.
- **Better file recognition** — more accurate detection of older Red Alert / Tiberian Dawn file types, and fixed video frame-rate detection.
- **Improved video player** — proper play/pause, adjustable speed, and scrubbing for SHP/WSA/VQA playback.
- **Smoother performance** — faster file scanning and a fix that stops the app from needlessly using a CPU core in the background.
- **Works without extras** — a fallback game database is built into the app, so it still recognizes files even if the database file is missing.

	


## Build

- Visual Studio 2022 (`v143` toolset), **Win32 only** (the projects are 32-bit MFC).
- vcpkg manifest mode (`vcpkg.json` at root). Run `vcpkg install` once. For static-linked Mixer/Library builds: `vcpkg install --triplet=x86-windows-static`.
- Master solution `XCC.sln` builds `XCC Library`, `XCC Mixer`, `XCC MIX Editor`, `XCC AV Player`, `XCC TMP Editor`, `XCC Editor`. Other apps each have their own `.sln`.
- CLI: `msbuild XCC.sln /p:Configuration=Release /p:Platform=Win32 /m`.

## License

Inherits the licensing of upstream XCC Utilities. See the lineage repos for terms.
