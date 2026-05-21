#include "stdafx.h"
#include "nnedi3.h"
#include "resource.h"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <vector>

// =============================================================================
// NNEDI3 scalar implementation. See header for licensing + provenance.
//
// Algorithm at a glance:
//   1. Convert source BGRA -> luma plane (BT.601), padded with edge-replicated
//      border so the prescreener and predictor windows never read OOB.
//   2. Vertical 2x pass: produce a 2x-tall image. Even rows = source rows
//      copied. Odd rows = filled by either cubic interp (if the prescreener
//      flags the pixel as "easy") or by the predictor neural network (if
//      flagged as "hard").
//   3. Transpose. Now run the same vertical 2x pass on the transposed image,
//      which fills the missing horizontal samples. Transpose back.
//   4. Bilinear-resample chroma (4:0:0 -> 4:4:4 effectively) to match the 2x
//      luma resolution and recombine into BGRA output.
//
// The "subtract mean" calls in znedi3's weights.cpp normalize coefficients
// against the pixel mid-value (128 for 8-bit). We bake that in once at
// weight-load time so the per-pixel hot loop just runs straight dot products.
// =============================================================================

namespace nnedi3 {
namespace {

// ----------------------------------------------------------------------------
// Weight storage. Mirrors znedi3::PrescreenerOldCoefficients +
// PredictorCoefficients but flattened into plain vectors so we don't pull
// in znedi3's unique_ptr/unordered_map machinery.
// ----------------------------------------------------------------------------

struct PrescreenerOld {
	float kernel_l0[4][48];	// 4 outputs x 48 inputs (4 rows x 12 cols)
	float bias_l0[4];
	float kernel_l1[4][4];
	float bias_l1[4];
	float kernel_l2[4][8];
	float bias_l2[4];
};

constexpr int XDIM = 32;	// predictor input window width
constexpr int YDIM = 6;	// predictor input window height (3 rows above + 3 below)
constexpr int NNS  = 64;	// neurons per layer
constexpr int FILTER_SIZE = XDIM * YDIM;	// 192

struct Predictor {
	// Quality-1 + Quality-2 softmax + elliott filters. Each filter block is
	// NNS * FILTER_SIZE floats. Order matches znedi3's read_nnedi3_weights:
	// softmax_q1, elliott_q1, softmax_bias_q1, elliott_bias_q1, then q2.
	std::vector<float> softmax_q1;
	std::vector<float> elliott_q1;
	std::vector<float> softmax_bias_q1;
	std::vector<float> elliott_bias_q1;
	std::vector<float> softmax_q2;
	std::vector<float> elliott_q2;
	std::vector<float> softmax_bias_q2;
	std::vector<float> elliott_bias_q2;
};

struct Weights {
	PrescreenerOld pre;
	Predictor pred;
	bool loaded = false;
};

Weights g_weights;
std::once_flag g_init_flag;

// ----------------------------------------------------------------------------
// znedi3's "subtract mean" normalization, transcribed from weights.cpp.
// Runs once at load time. Without this the predictor produces wrong output.
// ----------------------------------------------------------------------------

double meanf(const float* buf, size_t n)
{
	return std::accumulate(buf, buf + n, 0.0) / n;
}

void normalize_prescreener_old(PrescreenerOld& p, double pixel_half)
{
	for (int n = 0; n < 4; n++) {
		double m = meanf(p.kernel_l0[n], 48);
		for (int k = 0; k < 48; k++)
			p.kernel_l0[n][k] = static_cast<float>((p.kernel_l0[n][k] - m) / pixel_half);
	}
}

void normalize_predictor(Predictor& pr)
{
	// Per znedi3 weights.cpp::subtract_mean(PredictorModel&):
	// - per-neuron mean of softmax/elliott filters
	// - pointwise mean across all softmax filters (for q1 then q2 separately)
	// - mean of softmax biases
	// Then subtract each from the corresponding weight.
	const int filter_size = FILTER_SIZE;
	const int nns = NNS;

	auto normalize_pair = [&](std::vector<float>& sm, std::vector<float>& el,
		std::vector<float>& sm_bias, std::vector<float>& /*el_bias*/)
	{
		std::vector<double> softmax_means(nns);
		std::vector<double> elliott_means(nns);
		std::vector<double> mean_filter(filter_size, 0.0);
		for (int nn = 0; nn < nns; nn++) {
			softmax_means[nn] = meanf(sm.data() + nn * filter_size, filter_size);
			elliott_means[nn] = meanf(el.data() + nn * filter_size, filter_size);
			for (int k = 0; k < filter_size; k++)
				mean_filter[k] += sm[nn * filter_size + k] - softmax_means[nn];
		}
		for (int k = 0; k < filter_size; k++)
			mean_filter[k] /= nns;
		const double mean_bias = meanf(sm_bias.data(), nns);
		for (int nn = 0; nn < nns; nn++) {
			for (int k = 0; k < filter_size; k++) {
				sm[nn * filter_size + k] -= static_cast<float>(softmax_means[nn] + mean_filter[k]);
				el[nn * filter_size + k] -= static_cast<float>(elliott_means[nn]);
			}
			sm_bias[nn] -= static_cast<float>(mean_bias);
		}
	};

	normalize_pair(pr.softmax_q1, pr.elliott_q1, pr.softmax_bias_q1, pr.elliott_bias_q1);
	normalize_pair(pr.softmax_q2, pr.elliott_q2, pr.softmax_bias_q2, pr.elliott_bias_q2);
}

// ----------------------------------------------------------------------------
// Weights file parser. The slice generated by C:\XCC\slice_nnedi3.py contains:
//   - Old prescreener (252 floats — 4*48 kernel + 4 bias + 4*4 + 4 + 4*8 + 4)
//   - One predictor block for (32x6, NNS=64, etype=ABS): 49408 floats
// Total: 49660 floats = 198640 bytes.
// ----------------------------------------------------------------------------

bool parse_weights(const float* data, size_t n_floats)
{
	// 252 (prescreener) + 49408 (predictor)
	const size_t expected = 252 + (4 * NNS * FILTER_SIZE + 4 * NNS);
	if (n_floats != expected)
		return false;

	const float* p = data;

	// Old prescreener (mirrors weights.cpp:152..161).
	std::memcpy(&g_weights.pre.kernel_l0[0][0], p, sizeof(float) * 4 * 48); p += 4 * 48;
	std::memcpy(g_weights.pre.bias_l0, p, sizeof(float) * 4); p += 4;
	std::memcpy(&g_weights.pre.kernel_l1[0][0], p, sizeof(float) * 4 * 4); p += 4 * 4;
	std::memcpy(g_weights.pre.bias_l1, p, sizeof(float) * 4); p += 4;
	std::memcpy(&g_weights.pre.kernel_l2[0][0], p, sizeof(float) * 4 * 8); p += 4 * 8;
	std::memcpy(g_weights.pre.bias_l2, p, sizeof(float) * 4); p += 4;

	// Predictor (mirrors the block at weights.cpp:204..217, single nsize+nns).
	auto load_block = [&](std::vector<float>& v, size_t count) {
		v.assign(p, p + count);
		p += count;
	};
	load_block(g_weights.pred.softmax_q1, NNS * FILTER_SIZE);
	load_block(g_weights.pred.elliott_q1, NNS * FILTER_SIZE);
	load_block(g_weights.pred.softmax_bias_q1, NNS);
	load_block(g_weights.pred.elliott_bias_q1, NNS);
	load_block(g_weights.pred.softmax_q2, NNS * FILTER_SIZE);
	load_block(g_weights.pred.elliott_q2, NNS * FILTER_SIZE);
	load_block(g_weights.pred.softmax_bias_q2, NNS);
	load_block(g_weights.pred.elliott_bias_q2, NNS);

	assert(p == data + n_floats);

	// Bake in the subtract_mean normalization so the hot loop runs raw dot
	// products without per-pixel mean subtraction.
	normalize_prescreener_old(g_weights.pre, 128.0);	// 8-bit pixel midpoint
	normalize_predictor(g_weights.pred);

	g_weights.loaded = true;
	return true;
}

void do_load_once()
{
	// Locate RCDATA resource NNEDI3_WEIGHTS in the running .exe. Same pattern
	// as Mixer's GLOBAL_MIX_DATABASE fallback in XCC Mixer.cpp.
	HMODULE hmod = ::GetModuleHandleW(nullptr);
	HRSRC hr = ::FindResourceA(hmod, "NNEDI3_WEIGHTS", RT_RCDATA);
	if (!hr) return;
	DWORD sz = ::SizeofResource(hmod, hr);
	HGLOBAL hg = ::LoadResource(hmod, hr);
	if (!hg) return;
	const void* p = ::LockResource(hg);
	if (!p) return;
	if (sz % sizeof(float) != 0) return;
	parse_weights(reinterpret_cast<const float*>(p), sz / sizeof(float));
}

// ----------------------------------------------------------------------------
// Math helpers — straight ports from znedi3 kernel.cpp.
// ----------------------------------------------------------------------------

float dot_product(const float* kernel, const float* input, int n, float scale, float bias)
{
	float accum = 0.0f;
	for (int i = 0; i < n; i++) accum += kernel[i] * input[i];
	return accum * scale + bias;
}

float elliott(float x) { return x / (1.0f + std::fabs(x)); }

float softmax_exp(float x)
{
	if (x < -80.0f) x = -80.0f;
	else if (x > 80.0f) x = 80.0f;
	return std::exp(x);
}

// ----------------------------------------------------------------------------
// Prescreener: predicts whether a given output pixel is on a hard edge
// (run the neural predictor) or on a smooth gradient (cubic interp suffices).
// Port of PrescreenerOldC::process in kernel.cpp.
//
// Window: 4 rows x 12 cols centered on the output position. src[0..3] are
// pointers to rows -2, -1, +1, +2 relative to output (the 4 neighboring
// source rows; the missing row is what we're filling).
// ----------------------------------------------------------------------------

void prescreen_row(const float* const src[4], unsigned char* prescreen, int n)
{
	const PrescreenerOld& d = g_weights.pre;
	const ptrdiff_t window_offset = 5;
	for (ptrdiff_t j = 0; j < n; j++) {
		float input[48];
		float state[12];
		for (int i = 0; i < 4; i++)
			std::memcpy(input + i * 12, src[i] - window_offset + j, 12 * sizeof(float));

		// Layer 0: 4 outputs from 48 inputs. Output 0 is left raw (per
		// znedi3); outputs 1..3 pass through elliott.
		for (int nn = 0; nn < 4; nn++)
			state[nn] = dot_product(d.kernel_l0[nn], input, 48, 1.0f, d.bias_l0[nn]);
		for (int nn = 1; nn < 4; nn++) state[nn] = elliott(state[nn]);

		// Layer 1: 4 outputs from layer-0 state, all elliott.
		for (int nn = 0; nn < 4; nn++)
			state[nn + 4] = dot_product(d.kernel_l1[nn], state, 4, 1.0f, d.bias_l1[nn]);
		for (int nn = 4; nn < 8; nn++) state[nn] = elliott(state[nn]);

		// Layer 2: 4 outputs from 8 layer-0+1 features, no nonlinearity.
		for (int nn = 0; nn < 4; nn++)
			state[nn + 8] = dot_product(d.kernel_l2[nn], state, 8, 1.0f, d.bias_l2[nn]);

		// Classification: hard pixel if max(state[10..11]) > max(state[8..9]),
		// else easy. UCHAR_MAX = easy (run cubic), 0 = hard (run predictor).
		// Inverted from znedi3 because we flag the cubic-eligible pixels.
		prescreen[j] = std::max(state[10], state[11]) <= std::max(state[8], state[9]) ? 1 : 0;
	}
}

// ----------------------------------------------------------------------------
// Predictor: the neural network. Port of PredictorC::process.
// ----------------------------------------------------------------------------

void wae5(const float* softmax, const float* el, int n, float mstd[4])
{
	float vsum = 0.0f, wsum = 0.0f;
	for (int i = 0; i < n; i++) {
		vsum += softmax[i] * elliott(el[i]);
		wsum += softmax[i];
	}
	if (wsum > 1e-10f)
		mstd[3] += (5.0f * vsum) / wsum * mstd[1] + mstd[0];
	else
		mstd[3] += mstd[0];
}

void predict_row(const float* const* src, float* dst, const unsigned char* prescreen, int n)
{
	const Predictor& pr = g_weights.pred;
	const ptrdiff_t window_offset_y = 3 - YDIM / 2;	// 0 for ydim=6
	const ptrdiff_t window_offset_x = XDIM / 2 - 1;	// 15 for xdim=32

	for (ptrdiff_t i = 0; i < n; i++) {
		if (prescreen[i])
			continue;	// cubic path handled it

		float input[FILTER_SIZE];
		float activation[NNS * 2];
		float mstd[4] = { 0, 0, 0, 0 };

		// Gather input + compute mean/stddev of the window.
		double sum = 0, sum_sq = 0;
		for (int yy = 0; yy < YDIM; yy++) {
			const float* row = src[yy + window_offset_y];
			for (int xx = 0; xx < XDIM; xx++) {
				const float v = row[i - window_offset_x + xx];
				input[yy * XDIM + xx] = v;
				sum += v;
				sum_sq += static_cast<double>(v) * v;
			}
		}
		mstd[0] = static_cast<float>(sum / FILTER_SIZE);
		mstd[3] = 0.0f;
		double tmp = sum_sq / FILTER_SIZE - static_cast<double>(mstd[0]) * mstd[0];
		if (tmp < FLT_EPSILON) {
			mstd[1] = 0.0f;
			mstd[2] = 0.0f;
		} else {
			mstd[1] = static_cast<float>(std::sqrt(tmp));
			mstd[2] = 1.0f / mstd[1];
		}
		const float scale = mstd[2];

		// Quality 1.
		for (int nn = 0; nn < NNS; nn++)
			activation[nn] = dot_product(pr.softmax_q1.data() + nn * FILTER_SIZE, input,
				FILTER_SIZE, scale, pr.softmax_bias_q1[nn]);
		for (int nn = 0; nn < NNS; nn++)
			activation[NNS + nn] = dot_product(pr.elliott_q1.data() + nn * FILTER_SIZE, input,
				FILTER_SIZE, scale, pr.elliott_bias_q1[nn]);
		for (int nn = 0; nn < NNS; nn++) activation[nn] = softmax_exp(activation[nn]);
		wae5(activation, activation + NNS, NNS, mstd);

		// Quality 2 (we always use qual=2 for max quality).
		for (int nn = 0; nn < NNS; nn++)
			activation[nn] = dot_product(pr.softmax_q2.data() + nn * FILTER_SIZE, input,
				FILTER_SIZE, scale, pr.softmax_bias_q2[nn]);
		for (int nn = 0; nn < NNS; nn++)
			activation[NNS + nn] = dot_product(pr.elliott_q2.data() + nn * FILTER_SIZE, input,
				FILTER_SIZE, scale, pr.elliott_bias_q2[nn]);
		for (int nn = 0; nn < NNS; nn++) activation[nn] = softmax_exp(activation[nn]);
		wae5(activation, activation + NNS, NNS, mstd);

		dst[i] = mstd[3] / 2.0f;	// averaged over q1 + q2
	}
}

// ----------------------------------------------------------------------------
// Cubic interpolation used for prescreener-flagged "easy" pixels. Port of
// cubic_interpolation_c. src[0..3] = rows -2, -1, +1, +2.
// ----------------------------------------------------------------------------

void cubic_row(const float* const src[4], float* dst, const unsigned char* prescreen, int n)
{
	for (int i = 0; i < n; i++) {
		if (!prescreen[i]) continue;
		dst[i] = (-3.0f / 32.0f) * src[0][i] + (19.0f / 32.0f) * src[1][i] +
		         (19.0f / 32.0f) * src[2][i] + (-3.0f / 32.0f) * src[3][i];
	}
}

// ----------------------------------------------------------------------------
// Vertical 2x: input is a luma plane (sw x sh, padded). Output is 2x-tall
// luma (sw x 2*sh) where even rows are copies of source and odd rows are
// neural-network interpolated.
//
// `padded` is laid out with PAD rows of border on top and bottom + PAD
// columns of border on left and right. Indexing uses the un-padded
// coordinate space; helpers return the right row pointer.
// ----------------------------------------------------------------------------

constexpr int PAD = 8;	// vertical pad (need 3 rows above + 3 below for predictor; pad extra for safety)
constexpr int HPAD = 16;	// horizontal pad (need 15 left + 16 right for predictor xdim=32)

inline const float* row_at(const std::vector<float>& padded, int sw, int sh, int y)
{
	(void)sh;
	const int stride = sw + 2 * HPAD;
	return padded.data() + static_cast<size_t>(y + PAD) * stride + HPAD;
}

void pad_image(const std::vector<float>& src, int sw, int sh, std::vector<float>& padded)
{
	const int stride = sw + 2 * HPAD;
	padded.assign(static_cast<size_t>(stride) * (sh + 2 * PAD), 0.0f);
	// Copy source rows, then horizontally edge-extend.
	for (int y = 0; y < sh; y++) {
		float* dst = padded.data() + static_cast<size_t>(y + PAD) * stride;
		const float* s = src.data() + static_cast<size_t>(y) * sw;
		for (int x = 0; x < HPAD; x++) dst[x] = s[0];
		std::memcpy(dst + HPAD, s, sw * sizeof(float));
		for (int x = 0; x < HPAD; x++) dst[HPAD + sw + x] = s[sw - 1];
	}
	// Vertical edge-extend by copying first/last source row into padding rows.
	for (int y = 0; y < PAD; y++) {
		float* dst_top = padded.data() + static_cast<size_t>(y) * stride;
		float* dst_bot = padded.data() + static_cast<size_t>(sh + PAD + y) * stride;
		const float* src_top = padded.data() + static_cast<size_t>(PAD) * stride;
		const float* src_bot = padded.data() + static_cast<size_t>(sh + PAD - 1) * stride;
		std::memcpy(dst_top, src_top, stride * sizeof(float));
		std::memcpy(dst_bot, src_bot, stride * sizeof(float));
	}
}

void upscale_v2x_luma(const std::vector<float>& luma_src, int sw, int sh,
	std::vector<float>& luma_dst)
{
	std::vector<float> padded;
	pad_image(luma_src, sw, sh, padded);

	luma_dst.assign(static_cast<size_t>(sw) * sh * 2, 0.0f);

	// For each "missing" row at output position 2*y+1, the 4 neighbor source
	// rows are y-1, y, y+1, y+2 (for the prescreener and cubic) and y-2..y+3
	// for the predictor (ydim=6). For each pixel we choose cubic or NN.
	//
	// Row loop is OMP-parallel: rows write disjoint output bands and only
	// read from the read-only `padded` buffer + the source plane. The
	// prescreen scratch is declared inside the loop body so each thread
	// gets its own. Gate forking on total work — small images would lose
	// time to fork overhead. NNEDI3 is ~10x heavier than xBR per pixel,
	// so the threshold is generous (skip fork on really tiny SHPs only).
	const long long work = static_cast<long long>(sw) * sh;
	#pragma omp parallel for schedule(static) if(work >= 1024)
	for (int y = 0; y < sh; y++) {
		std::vector<unsigned char> prescreen(sw);

		// Copy source row into even output position.
		float* dst_even = luma_dst.data() + static_cast<size_t>(2 * y) * sw;
		std::memcpy(dst_even, luma_src.data() + static_cast<size_t>(y) * sw, sw * sizeof(float));

		// Fill missing row at output 2*y+1 (interpolated between source rows
		// y and y+1). For the last source row this is the synthetic edge —
		// just duplicate.
		float* dst_odd = luma_dst.data() + static_cast<size_t>(2 * y + 1) * sw;
		if (y == sh - 1) {
			std::memcpy(dst_odd, dst_even, sw * sizeof(float));
			continue;
		}

		// Prescreener: 4 rows (y-1, y, y+1, y+2) for the 4x12 window.
		const float* pre_src[4] = {
			row_at(padded, sw, sh, y - 1),
			row_at(padded, sw, sh, y    ),
			row_at(padded, sw, sh, y + 1),
			row_at(padded, sw, sh, y + 2),
		};
		prescreen_row(pre_src, prescreen.data(), sw);

		// Cubic fill for the "easy" pixels (prescreen[i]==1).
		cubic_row(pre_src, dst_odd, prescreen.data(), sw);

		// Predictor fill for the "hard" pixels (prescreen[i]==0). Predictor
		// needs ydim=6 rows centered on the gap: y-2, y-1, y, y+1, y+2, y+3.
		const float* pred_src[YDIM] = {
			row_at(padded, sw, sh, y - 2),
			row_at(padded, sw, sh, y - 1),
			row_at(padded, sw, sh, y    ),
			row_at(padded, sw, sh, y + 1),
			row_at(padded, sw, sh, y + 2),
			row_at(padded, sw, sh, y + 3),
		};
		predict_row(pred_src, dst_odd, prescreen.data(), sw);
	}
}

// ----------------------------------------------------------------------------
// Transpose helper for the second-pass (horizontal -> vertical via transpose).
// ----------------------------------------------------------------------------

void transpose_luma(const std::vector<float>& src, int sw, int sh, std::vector<float>& dst)
{
	dst.assign(static_cast<size_t>(sw) * sh, 0.0f);
	for (int y = 0; y < sh; y++)
		for (int x = 0; x < sw; x++)
			dst[static_cast<size_t>(x) * sh + y] = src[static_cast<size_t>(y) * sw + x];
}

// ----------------------------------------------------------------------------
// BGRA <-> luma conversion. Pixel-art content carries detail in luma; chroma
// goes through a simple 2x bilinear and combines back at output time. The
// human eye is much more sensitive to luma resolution than chroma — this
// matches how DVD/JPEG/JPEG-XL all subsample chroma.
// ----------------------------------------------------------------------------

inline float to_luma(unsigned int bgra)
{
	// BT.601: 0.299*R + 0.587*G + 0.114*B
	const float b = static_cast<float>(bgra & 0xff);
	const float g = static_cast<float>((bgra >> 8) & 0xff);
	const float r = static_cast<float>((bgra >> 16) & 0xff);
	return 0.299f * r + 0.587f * g + 0.114f * b;
}

inline unsigned int clamp_byte(int x) { return static_cast<unsigned int>(x < 0 ? 0 : (x > 255 ? 255 : x)); }

// Build a 2x BGRA result from upscaled luma + bilinear chroma. For each
// destination pixel: look up the bilinearly-sampled source BGRA at the
// half-pixel offset; replace its luma component with the NN luma.
unsigned int recombine_pixel(const unsigned int* src, int sw, int sh,
	float dst_x, float dst_y, float new_luma)
{
	// dst_x, dst_y are in source-pixel coords (0..sw, 0..sh).
	const float fx = dst_x - 0.5f;
	const float fy = dst_y - 0.5f;
	const int x0 = static_cast<int>(std::floor(fx));
	const int y0 = static_cast<int>(std::floor(fy));
	const float wx = fx - x0;
	const float wy = fy - y0;
	const int x0c = std::max(0, std::min(sw - 1, x0));
	const int x1c = std::max(0, std::min(sw - 1, x0 + 1));
	const int y0c = std::max(0, std::min(sh - 1, y0));
	const int y1c = std::max(0, std::min(sh - 1, y0 + 1));
	const unsigned int p00 = src[y0c * sw + x0c];
	const unsigned int p10 = src[y0c * sw + x1c];
	const unsigned int p01 = src[y1c * sw + x0c];
	const unsigned int p11 = src[y1c * sw + x1c];

	auto get = [](unsigned int p, int shift) { return static_cast<float>((p >> shift) & 0xff); };
	const float w00 = (1 - wx) * (1 - wy);
	const float w10 =      wx  * (1 - wy);
	const float w01 = (1 - wx) *      wy ;
	const float w11 =      wx  *      wy ;
	const float b = w00 * get(p00, 0) + w10 * get(p10, 0) + w01 * get(p01, 0) + w11 * get(p11, 0);
	const float g = w00 * get(p00, 8) + w10 * get(p10, 8) + w01 * get(p01, 8) + w11 * get(p11, 8);
	const float r = w00 * get(p00, 16) + w10 * get(p10, 16) + w01 * get(p01, 16) + w11 * get(p11, 16);
	const float a = w00 * get(p00, 24) + w10 * get(p10, 24) + w01 * get(p01, 24) + w11 * get(p11, 24);

	// Replace luma. Scale (r, g, b) so that their BT.601 luma matches new_luma.
	const float cur_luma = 0.299f * r + 0.587f * g + 0.114f * b;
	float scale = 1.0f;
	if (cur_luma > 1.0f) scale = new_luma / cur_luma;
	int rr = static_cast<int>(r * scale + 0.5f);
	int gg = static_cast<int>(g * scale + 0.5f);
	int bb = static_cast<int>(b * scale + 0.5f);
	int aa = static_cast<int>(a + 0.5f);
	return clamp_byte(bb) | (clamp_byte(gg) << 8) | (clamp_byte(rr) << 16) | (clamp_byte(aa) << 24);
}

} // namespace

// =============================================================================
// Public API.
// =============================================================================

bool ensure_weights_loaded()
{
	std::call_once(g_init_flag, do_load_once);
	return g_weights.loaded;
}

void nnedi3_2x(const unsigned int* src, int sw, int sh, unsigned int* dst)
{
	if (!ensure_weights_loaded()) {
		// Resource missing — fall back to nearest-neighbor 2x so the
		// caller always gets a visually-sized output buffer.
		for (int y = 0; y < sh * 2; y++) {
			for (int x = 0; x < sw * 2; x++) {
				dst[y * (sw * 2) + x] = src[(y / 2) * sw + (x / 2)];
			}
		}
		return;
	}

	// Extract luma plane. Parallel: each pixel independent.
	std::vector<float> luma(static_cast<size_t>(sw) * sh);
	#pragma omp parallel for schedule(static) if(sw * sh >= 4096)
	for (int i = 0; i < sw * sh; i++) luma[i] = to_luma(src[i]);

	// Vertical 2x — output (sw) x (2*sh).
	std::vector<float> luma_v;
	upscale_v2x_luma(luma, sw, sh, luma_v);

	// Transpose to (2*sh) x (sw).
	std::vector<float> luma_vt;
	transpose_luma(luma_v, sw, 2 * sh, luma_vt);

	// Vertical 2x of the transposed — output (2*sh) x (2*sw).
	std::vector<float> luma_vt2;
	upscale_v2x_luma(luma_vt, 2 * sh, sw, luma_vt2);

	// Transpose back to (2*sw) x (2*sh).
	std::vector<float> luma_final;
	transpose_luma(luma_vt2, 2 * sh, 2 * sw, luma_final);

	// Recombine luma with bilinear chroma into BGRA. Parallel: each output
	// pixel independent. Heavy enough to warrant forking even on smaller
	// frames — bilinear sample + luma-rescale + 4-channel pack per pixel.
	const int dw = sw * 2;
	const int dh = sh * 2;
	#pragma omp parallel for schedule(static) if(dw * dh >= 4096)
	for (int dy = 0; dy < dh; dy++) {
		for (int dx = 0; dx < dw; dx++) {
			const float new_luma = luma_final[static_cast<size_t>(dy) * dw + dx];
			// dst pixel center at (dx + 0.5, dy + 0.5) in dst coords maps to
			// ((dx + 0.5) / 2, (dy + 0.5) / 2) in src coords.
			const float src_x = (dx + 0.5f) * 0.5f;
			const float src_y = (dy + 0.5f) * 0.5f;
			dst[static_cast<size_t>(dy) * dw + dx] = recombine_pixel(src, sw, sh, src_x, src_y, new_luma);
		}
	}
}

void nnedi3_4x(const unsigned int* src, int sw, int sh, unsigned int* dst)
{
	std::vector<unsigned int> mid(static_cast<size_t>(sw) * 2 * sh * 2);
	nnedi3_2x(src, sw, sh, mid.data());
	nnedi3_2x(mid.data(), sw * 2, sh * 2, dst);
}

} // namespace nnedi3
