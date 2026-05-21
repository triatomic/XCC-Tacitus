#pragma once

// NNEDI3 — neural-network edge-directed interpolation. Scalar C++ port of
// znedi3 (sekrit-twc), itself a fast port of Tritical's original NNEDI3
// AviSynth plugin. GPLv2 inherited from upstream.
//
// Operates as a 2x upscaler: the missing rows of a double-height image
// are filled by a small neural network that consumes a 32x6 luma window
// around each output pixel. Transposing between vertical and horizontal
// passes turns the deinterlacer into a 2D upscaler. Output looks better
// than any rule-based pixel-art kernel on diagonal/curved edges.
//
// We ship a single weight preset (nsize=32x6, nns=64, etype=ABS, qual=2)
// extracted from Tritical's nnedi3_weights.bin into data/nnedi3_weights_mixer.bin
// and embedded as RCDATA resource NNEDI3_WEIGHTS. About 200 KB.
//
// Cost: roughly 4-8x slower than xBR per output pixel. Acceptable for
// preview prefill, NOT for real-time playback — runs once per file load
// and the cached BGRA is reused across frames.

namespace nnedi3
{
	// One-time weights loader. Called lazily on first upscaler use; safe
	// to call multiple times (idempotent). Reads the embedded RCDATA
	// resource and prepares the prescreener + predictor coefficients
	// (including the subtract_mean normalization that's part of znedi3's
	// initialization). Thread-safe — first caller wins, others spin until
	// init completes. Returns true on success, false if the resource
	// couldn't be located (in which case the upscalers fall back to
	// nearest-neighbor, so caller never crashes).
	bool ensure_weights_loaded();

	// 2x upscale. src is sw*sh BGRA DWORDs; dst is pre-allocated for
	// 2*sw * 2*sh DWORDs. Output: NNEDI3 on the luma channel (BT.601
	// luma), bilinear on chroma. Pixel-art content uses palette colors
	// so the chroma simplification is invisible.
	void nnedi3_2x(const unsigned int* src, int sw, int sh, unsigned int* dst);

	// 4x = chained 2x. Allocates an intermediate buffer internally.
	void nnedi3_4x(const unsigned int* src, int sw, int sh, unsigned int* dst);
}
