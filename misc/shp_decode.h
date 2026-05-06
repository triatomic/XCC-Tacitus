#pragma once

#include <virtual_binary.h>

int decode2(const byte* s, byte* d, int cb_s, const byte* reference_palette);
int RLEZeroTSDecompress(const byte* s, byte* d, int cx, int cy);
int RLEZeroTSCompress(const byte* s, byte* d, int cx, int cy);
int decode5(const byte* s, byte* d, int cb_s, int format);
int encode5(const byte* s, byte* d, int cb_s, int format);
int decode5s(const byte* s, byte* d, int cb_s);
int encode5s(const byte* s, byte* d, int cb_s);
Cvirtual_binary decode64(data_ref);
Cvirtual_binary encode64(data_ref);
int ApplyXORDelta(const byte* s, byte* d);
int LCWDecompress(void const* source, void* dest);
int GenerateXORDelta(const byte* last_s, const byte* s, byte* d, int cb_s);
int LCWCompress(const byte* s, byte* d, int cb_s);
int get_run_length(const byte* r, const byte* s_end);
