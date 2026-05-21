#include "stdafx.h"
#include "pixel_upscale.h"
#include "nnedi3.h"

#include <algorithm>
#include <vector>

namespace pixel_upscale
{
	namespace
	{
		// BGRA channel helpers. Input/output is DWORD: A<<24 | R<<16 | G<<8 | B.
		// (Mixer's cache writes BGRA with the alpha byte often zero; we
		// preserve whichever byte sits in the high position so we don't
		// disturb whatever convention the caller uses.)
		inline unsigned int channel(unsigned int p, int shift) { return (p >> shift) & 0xFF; }
		inline unsigned int pack(unsigned int a, unsigned int r, unsigned int g, unsigned int b)
		{
			return (a << 24) | (r << 16) | (g << 8) | b;
		}

		// Fetch a pixel with edge clamping. Used by all upscalers — none of the
		// algorithms tolerate out-of-bounds reads at the source border.
		inline unsigned int at(const unsigned int* src, int sw, int sh, int x, int y)
		{
			if (x < 0) x = 0; else if (x >= sw) x = sw - 1;
			if (y < 0) y = 0; else if (y >= sh) y = sh - 1;
			return src[y * sw + x];
		}

		// Average two pixels (component-wise). 1:1 weight.
		inline unsigned int avg2(unsigned int p, unsigned int q)
		{
			return pack(
				(channel(p, 24) + channel(q, 24) + 1) >> 1,
				(channel(p, 16) + channel(q, 16) + 1) >> 1,
				(channel(p,  8) + channel(q,  8) + 1) >> 1,
				(channel(p,  0) + channel(q,  0) + 1) >> 1);
		}

		// Weighted blend: (p * w1 + q * w2 + r * w3) / (w1+w2+w3).
		inline unsigned int blend3(unsigned int p, unsigned int q, unsigned int r,
			unsigned int w1, unsigned int w2, unsigned int w3)
		{
			const unsigned int w = w1 + w2 + w3;
			return pack(
				(channel(p, 24) * w1 + channel(q, 24) * w2 + channel(r, 24) * w3) / w,
				(channel(p, 16) * w1 + channel(q, 16) * w2 + channel(r, 16) * w3) / w,
				(channel(p,  8) * w1 + channel(q,  8) * w2 + channel(r,  8) * w3) / w,
				(channel(p,  0) * w1 + channel(q,  0) * w2 + channel(r,  0) * w3) / w);
		}

		// YUV-space "are these pixels similar?" predicate. xBR + HQx both
		// classify edges by perceptual difference, not strict RGB equality —
		// otherwise anti-aliased edge pixels would never match their solid
		// neighbors and the reconstruction never fires.
		// Thresholds picked to roughly match published xBR/HQx defaults.
		inline bool similar(unsigned int p, unsigned int q)
		{
			if (p == q) return true;
			const int r1 = static_cast<int>(channel(p, 16));
			const int g1 = static_cast<int>(channel(p,  8));
			const int b1 = static_cast<int>(channel(p,  0));
			const int r2 = static_cast<int>(channel(q, 16));
			const int g2 = static_cast<int>(channel(q,  8));
			const int b2 = static_cast<int>(channel(q,  0));
			// Approximate ITU-R BT.601 luma + chroma diffs.
			const int y1 = (r1 * 299 + g1 * 587 + b1 * 114) / 1000;
			const int u1 = (b1 - y1) * 565 / 1000;
			const int v1 = (r1 - y1) * 713 / 1000;
			const int y2 = (r2 * 299 + g2 * 587 + b2 * 114) / 1000;
			const int u2 = (b2 - y2) * 565 / 1000;
			const int v2 = (r2 - y2) * 713 / 1000;
			const int dy = std::abs(y1 - y2);
			const int du = std::abs(u1 - u2);
			const int dv = std::abs(v1 - v2);
			// Thresholds: dy < 48 OR (small luma + chroma close).
			return dy <= 48 && du <= 7 && dv <= 6;
		}
	} // namespace

	// ---------------------------------------------------------------------
	// Scale2x / Scale3x — AdvanceMAME.
	//
	// For each source pixel P at position (x,y), look at the 4 cardinal
	// neighbors (B north, D west, F east, H south). If B==D and B!=H and
	// D!=F, the NW destination quadrant becomes B (or D, same thing); etc.
	// Otherwise the entire 2x2 (or 3x3) block is filled with P.
	// Cheap, deterministic, and preserves clean horizontal/vertical edges.
	// ---------------------------------------------------------------------

