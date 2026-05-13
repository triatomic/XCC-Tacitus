#pragma once

#include <cc_file_small.h>
#include <palette.h>

// Westwood VPL ("voxels.vpl") — per-game voxel lighting lookup table used by
// TS / RA2 / YR engines. Layout:
//   Header   (16 bytes):   remap_start (u32), remap_end (u32),
//                          section_count (u32, =32), unknown (u32)
//   Palette  (768 bytes):  256 x t_palette_entry, 8-bit RGB
//   Sections (32 x 256):   per-section 256-byte remap tables
// Total file size: 8976 bytes.
//
// section[s][c] is the palette index to use when palette color `c` is rendered
// at brightness section `s` (0 = brightest / normal facing the light,
// 31 = darkest / normal opposite the light). Some third-party docs describe
// the order inverted; the layout above was verified empirically by tracing
// remap entries in real voxels.vpl files. TS only meaningfully uses sections
// 0..15; RA2/YR uses all 32.
// The "unknown" dword and remap_start/remap_end are not consistently set by
// every generator (vxl-renderer leaves them as 0xffffffff / uninitialized),
// so is_valid() does not constrain them.
class Cvpl_file : public Ccc_file_small
{
public:
	enum
	{
		c_header_size = 16,
		c_palette_size = 256 * 3,
		c_section_count = 32,
		c_section_size = 256,
		c_total_size = c_header_size + c_palette_size + c_section_count * c_section_size,
	};

	bool is_valid() const;

	int section_count() const
	{
		return get_dword(8);
	}

	const t_palette_entry* get_palette() const
	{
		return reinterpret_cast<const t_palette_entry*>(data() + c_header_size);
	}

	const byte* get_section(int s) const
	{
		return data() + c_header_size + c_palette_size + s * c_section_size;
	}

	void decode_palette(t_palette& palette) const
	{
		convert_palette_18_to_24(get_palette(), palette);
	}

private:
	uint32_t get_dword(size_t offset) const
	{
		const byte* p = data() + offset;
		return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
	}
};
