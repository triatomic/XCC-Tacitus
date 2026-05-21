#pragma once

// Pixel-art aware upscalers operating on 32-bit BGRA buffers (DWORD per
// pixel; A in MSB, B in LSB). Each function reads `src` of size sw*sh and
// writes `dst` of size (sw*factor)*(sh*factor). dst must be pre-allocated
// by the caller. All functions are stateless and thread-safe per-buffer.
//
// Used by Mixer's SHP/WSA player to pre-scale the BGRA cache when the
// user picks one of the pixel-art interpolation modes (interp_scale2x ..
// interp_xbr4x in theme.h). The downstream stretch_image then bilinear-
// resamples from the upscaled cache to the viewport.
//
// Algorithm credits:
//   Scale2x/Scale3x — Andrea Mazzoleni, AdvanceMAME, public domain.
//     https://www.scale2x.it/algorithm
//   HQ2x/HQ4x      — Maxim Stepin, simplified single-file port.
//                     Original LGPL; the simplified weight-based
//                     reconstruction here is independent of Stepin's
//                     YUV-difference LUT.
//   xBR-2x/4x      — Hyllian; algorithm public domain. Compact port.

namespace pixel_upscale
{
	// BGRA-DWORD layout. sw/sh source size; dst must be (sw*factor)*(sh*factor).
	void scale2x(const unsigned int* src, int sw, int sh, unsigned int* dst);
	void scale3x(const unsigned int* src, int sw, int sh, unsigned int* dst);

	void hq2x  (const unsigned int* src, int sw, int sh, unsigned int* dst);
	void hq4x  (const unsigned int* src, int sw, int sh, unsigned int* dst);

	void xbr2x (const unsigned int* src, int sw, int sh, unsigned int* dst);
	void xbr4x (const unsigned int* src, int sw, int sh, unsigned int* dst);

	// NNEDI3 — scalar port of znedi3 (Tritical's neural-net upscaler).
	// Slow (4-8x xBR per pixel) but visibly higher quality on diagonal /
	// curved edges. SHP/WSA preview only; weights loaded once from the
	// NNEDI3_WEIGHTS RCDATA resource. See nnedi3.h for details.
	void nnedi2x(const unsigned int* src, int sw, int sh, unsigned int* dst);
	void nnedi4x(const unsigned int* src, int sw, int sh, unsigned int* dst);
}
