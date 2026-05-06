#pragma once

#include <palette.h>

void resize_image_down(const byte* s, byte* d, int cx, int cy, int c_planes, int cx_d, int cy_d);
void resize_image_up(const byte* s, byte* d, int cx, int cy, int c_planes, int cx_d, int cy_d);
void resize_image_down(const t_palette32_entry* s, t_palette32_entry* d, int cx, int cy, int cx_d, int cy_d);
void resize_image_up(const t_palette32_entry* s, t_palette32_entry* d, int cx, int cy, int cx_d, int cy_d);