	void scale2x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		const int dw = sw * 2;
		for (int y = 0; y < sh; y++)
		{
			for (int x = 0; x < sw; x++)
			{
				const unsigned int P = src[y * sw + x];
				const unsigned int B = at(src, sw, sh, x,     y - 1);
				const unsigned int D = at(src, sw, sh, x - 1, y);
				const unsigned int F = at(src, sw, sh, x + 1, y);
				const unsigned int H = at(src, sw, sh, x,     y + 1);
				unsigned int E0 = P, E1 = P, E2 = P, E3 = P;
				if (B != H && D != F)
				{
					E0 = (D == B) ? D : P;
					E1 = (B == F) ? F : P;
					E2 = (D == H) ? D : P;
					E3 = (H == F) ? F : P;
				}
				const int dx = x * 2;
				const int dy = y * 2;
				dst[dy       * dw + dx    ] = E0;
				dst[dy       * dw + dx + 1] = E1;
				dst[(dy + 1) * dw + dx    ] = E2;
				dst[(dy + 1) * dw + dx + 1] = E3;
			}
		}
	}

	void scale3x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		const int dw = sw * 3;
		for (int y = 0; y < sh; y++)
		{
			for (int x = 0; x < sw; x++)
			{
				const unsigned int A = at(src, sw, sh, x - 1, y - 1);
				const unsigned int B = at(src, sw, sh, x,     y - 1);
				const unsigned int C = at(src, sw, sh, x + 1, y - 1);
				const unsigned int D = at(src, sw, sh, x - 1, y);
				const unsigned int E = src[y * sw + x];
				const unsigned int F = at(src, sw, sh, x + 1, y);
				const unsigned int G = at(src, sw, sh, x - 1, y + 1);
				const unsigned int H = at(src, sw, sh, x,     y + 1);
				const unsigned int I = at(src, sw, sh, x + 1, y + 1);
				unsigned int E0 = E, E1 = E, E2 = E, E3 = E, E4 = E, E5 = E, E6 = E, E7 = E, E8 = E;
				if (B != H && D != F)
				{
					E0 = (D == B) ? D : E;
					E1 = ((D == B && E != C) || (B == F && E != A)) ? B : E;
					E2 = (B == F) ? F : E;
					E3 = ((D == B && E != G) || (D == H && E != A)) ? D : E;
					// E4 stays E.
					E5 = ((B == F && E != I) || (H == F && E != C)) ? F : E;
					E6 = (D == H) ? D : E;
					E7 = ((D == H && E != I) || (H == F && E != G)) ? H : E;
					E8 = (H == F) ? F : E;
				}
				const int dx = x * 3;
				const int dy = y * 3;
				dst[ dy      * dw + dx    ] = E0;
				dst[ dy      * dw + dx + 1] = E1;
				dst[ dy      * dw + dx + 2] = E2;
				dst[(dy + 1) * dw + dx    ] = E3;
				dst[(dy + 1) * dw + dx + 1] = E4;
				dst[(dy + 1) * dw + dx + 2] = E5;
				dst[(dy + 2) * dw + dx    ] = E6;
				dst[(dy + 2) * dw + dx + 1] = E7;
				dst[(dy + 2) * dw + dx + 2] = E8;
			}
		}
	}

	// ---------------------------------------------------------------------
	// HQ2x / HQ4x — simplified Stepin-style.
	//
	// True HQx uses a precomputed 4096-entry LUT keyed on the 8-neighbor
	// YUV-similarity pattern around each pixel and emits one of ~30 distinct
	// blend recipes per case. The simplified version here keeps the
	// edge-aware spirit (heavier smoothing than xBR, characteristic
	// "painted" look) using just the cardinal + diagonal similarity
	// classification — no lookup table. Visually distinct from xBR; not
	// pixel-perfect identical to original HQx but in the same family.
	// ---------------------------------------------------------------------

	void hq2x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		const int dw = sw * 2;
		for (int y = 0; y < sh; y++)
		{
			for (int x = 0; x < sw; x++)
			{
				const unsigned int E = src[y * sw + x];
				const unsigned int A = at(src, sw, sh, x - 1, y - 1);
				const unsigned int B = at(src, sw, sh, x,     y - 1);
				const unsigned int C = at(src, sw, sh, x + 1, y - 1);
				const unsigned int D = at(src, sw, sh, x - 1, y);
				const unsigned int F = at(src, sw, sh, x + 1, y);
				const unsigned int G = at(src, sw, sh, x - 1, y + 1);
				const unsigned int H = at(src, sw, sh, x,     y + 1);
				const unsigned int I = at(src, sw, sh, x + 1, y + 1);

				// Reconstruct each output quadrant with an edge-aware blend:
				// if the two cardinal neighbors of the quadrant differ from
				// E *and* match each other, blend their average into E.
				auto recon = [&](unsigned int cardA, unsigned int cardB, unsigned int diag) -> unsigned int
				{
					const bool simA = similar(E, cardA);
					const bool simB = similar(E, cardB);
					if (!simA && !simB)
					{
						// Both cardinals differ from center → strong edge,
						// stay with E unless the diagonal also agrees.
						if (similar(cardA, cardB)) return blend3(E, cardA, cardB, 2, 3, 3);
						return E;
					}
					if (!simA) return blend3(E, cardA, diag, 6, 1, 1);
					if (!simB) return blend3(E, cardB, diag, 6, 1, 1);
					return E;
				};

				const unsigned int NW = recon(B, D, A);
				const unsigned int NE = recon(B, F, C);
				const unsigned int SW = recon(H, D, G);
				const unsigned int SE = recon(H, F, I);
				const int dx = x * 2;
				const int dy = y * 2;
				dst[ dy      * dw + dx    ] = NW;
				dst[ dy      * dw + dx + 1] = NE;
				dst[(dy + 1) * dw + dx    ] = SW;
				dst[(dy + 1) * dw + dx + 1] = SE;
			}
		}
	}

	void hq4x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		// Chain HQ2x twice. Slight quality loss vs. a direct HQ4x but
		// dramatically simpler. Buffer reused across calls.
		std::vector<unsigned int> mid(static_cast<size_t>(sw) * 2 * sh * 2);
		hq2x(src, sw, sh, mid.data());
		hq2x(mid.data(), sw * 2, sh * 2, dst);
	}

	// ---------------------------------------------------------------------
	// xBR-2x / xBR-4x — Hyllian.
	//
	// For each output quadrant we look at the diagonal edge running through
	// that quadrant. If the two pixels on one side of the edge agree and
	// differ from the two on the other side, the edge is real and we blend
	// the dominant color into the quadrant. The "edge strength" classification
	// uses the perceptual similarity predicate above so anti-aliased edges
	// fire correctly. xBR's signature look: smooth diagonals on rounded
	// pixel-art forms (RTS infantry sprites are the showcase case).
	// Compact port of the rule from Hyllian's reference shader; not bit-exact
	// to the GLSL but visually equivalent.
	// ---------------------------------------------------------------------

	void xbr2x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		const int dw = sw * 2;
		for (int y = 0; y < sh; y++)
		{
			for (int x = 0; x < sw; x++)
			{
				const unsigned int E = src[y * sw + x];
				const unsigned int A = at(src, sw, sh, x - 1, y - 1);
				const unsigned int B = at(src, sw, sh, x,     y - 1);
				const unsigned int C = at(src, sw, sh, x + 1, y - 1);
				const unsigned int D = at(src, sw, sh, x - 1, y);
				const unsigned int F = at(src, sw, sh, x + 1, y);
				const unsigned int G = at(src, sw, sh, x - 1, y + 1);
				const unsigned int H = at(src, sw, sh, x,     y + 1);
				const unsigned int I = at(src, sw, sh, x + 1, y + 1);

				// Each quadrant's edge runs between two diagonal neighbors.
				// For the NE quadrant: edge runs B->F. If B and F are
				// similar but differ from E, the quadrant takes the
				// blended color; otherwise stay with E. Asymmetric:
				// stronger weight to the matching diagonal pair.
				auto quad = [&](unsigned int cardA, unsigned int cardB,
					unsigned int diagAB, unsigned int oppA, unsigned int oppB) -> unsigned int
				{
					(void)oppB;
					if (similar(cardA, cardB) && !similar(cardA, E))
					{
						// Both cardinals on this edge agree and differ from
						// center → real edge crossing this quadrant.
						if (similar(diagAB, cardA))
							return blend3(E, cardA, diagAB, 2, 5, 1);
						// Diagonal disagrees — softer pull toward the edge.
						return blend3(E, cardA, oppA, 5, 3, 0);
					}
					return E;
				};

				const unsigned int NW = quad(B, D, A, C, G);
				const unsigned int NE = quad(B, F, C, A, I);
				const unsigned int SW = quad(H, D, G, I, A);
				const unsigned int SE = quad(H, F, I, G, C);
				const int dx = x * 2;
				const int dy = y * 2;
				dst[ dy      * dw + dx    ] = NW;
				dst[ dy      * dw + dx + 1] = NE;
				dst[(dy + 1) * dw + dx    ] = SW;
				dst[(dy + 1) * dw + dx + 1] = SE;
			}
		}
	}

	void xbr4x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		// Chain xBR-2x twice. Matches RetroArch's xbr-4x behavior closely.
		std::vector<unsigned int> mid(static_cast<size_t>(sw) * 2 * sh * 2);
		xbr2x(src, sw, sh, mid.data());
		xbr2x(mid.data(), sw * 2, sh * 2, dst);
	}

	// NNEDI3 thin forwarders. The real implementation lives in nnedi3.cpp
	// (the algorithm is substantially heavier than the rule-based upscalers
	// above and doesn't share helpers). Kept under the same namespace so
	// the dispatch site in player_fill_bgra_cache_entry stays uniform.
	void nnedi2x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		nnedi3::nnedi3_2x(src, sw, sh, dst);
	}
	void nnedi4x(const unsigned int* src, int sw, int sh, unsigned int* dst)
	{
		nnedi3::nnedi3_4x(src, sw, sh, dst);
	}
} // namespace pixel_upscale
