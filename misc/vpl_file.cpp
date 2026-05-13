#include "stdafx.h"
#include "vpl_file.h"

bool Cvpl_file::is_valid() const
{
	// Real-world voxels.vpl files (TS/RA2/YR + generator output like
	// vxl-renderer) are loose about every header field except size and
	// section_count: the "unknown" dword at offset 12 is often uninitialized
	// (0xcdcdcdcd from a debug allocator), remap_start/remap_end can be
	// 0xffffffff sentinels, and the embedded palette uses full 8-bit RGB
	// rather than the 6-bit VGA range Cpal_file enforces. Strict checks on
	// those fields rejected legitimate files with "File is not a valid VPL.";
	// vxl-renderer's loader only checks size, and that's the right call here.
	if (get_size() != c_total_size)
		return false;
	if (section_count() != c_section_count)
		return false;
	return true;
}
