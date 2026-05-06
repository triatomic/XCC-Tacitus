#pragma once

#include <vartypes.h>

#pragma pack(push, 1)

struct t_palette_entry
{
	byte r, g, b;
};

struct t_palette48_entry
{
	unsigned short r, g, b;
};

struct t_palette64_entry
{
	unsigned short r, g, b, a;
};

union t_palette32_entry
{
	struct
	{
		byte r, g, b, a;
	};
	unsigned __int32 v;
};

union t_palette32bgr_entry
{
	struct
	{
		byte b, g, r, a;
	};
	unsigned __int32 v;
};

using t_palette = t_palette_entry[256];

void apply_rp(byte* d, int cb_d, const byte* rp);
void convert_image_8_to_24(const byte* s, byte* d, int cx, int cy, const t_palette palette);
void convert_image_24_to_8(const byte* s, byte* d, int cx, int cy, const byte* rp);
void convert_image_24_to_8(const byte* s, byte* d, int cx, int cy, const t_palette palette);
void convert_palette_18_to_24(const t_palette s, t_palette d);
void convert_palette_18_to_24(t_palette palette);
void convert_palette_24_to_18(const t_palette s, t_palette d);
void convert_palette_24_to_18(t_palette palette);
void create_downsample_table(const t_palette palette, byte* rp);
void create_rp(const t_palette s1, const t_palette s2, byte* d);
void downsample_image(const t_palette32_entry* s, byte* d, int cx, int cy, const byte* rp);
void downsample_image(const t_palette32_entry* s, byte* d, int cx, int cy, const t_palette palette);
void upsample_image(const byte* s, t_palette32_entry* d, int cx, int cy, const t_palette palette);
int find_color(int r, int g, int b, const t_palette palette);

#pragma pack(pop)
