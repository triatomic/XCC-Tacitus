#include "stdafx.h"
#include "shp_decode.h"

#include <lzo/lzo1x.h>
#include <virtual_binary.h>
#include "cc_structures.h"

static const char* encode64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int decode64_table[256] = 
{
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

static int read_w(const byte*& r)
{
	int v = *reinterpret_cast<const unsigned __int16*>(r);
	r += 2;
	return v;
}

static void write_w(int v, byte*& w)
{
	*w++ = v & 0xff;
	*w++ = v >> 8;
}

static void write_v40(byte v, int count, byte*& d)
{
	while (count)
	{
		if (v)
		{
			if (count < 0x100)
			{
				*d++ = 0x00;
				*d++ = count;
				*d++ = v;
				break;
			}
			int c_write = min<size_t>(count, 0x3fff);
			*d++ = 0x80;
			write_w(0xc000 | c_write, d);
			count -= c_write;
			*d++ = v;
		}
		else if (count < 0x80)
		{
			*d++ = 0x80 | count;
			count = 0;
		}
		else 
		{
			int c_write = count < 0x8000  ? count : 0x7fff;
			*d++ = 0x80;
			write_w(c_write, d);
			count -= c_write;
		}		
	}
}

int get_run_length(const byte* r, const byte* s_end)
{
	int count = 1;
	int v = *r++;
	while (r < s_end && *r++ == v)
		count++;
	return count;
}

static void write40_c0(byte*& w, int count, int v)
{
	*w++ = 0;
	*w++ = count;
	*w++ = v;
}

static void write40_c1(byte*& w, int count, const byte* r)
{
	*w++ = count;
	memcpy(w, r, count);
	w += count;
}

static void write40_c2(byte*& w, int count)
{
	*w++ = 0x80;
	write_w(count, w);
}

static void write40_c3(byte*& w, int count, const byte* r)
{
	*w++ = 0x80;
	write_w(0x8000 | count, w);
	memcpy(w, r, count);
	w += count;
}

static void write40_c4(byte*& w, int count, int v)
{
	*w++ = 0x80;
	write_w(0xc000 | count, w);
	*w++ = v;
}

static void write40_c5(byte*& w, int count)
{
	*w++ = 0x80 | count;
}

static void write40_copy(byte*& w, int count, const byte* r)
{
	while (count)
	{
		if (count < 0x80)
		{
			write40_c1(w, count, r);
			count = 0;
		}
		else
		{
			int c_write = count < 0x4000 ? count : 0x3fff;
			write40_c3(w, c_write, r);
			r += c_write;
			count -= c_write;
		}
	}
}

static void write40_fill(byte*& w, int count, int v)
{
	while (count)
	{
		if (count < 0x100)
		{
			write40_c0(w, count, v);
			count = 0;
		}
		else
		{
			int c_write = count < 0x4000 ? count : 0x3fff;
			write40_c4(w, c_write, v);
			count -= c_write;
		}
	}
}

static void write40_skip(byte*& w, int count)
{
	while (count)
	{
		if (count < 0x80)
		{
			write40_c5(w, count);
			count = 0;
		}
		else
		{
			int c_write = count < 0x8000 ? count : 0x7fff;
			write40_c2(w, c_write);
			count -= c_write;
		}
	}
}

static void flush_copy(byte*& w, const byte* r, const byte*& copy_from)
{
	if (copy_from)
	{
		write40_copy(w, r - copy_from, copy_from);
		copy_from = NULL;
	}
}

#define XOR_SMALL		127
#define XOR_MED			255
#define XOR_LARGE		16383
#define XOR_MAX			32767

int GenerateXORDelta(const byte* last_s, const byte* x, byte* d, int cb_s) //Generate XOR Delta
{
	byte* putp = static_cast<byte*>(d);	//our delta
	byte const* getsp = static_cast<byte const*>(x);	//This is the image we go to
	byte const* getbp = static_cast<byte const*>(last_s);	//This is image we come from
	byte const* getsendp = getsp + cb_s;

	//only check getsp. Both source and base should be same size and both pointers
	//should be incremented at the same time.
	while (getsp < getsendp) {
		unsigned int fillcount = 0;
		unsigned int xorcount = 0;
		unsigned int skipcount = 0;
		byte lastxor = *getsp ^ *getbp;
		byte const* testsp = getsp;
		byte const* testbp = getbp;

		//Only evaluate other options if we don't have a matched pair
		while (*testsp != *testbp && testsp < getsendp) {
			if ((*testsp ^ *testbp) == lastxor) {
				++fillcount;
				++xorcount;
			}
			else {
				if (fillcount > 3) {
					break;
				}
				else {
					lastxor = *testsp ^ *testbp;
					fillcount = 1;
					++xorcount;
				}
			}
			++testsp;
			++testbp;
		}

		fillcount = fillcount > 3 ? fillcount : 0;

		//Okay, lets see if we have any xor bytes we need to handle
		xorcount -= fillcount;
		while (xorcount) {
			uint16_t count = 0;
			//cmd 0???????
			if (xorcount < XOR_MED) {
				count = xorcount <= XOR_SMALL ? xorcount : XOR_SMALL;
				*putp++ = count;
				//cmd 10000000 10?????? ??????
			}
			else {
				count = xorcount <= XOR_LARGE ? xorcount : XOR_LARGE;
				*putp++ = 0x80;
				*putp++ = count;
				*putp++ = (count >> 8) | 0x80;
			}

			while (count) {
				*putp++ = *getsp++ ^ *getbp++;
				--count;
				--xorcount;
			}
		}

		//lets handle the bytes that are best done as xorfill
		while (fillcount) {
			uint16_t count = 0;
			//cmd 00000000 ????????
			if (fillcount <= XOR_MED) {
				count = fillcount;
				*putp++ = 0;
				*putp++ = count;
				//cmd 10000000 11?????? ??????
			}
			else {
				count = fillcount <= XOR_LARGE ? fillcount : XOR_LARGE;
				*putp++ = 0x80;
				*putp++ = count;
				*putp++ = (count >> 8) | 0xC0;
			}

			*putp++ = *getsp ^ *getbp;
			fillcount -= count;
			getsp += count;
			getbp += count;
		}

		//Handle regions that match exactly
		while (*testsp == *testbp && testsp < getsendp) {
			++skipcount;
			++testsp;
			++testbp;
		}

		while (skipcount) {
			uint16_t count = 0;
			if (skipcount < XOR_MED) {
				count = skipcount <= XOR_SMALL ? skipcount : XOR_SMALL;
				*putp++ = count | 0x80;
				//cmd 10000000 0??????? ????????
			}
			else {
				count = skipcount <= XOR_MAX ? skipcount : XOR_MAX;
				*putp++ = 0x80;
				*putp++ = count;
				*putp++ = count >> 8;
			}

			skipcount -= count;
			getsp += count;
			getbp += count;
		}

	}

	//final skip command of 0;
	*putp++ = 0x80;
	*putp++ = 0;
	*putp++ = 0;

	return putp - static_cast<byte*>(d);
}


int ApplyXORDelta(const byte* s, byte* d)	//Apply XOR Delta
{
	 unsigned char* putp = (unsigned char*)(d);
    const unsigned char* getp = (const unsigned char*)(s);
    unsigned short count = 0;
    unsigned char value = 0;
    unsigned char cmd = 0;

    while (true) {
        bool xorval = false;
        cmd = *getp++;
        count = cmd;

        if (!(cmd & 0x80)) {
            // 0b00000000
            if (cmd == 0) {
                count = *getp++ & 0xFF;
                value = *getp++;
                xorval = true;
                // 0b0???????
            }
        } else {
            // 0b1??????? remove most significant bit
            count &= 0x7F;
            if (count != 0) {
                putp += count;
                continue;
            }

            count = (*getp & 0xFF) + (*(getp + 1) << 8);
            getp += 2;

            // 0b10000000 0 0
            if (count == 0) {
                break;
            }

            // 0b100000000 0?
            if ((count & 0x8000) == 0) {
                putp += count;
                continue;
            } else {
                // 0b10000000 11
                if (count & 0x4000) {
                    count &= 0x3FFF;
                    value = *getp++;
                    xorval = true;
                    // 0b10000000 10
                } else {
                    count &= 0x3FFF;
                }
            }
        }

        if (xorval) {
            for (; count > 0; --count) {
                *putp++ ^= value;
            }
        } else {
            for (; count > 0; --count) {
                *putp++ ^= *getp++;
            }
        }
    }
		return putp - d;
}

static void write_v80(byte v, int count, byte*& d)
{
	if (count > 3)
	{
		*d++ = 0xfe;
		write_w(count, d);
		*d++ = v;
	}
	else if (count)
	{
		*d++ = 0x80 | count;
		while (count--)
			*d++ = v;
	}
}
#include <cstdint>

void get_same(const byte* s, const byte* r, const byte* s_end, byte*& p, int& cb_p)
{
	const byte* s_end_ptr = s_end;
	const byte* s_ptr = s;
	int ecx_value = 0;
	byte* edi_ptr = p;
	*edi_ptr = static_cast<byte>(ecx_value);
	s_ptr--;

next_s:
	s_ptr++;
	int edx_value = 0;
	const byte* esi_ptr = r;
	const byte* edi_val = s_ptr;
	if (edi_val >= esi_ptr)
		goto end0;

next0:
	edx_value++;
	if (esi_ptr >= s_end_ptr)
		goto end_line;

	if (*esi_ptr == *s_ptr)
		goto next0;

end_line:
	edx_value--;
	if (edx_value < ecx_value)
		goto next_s;

	ecx_value = edx_value;
	edi_ptr = p;
	*edi_ptr = static_cast<byte>(*s_ptr);
	goto next_s;

end0:
	edi_ptr = reinterpret_cast<byte*>(&cb_p);
	*edi_ptr = ecx_value;
	// Restore registers
}

static void write80_c0(byte*& w, int count, int p)
{
	*w++ = (count - 3) << 4 | p >> 8;
	*w++ = p & 0xff;
}

static void write80_c1(byte*& w, int count, const byte* r)
{
	do
	{
		int c_write = count < 0x40 ? count : 0x3f;
		*w++ = 0x80 | c_write;
		memcpy(w, r, c_write);
		r += c_write;
		w += c_write;
		count -= c_write;
	}
	while (count);
}

static void write80_c2(byte*& w, int count, int p)
{
	*w++ = 0xc0 | (count - 3);
	write_w(p, w);
}

static void write80_c3(byte*& w, int count, int v)
{
	*w++ = 0xfe;
	write_w(count, w);
	*w++ = v;
}

static void write80_c4(byte*& w, int count, int p)
{
	*w++ = 0xff;
	write_w(count, w);
	write_w(p, w);
}

static void flush_c1(byte*& w, const byte* r, const byte*& copy_from)
{
	if (copy_from)
	{
		write80_c1(w, r - copy_from, copy_from);
		copy_from = NULL;
	}
}

int LCWCompress(const byte* s, byte* d, int cb_s)	//LCW Compress
{
	if (!cb_s) {
		return 0;
	}

	bool relative = false;

	const byte* getp = static_cast<const byte*>(s);
	byte* putp = static_cast<byte*>(d);
	const byte* getstart = getp;
	const byte* getend = getp + cb_s;
	byte* putstart = putp;

	//Write a starting cmd1 and set bool to have cmd1 in progress
	byte* cmd_onep = putp;
	*putp++ = 0x81;
	*putp++ = *getp++;
	bool cmd_one = true;

	//Compress data
	while (getp < getend) {
		//Is RLE encode (4bytes) worth evaluating?
		if (getend - getp > 64 && *getp == *(getp + 64)) {
			//RLE run length is encoded as a short so max is UINT16_MAX
			const byte* rlemax = (getend - getp) < UINT16_MAX ? getend : getp + UINT16_MAX;
			const byte* rlep;

			for (rlep = getp + 1; *rlep == *getp && rlep < rlemax; ++rlep);

			uint16_t run_length = rlep - getp;

			//If run length is long enough, write the command and start loop again
			if (run_length >= 0x41) {
				//write 4byte command 0b11111110
				cmd_one = false;
				*putp++ = 0xFE;
				*putp++ = run_length;
				*putp++ = run_length >> 8;
				*putp++ = *getp;
				getp = rlep;
				continue;
			}
		}

		//current block size for an offset copy
		int block_size = 0;
		const byte* offstart;

		//Set where we start looking for matching runs.
		if (relative) {
			offstart = (getp - getstart) < UINT16_MAX ? getstart : getp - UINT16_MAX;
		}
		else {
			offstart = getstart;
		}

		//Look for matching runs
		const byte* offchk = offstart;
		const byte* offsetp = getp;
		while (offchk < getp) {
			//Move offchk to next matching position
			while (offchk < getp && *offchk != *getp) {
				++offchk;
			}

			//If the checking pointer has reached current pos, break
			if (offchk >= getp) {
				break;
			}

			//find out how long the run of matches goes for
			int i;
			for (i = 1; &getp[i] < getend; ++i) {
				if (offchk[i] != getp[i]) {
					break;
				}
			}

			if (i >= block_size) {
				block_size = i;
				offsetp = offchk;
			}

			++offchk;
		}

		//decide what encoding to use for current run
		if (block_size <= 2) {
			//short copy 0b10??????
			//check we have an existing 1 byte command and if its value is still
			//small enough to handle additional bytes
			//start a new command if current one doesn't have space or we don't
			//have one to continue
			if (cmd_one && *cmd_onep < 0xBF) {
				//increment command value
				++*cmd_onep;
				*putp++ = *getp++;
			}
			else {
				cmd_onep = putp;
				*putp++ = 0x81;
				*putp++ = *getp++;
				cmd_one = true;
			}
		}
		else {
			uint16_t offset;
			uint16_t rel_offset = getp - offsetp;
			if (block_size > 0xA || (rel_offset > 0xFFF)) {
				//write 5 byte command 0b11111111
				if (block_size > 0x40) {
					*putp++ = 0xFF;
					*putp++ = block_size;
					*putp++ = block_size >> 8;
					//write 3 byte command 0b11??????
				}
				else {
					*putp++ = (block_size - 3) | 0xC0;
				}

				offset = relative ? rel_offset : offsetp - getstart;
				//write 2 byte command? 0b0???????
			}
			else {
				offset = rel_offset << 8 | (16 * (block_size - 3) + (rel_offset >> 8));
			}
			*putp++ = offset;
			*putp++ = offset >> 8;
			getp += block_size;
			cmd_one = false;
		}
	}

	//write final 0x80, this is why its also known as format80 compression
	*putp++ = 0x80;
	return putp - putstart;
}


int LCWDecompress(void const* source, void* dest)	//LCW Decompress
{
	unsigned char* source_ptr, * dest_ptr, * copy_ptr, op_code;
	unsigned count;

	/* Copy the source and destination ptrs. */
	source_ptr = (unsigned char*)source;
	dest_ptr = (unsigned char*)dest;
	//dest_end = dest_ptr + sizeof(dest);

	while (true) {

		/* Read in the operation code. */
		op_code = *source_ptr++;

		if (!(op_code & 0x80)) {

			/* Do a short copy from destination. */
			count = (op_code >> 4) + 3;
			copy_ptr = dest_ptr - ((unsigned)*source_ptr++ + (((unsigned)op_code & 0x0f) << 8));

			/* Check we aren't going to write past the end of the destination buffer */
			//if (count > (unsigned)(dest_end - dest_ptr)) {
			//	count = dest_end - dest_ptr;
			//}

			while (count--)
				*dest_ptr++ = *copy_ptr++;

		}
		else {

			if (!(op_code & 0x40)) {

				if (op_code == 0x80) {

					/* Return # of destination bytes written. */
					return (int)(dest_ptr - (unsigned char*)dest);

				}
				else {

					/* Do a medium copy from source. */
					count = op_code & 0x3f;

					/* Check we aren't going to write past the end of the destination buffer */
					//if (count > (unsigned)(dest_end - dest_ptr)) {
					//	count = dest_end - dest_ptr;
					//}

					while (count--)
						*dest_ptr++ = *source_ptr++;
				}

			}
			else {

				if (op_code == 0xfe) {

					/* Do a long run. */
					count = *source_ptr++;
					count += (*source_ptr++) << 8;

					//if (count > (unsigned)(dest_end - dest_ptr)) {
					//	count = dest_end - dest_ptr;
					//}

					memset(dest_ptr, (*source_ptr++), count);
					dest_ptr += count;

				}
				else {

					if (op_code == 0xff) {

						/* Do a long copy from destination. */
						count = *source_ptr++;
						count += (*source_ptr++) << 8;
						copy_ptr = (unsigned char*)dest + *source_ptr++;
						copy_ptr += (*source_ptr++) << 8;

						//if (count > (unsigned)(dest_end - dest_ptr)) {
						//	count = dest_end - dest_ptr;
						//}

						while (count--)
							*dest_ptr++ = *copy_ptr++;

					}
					else {

						/* Do a medium copy from destination. */
						count = (op_code & 0x3f) + 3;
						copy_ptr = (unsigned char*)dest + *source_ptr + ((unsigned)*(source_ptr + 1) << 8);
						source_ptr += 2;

						//if (count > (unsigned)(dest_end - dest_ptr)) {
						//	count = dest_end - dest_ptr;
						//}

						while (count--)
							*dest_ptr++ = *copy_ptr++;
					}
				}
			}
		}
	}

	return (int)(dest_ptr - (unsigned char*)dest);
}

int decode2(const byte* s, byte* d, int cb_s, const byte* reference_palette)
{
	const byte* r = s;
	const byte* r_end = s + cb_s;
	byte* w = d;
	while (r < r_end)
	{
		int v = *r++;
		if (v)
			*w++ = v;
		else
		{
			v = *r++;
			memset(w, 0, v);
			w += v;
		}
	}
	if (reference_palette)
		apply_rp(d, w - d, reference_palette);
	return w - d;
}

int RLEZeroTSDecompress(const byte* s, byte* d, int cx, int cy)	//RLE Zero TS Decompress
{
	const byte* r = s;
	byte* w = d;
	for (int y = 0; y < cy; y++)
	{
		int count = *reinterpret_cast<const unsigned __int16*>(r) - 2;
		r += 2;
		int x = 0;
		while (count--)
		{
			int v = *r++;
			if (v)
			{
				x++;
				*w++ = v;
			}
			else
			{
				count--;
				v = *r++;
				if (x + v > cx)
					v = cx - x;
				x += v;
				while (v--)
					*w++ = 0;
			}
		}
	}
	return w - d;
}

int RLEZeroTSCompress(const byte* s, byte* d, int cx, int cy)	//RLE Zero TS Compress
{
	const byte* r = s;
	byte* w = d;
	for (int y = 0; y < cy; y++)
	{
		const byte* r_end = r + cx;
		byte* w_line = w;
		w += 2;
		while (r < r_end)
		{
			
			int v = *r;
			*w++ = v;
			if (v)
				r++;
			else
			{
				int c = get_run_length(r, r_end);
				if (c > 0xff)
					c = 0xff;
				r += c;
				*w++ = c;
			}
		}
		*reinterpret_cast<unsigned __int16*>(w_line) = w - w_line;
	}
	return w - d;
}

Cvirtual_binary encode64(data_ref s)
{
	Cvirtual_binary d;
	const byte* r = s.data();
	int cb_s = s.size();
    byte* w = d.write_start(s.size() << 1);
    while (cb_s) 
	{		
		int c1 = *r++;
		*w++ = encode64_table[c1>>2];
		
		int c2 = --cb_s == 0 ? 0 : *r++;
		*w++ = encode64_table[((c1 & 0x3) << 4) | ((c2 & 0xf0) >> 4)];		
		if (cb_s == 0) 
		{
			*w++ = '=';
			*w++ = '=';
			break;
		}
		
		int c3 = --cb_s == 0 ? 0 : *r++;
		
		*w++ = encode64_table[((c2 & 0xf) << 2) | ((c3 & 0xc0) >> 6)];
		if (cb_s == 0) 
		{
			*w++ = '=';
			break;
		}		
		--cb_s;
		*w++ = encode64_table[c3 & 0x3f];
    }	
	d.set_size(w - d.data());
    return d;
}

Cvirtual_binary decode64(data_ref s)
{
	Cvirtual_binary d;
	const byte* r = s.data();
    byte* w = d.write_start(s.size() << 1);
    while (*r)
	{
		int c1 = *r++;
		if (decode64_table[c1] == -1) 
			return Cvirtual_binary();
		int c2 = *r++;
		if (decode64_table[c2] == -1) 
			return Cvirtual_binary();
		int c3 = *r++;
		if (c3 != '=' && decode64_table[c3] == -1) 
			return Cvirtual_binary();
		int c4 = *r++;
		if (c4 != '=' && decode64_table[c4] == -1) 
			return Cvirtual_binary();
		*w++ = (decode64_table[c1] << 2) | (decode64_table[c2] >> 4);
		if (c3 == '=') 
			break;
		*w++ = ((decode64_table[c2] << 4) & 0xf0) | (decode64_table[c3] >> 2);
		if (c4 == '=') 
			break;
		*w++ = ((decode64_table[c3] << 6) & 0xc0) | decode64_table[c4];
    }	
	d.set_size(w - d.data());
	return d;
}

static void write5_count(byte*& w, int count)
{
	while (count > 255)
	{
		*w++ = 0;
		count -= 255;
	}
	*w++ = count;
}

static void write5_c0(byte*& w, int count, const byte* r, byte* small_copy)
{
	if (count < 4 && !small_copy)
		count = count;
	if ((count < 4 || count > 7) && small_copy)
	{
		int small_count = min<size_t>(count, 3);
		*small_copy |= small_count;
		memcpy(w, r, small_count);
		r += small_count;
		w += small_count;
		count -= small_count;
	}
	if (count)
	{
		assert(count > 3);
		if (count > 18)
		{
			*w++ = 0;
			write5_count(w, count - 18);
		}
		else 
			*w++ = count - 3;
		memcpy(w, r, count);
		w += count;
	}
}

static void write5_c1(byte*& w, int count, int p)
{
	assert(count > 2);
	assert(p >= 0);
	assert(p < 32768);
	count -= 2;
	if (count > 7)
	{
		*w++ = 0x10 | (p >> 11) & 8;
		write5_count(w, count - 7);
	}
	else
		*w++ = 0x10 | (p >> 11) & 8 | count;
	write_w((p << 2) & 0xfffc, w);
}

static void write5_c2(byte*& w, int count, int p)
{
	assert(count > 2);
	assert(p > 0);
	assert(p <= 16384);
	count -= 2;
	p--;
	if (count > 31)
	{
		*w++ = 0x20;
		write5_count(w, count - 31);
	}
	else
		*w++ = 0x20 | count;
	write_w(p << 2, w);
}

static void write5_c3(byte*& w, int count, int p)
{
	assert(count > 1);
	assert(count < 7);
	assert(p > 0);
	assert(p <= 2048);
	count -= 2;
	p--;
	*w++ = (count + 1) << 5 | (p & 7) << 2;
	*w++ = p >> 3;
}

int get_count(const byte*& r)
{
	int count = -255;
	int v;
	do
	{
		count += 255;
		v = *r++;
	}
	while (!v);
	return count + v;
}

static void flush_c0(byte*& w, const byte* r, const byte*& copy_from, byte* small_copy, bool start)
{
	if (copy_from)
	{
		int count = r - copy_from;
		/*
		if (start)
		{
			int small_count = count;
			if (count > 241)
				small_count = 238;
			else if (count > 238)
				small_count = count - 4;
			*w++ = small_count + 17;
			memcpy(w, copy_from, small_count);
			copy_from += small_count;
			w += small_count;
			count -= small_count;
		}
		*/
		if (count)
			write5_c0(w, count, copy_from, small_copy);
		copy_from = NULL;
	}
}

int encode5s(const byte* s, byte* d, int cb_s)
{
	lzo_init();
	static Cvirtual_binary t;
	lzo_uint cb_d;
	if (LZO_E_OK != lzo1x_1_compress(s, cb_s, d, &cb_d, t.write_start(LZO1X_1_MEM_COMPRESS)))
		cb_d = 0;
	return cb_d;
}

int encode5s_z(const byte* s, byte* d, int cb_s)
{
	// no compression
	const byte* r = s;
	const byte* r_end = s + cb_s;
	byte* w = d;
	write5_c0(w, cb_s, r, NULL);
	r += cb_s;
	write5_c1(w, 3, 0);
	assert(cb_s == r - s);
	return w - d;
}

int decode5s(const byte* s, byte* d, int cb_s)
{
	lzo_init();
	lzo_uint cb_d;
	if (LZO_E_OK != lzo1x_decompress(s, cb_s, d, &cb_d, NULL))
		return 0;
	return cb_d;
	/*
	0 copy 0000cccc
	1 copy 0001pccc ppppppzz p
	2 copy 001ccccc ppppppzz p
	3 copy cccpppzz p
	*/
	
	const byte* c;
	const byte* r = s;
	byte* w = d;
	int code;
	int count;
	code = *r;
	if (code > 17)
	{
		r++;
		count = code - 17;
		while (count--)
			*w++ = *r++;
	}
	while (1)
	{
		code = *r++;
		if (code & 0xf0)
		{
			if (code & 0xc0)
			{
				count = (code >> 5) - 1;
				c = w - (code >> 2 & 7);
				c -= *r++ << 3;
				c--;
			}
			else if (code & 0x20)
			{
				count = code & 0x1f;
				if (!count)
					count = get_count(r) + 31;
				c = w - (read_w(r) >> 2);
				c--;
			}
			else
			{
				c = w - ((code & 8) << 11);
				count = code & 7;
				if (!count)
					count = get_count(r) + 7;
				c -= read_w(r) >> 2;
				if (c == w)
					break;
			}
			count += 2;
			while (count--)
			{
				if (*w != *c)
					*w = *c;
				w++;
				c++;
			}
			count = *(r - 2) & 3;
			while (count--)
			{
				if (*w != *r)
					*w = *r;
				w++;
				r++;
			}
		}
		else
		{
			count = code ? code + 3: get_count(r) + 18;
			while (count--)
			{
				if (*w != *r)
					*w = *r;
				w++;
				r++;
			}
		}
	}
	assert(cb_s == r - s);
	return w - d;
}

int encode5(const byte* s, byte* d, int cb_s, int format)
{
	const byte* r = s;
	const byte* r_end = s + cb_s;
	byte* w = d;
	while (r < r_end)
	{
		int cb_section = min<size_t>(r_end - r, 8192);
		t_pack_section_header& header = *reinterpret_cast<t_pack_section_header*>(w);
		w += sizeof(t_pack_section_header);
		w += header.size_in = format == 80 ? LCWCompress(r, w, cb_section) : encode5s(r, w, cb_section);
		r += header.size_out = cb_section;
	}
	return w - d;
}

int decode5(const byte* s, byte* d, int cb_s, int format)
{
	const byte* r = s;
	const byte* r_end = s + cb_s;
	byte* w = d;
	while (r < r_end)
	{
		const t_pack_section_header& header = *reinterpret_cast<const t_pack_section_header*>(r);
		r += sizeof(t_pack_section_header);
		if (format == 80)
			LCWDecompress(r, w);
		else
			decode5s(r, w, header.size_in);
		r += header.size_in;
		w += header.size_out;
	}
	return w - d;
}
