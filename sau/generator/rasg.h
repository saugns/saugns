/* SAU library: Random segments generator implementation.
 * Copyright (c) 2022-2025 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <sau/line.h>
#include "noise.h"
#include "phasor.h"

#define RASG_MEASURE_LINE_AMP 0
#if RASG_MEASURE_LINE_AMP
# include <stdio.h>
#endif

typedef struct sauRasG {
	sauPhasor phasor;
	sauRasOpt opt;
	float prev_s, fb_s;
} sauRasG;

/**
 * Initialize instance for use.
 */
static inline void sau_init_RasG(sauRasG *restrict o, uint32_t srate) {
	*o = (sauRasG){
		.phasor = (sauPhasor){
			.cycle_phase = 0,
			.coeff = sauPhasor_COEFF(srate),
			.rate2x = true,
		},
		.opt.line = SAU_LINE_N_lin,
		.opt.func = SAU_RAS_F_URAND,
		.opt.level = sau_ras_level(9 /* max one-digit number */),
		.opt.alpha = SAU_FIBH32, // use golden ratio as default
		.opt.flags = 0,
	};
}

static inline void sauRasG_set_phase(sauRasG *restrict o, uint32_t phase) {
	sauPhasor_set_phase(&o->phasor, phase);
}

static inline void sauRasG_set_cycle(sauRasG *restrict o, uint32_t cycle) {
	sauPhasor_set_cycle(&o->phasor, cycle);
}

/**
 * Update mode options. Will adjust settings which are dependent on the mode.
 */
static void sauRasG_set_opt(sauRasG *restrict o, const sauRasOpt opt) {
	unsigned flags = opt.flags;
	if (opt.flags & SAU_RAS_O_LINE_SET)
		o->opt.line = opt.line;
	if (opt.flags & SAU_RAS_O_FUNC_SET)
		o->opt.func = opt.func;
	else
		flags |= o->opt.flags; /* keep old modifying flags */
	if (opt.flags & SAU_RAS_O_LEVEL_SET)
		o->opt.level = opt.level;
	if (opt.flags & SAU_RAS_O_ASUBVAL_SET)
		o->opt.alpha = opt.alpha;
	o->opt.flags = flags;
	bool rate2x = !(flags & SAU_RAS_O_HALFSHAPE);
	if (rate2x != o->phasor.rate2x) {
		uint32_t cycle = sauPhasor_get_cycle(&o->phasor);
		uint32_t phase = sauPhasor_get_phase(&o->phasor);
		o->phasor.rate2x = rate2x;
		sauPhasor_set_cycle(&o->phasor, cycle);
		sauPhasor_set_phase(&o->phasor, phase);
	}
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t sauRasG_cycle_len(sauRasG *restrict o, float freq) {
	return sau_ftoi(SAU_INV_FREQ(32, o->phasor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t sauRasG_cycle_pos(sauRasG *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = sau_ftoi(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return phs / inc;
}

/**
 * Calculate offset relative to wave cycle for \p freq, based on \p pos.
 *
 * Can be used to reduce time length to something rounder and reduce clicks.
 */
static inline int32_t sauRasG_cycle_offs(sauRasG *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = sau_ftoi(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - sauWave_SLEN) / inc;
}

/**
 * Wrapper for applying PD synthesis distortion.
 */
void sauRasG_pdist(sauRasG *restrict o,
		unsigned pdist_fn,
		void *restrict phase_buf,
		uint32_t *restrict cycle_ui32,
		size_t buf_len,
		const float *restrict v_f,
		const float *restrict f_f,
		float f_fval,
		float *restrict p_f,
		float p_fval) {
	sauPhasor_ui32tof(&o->phasor, phase_buf, buf_len);
	sauPhasor_get_pdist_fn(pdist_fn)(&o->phasor,
			phase_buf, cycle_ui32, buf_len,
			v_f, f_f, f_fval, p_f, p_fval);
}

typedef void (*sauRasG_map_f)(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf);

typedef void (*sauRasG_map_selfmod_f)(sauRasG *restrict o,
		size_t buf_len,
		float *restrict main_buf,
		sauLine_val_f line_f,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf);

/*
 * Define loop body for a sauRasg_map_*_s() function. Expects named macro.
 */
#define RASG_MAP_S_LOOP(loop_for_func) \
unsigned flags = o->opt.flags, line = o->opt.line; \
const float perlin_amp = \
	(flags & (SAU_RAS_O_HALFSHAPE|SAU_RAS_O_ZIGZAG)) ? \
	1.f : \
	sauLine_coeffs[line].perlin_amp; \
for (size_t i = 0; i < buf_len; ++i) { \
	float pm_a = o->fb_s * pm_abuf[i] * 0.5f; \
	float phase = main_buf[i] + pm_a; \
	int32_t cycle_adj = floorf(phase); \
	uint32_t cycle = cycle_buf[i] + cycle_adj; \
	phase -= cycle_adj; \
	/**/ RASG_MAP_##loop_for_func \
	if (flags & SAU_RAS_O_PERLIN) { \
		a *= perlin_amp*phase; \
		b *= perlin_amp*(phase-1.f); \
	} \
	if (flags & SAU_RAS_O_HALFSHAPE) { \
		/* sort value-pairs, for a decreasing sawtooth-like waveform */\
		float max = sau_maxf(a, b); \
		float min = sau_minf(a, b); \
		a = max; \
		b = min; \
	} \
	if (flags & SAU_RAS_O_ZIGZAG) { \
		/* swap half-cycle ends for jagged shape on random amplitude */\
		float tmp = a; a = b; b = tmp; \
	} \
	if (flags & SAU_RAS_O_SQUARE) { \
		/* square keeping sign; value uniformity to energy uniformity*/\
		a *= fabsf(a); \
		b *= fabsf(b); \
	} \
	float s = line_f(phase, a, b); \
	main_buf[i] = s; \
	/* Suppress ringing using 1-pole filter + 1-zero filter. */ \
	o->fb_s = (o->fb_s + s + o->prev_s) * 0.5f; \
	o->prev_s = s; \
}

/*
 * Define a whole sauRasg_map_*_s() function. Expects named macro.
 */
#define RASG_MAP_S_FUNC(loop_for_func) \
static sauMaybeUnused void sauRasG_map_##loop_for_func##_s(sauRasG *restrict o,\
		size_t buf_len, \
		float *restrict main_buf, \
		sauLine_val_f line_f, \
		const uint32_t *restrict cycle_buf, \
		const float *restrict pm_abuf) { \
	int sauMaybeUnused sr = o->opt.level; \
	RASG_MAP_S_LOOP(loop_for_func) \
}

/**
 * Run for \p buf_len samples in 'violet random' mode, generating output.
 */
static sauMaybeUnused void sauRasG_map_v_urand(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_v_urand \
		uint32_t s0 = sau_ranfast32(cycle - 1) / 2; \
		uint32_t s1 = sau_ranfast32(cycle) / 2; \
		uint32_t s2 = sau_ranfast32(cycle + 1) / 2; \
		float a = sau_fscalei(s1 - s0, 0x1p-31f); \
		float b = sau_fscalei(s2 - s1, 0x1p-31f); \
/**/
		RASG_MAP_v_urand
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

RASG_MAP_S_FUNC(v_urand)

/**
 * Run for \p buf_len samples in 'uniform random' mode, generating output.
 */
static sauMaybeUnused void sauRasG_map_urand(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	if (o->opt.flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_urand(o, buf_len, end_a_buf,end_b_buf, cycle_buf);
		return;
	}
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_urand \
		float a = sau_fscalei(sau_ranfast32(cycle), 0x1p-31f); \
		float b = sau_fscalei(sau_ranfast32(cycle + 1), 0x1p-31f); \
/**/
		RASG_MAP_urand
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

/**
 * Run for \p buf_len samples in 'uniform random' mode, generating output.
 *
 * Self-modulation version, slower and more flexible design.
 */
static sauMaybeUnused void sauRasG_map_urand_s(sauRasG *restrict o,
		size_t buf_len,
		float *restrict main_buf,
		sauLine_val_f line_f,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf) {
	if (o->opt.flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_urand_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	RASG_MAP_S_LOOP(urand)
}

/**
 * Run for \p buf_len samples in 'Gaussian random' mode, generating output.
 */
static sauMaybeUnused void sauRasG_map_gauss(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_gauss \
		float a = sau_franssgauss32(cycle); \
		float b = sau_franssgauss32(cycle + 1); \
/**/
		RASG_MAP_gauss
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

RASG_MAP_S_FUNC(gauss)

/**
 * Run for \p buf_len samples in 'violet binary' mode -- a differentiated,
 * scaled 'ternary random' variation. Ternary smooth random always changes
 * value, so only two differences are possible -- hence diffed for binary.
 */
static sauMaybeUnused void sauRasG_map_v_bin(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	int sr = o->opt.level;
	// TODO: Scaling ends up slightly too low near sr == 1, improve?
	const float scale_diff = 1.f
		- (sau_sar32(INT32_MAX, sr) / 0x1p31f);
	const float scale = (1.f + scale_diff*scale_diff) / 0x1p31f;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_v_bin \
		uint32_t sb = (cycle & 1) << 31; \
		uint32_t sb_flip = (1U<<31) - sb; \
		uint32_t s0 = sau_divi(sau_sar32(sau_ranfast32(cycle - 1), sr) \
				+ sb, 2); \
		uint32_t s1 = sau_divi(sau_sar32(sau_ranfast32(cycle), sr) \
				+ sb_flip, 2); /* at even pos to cos-align */ \
		uint32_t s2 = sau_divi(sau_sar32(sau_ranfast32(cycle + 1), sr) \
				+ sb, 2); \
		float a = sau_fscalei(s1 - s0, scale); \
		float b = sau_fscalei(s2 - s1, scale); \
/**/
		RASG_MAP_v_bin
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

/**
 * Run for \p buf_len samples in 'violet binary' mode -- a differentiated,
 *
 * Self-modulation version, slower and more flexible design.
 */
static sauMaybeUnused void sauRasG_map_v_bin_s(sauRasG *restrict o,
		size_t buf_len,
		float *restrict main_buf,
		sauLine_val_f line_f,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf) {
	int sr = o->opt.level;
	// TODO: Scaling ends up slightly too low near sr == 1, improve?
	const float scale_diff = 1.f
		- (sau_sar32(INT32_MAX, sr) / 0x1p31f);
	const float scale = (1.f + scale_diff*scale_diff) / 0x1p31f;
	RASG_MAP_S_LOOP(v_bin)
}

/**
 * Run for \p buf_len samples in 'binary random' mode, generating output.
 * For an increasing \a level > 0 each new level is half as squiggly, for
 * a near-binary mode when above 5 (with best quality seemingly from 27).
 */
static sauMaybeUnused void sauRasG_map_bin(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	if (o->opt.flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_bin(o, buf_len, end_a_buf, end_b_buf, cycle_buf);
		return;
	}
	int sr = o->opt.level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_bin \
		uint32_t offs = INT32_MAX + (cycle & 1) * 2; \
		uint32_t s1 = sau_sar32(sau_ranfast32(cycle), sr) + offs; \
		uint32_t s2 = sau_sar32(sau_ranfast32(cycle + 1), sr) - offs; \
		float a = sau_fscalei(s1, 0x1p-31f); \
		float b = sau_fscalei(s2, 0x1p-31f); \
/**/
		RASG_MAP_bin
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

/**
 * Run for \p buf_len samples in 'binary random' mode, generating output.
 *
 * Self-modulation version, slower and more flexible design.
 */
static sauMaybeUnused void sauRasG_map_bin_s(sauRasG *restrict o,
		size_t buf_len,
		float *restrict main_buf,
		sauLine_val_f line_f,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf) {
	if (o->opt.flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_bin_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	int sr = o->opt.level;
	RASG_MAP_S_LOOP(bin)
}

/**
 * Run for \p buf_len samples in 'ternary random' mode, generating output.
 * For an increasing \a level > 0 each new level is half as squiggly, with
 * a practically ternary mode when above 5, but 30 is technically perfect.
 *
 * This is a special, smooth ternary random, which always changes value --
 * from top-or-bottom to middle, like an oscillation randomly flipping its
 * polarity at zero crossings. Smooth-sounding, and has useful properties.
 */
static sauMaybeUnused void sauRasG_map_tern(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	int sr = o->opt.level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_tern \
		uint32_t sb = (cycle & 1) << 31; \
		uint32_t sb_flip = (1U<<31) - sb; \
		/* sb_flip is used before sb to cos-align result */ \
		uint32_t s1 = sau_sar32(sau_ranfast32(cycle), sr) + sb_flip; \
		uint32_t s2 = sau_sar32(sau_ranfast32(cycle + 1), sr) + sb; \
		float a = sau_fscalei(s1, 0x1p-31f); \
		float b = sau_fscalei(s2, 0x1p-31f); \
/**/
		RASG_MAP_tern
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

RASG_MAP_S_FUNC(tern)

/**
 * Run for \p buf_len samples in 'fixed cycle' mode, generating output.
 * Simple version, optimizing high level (pure base frequency) setting.
 */
static sauMaybeUnused void sauRasG_map_fixed_simple(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_fixed_simple \
		float a = sau_oddness_as_sign(cycle); \
		float b = -a; \
/**/
		RASG_MAP_fixed_simple
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

RASG_MAP_S_FUNC(fixed_simple)

/**
 * Run for \p buf_len samples in 'violet fixed' (violet-fixed mix) mode.
 * For an increasing \a level > 0, each new level halves the randomness,
 * the base frequency amplifying in its place -- toward ultimate purity.
 */
static sauMaybeUnused void sauRasG_map_v_fixed(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	int sr = o->opt.level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_v_fixed \
		uint32_t sign = sau_oddness_as_sign(cycle); \
		uint32_t s0 = sau_divi(sign * \
				((sau_ranfast32(cycle - 1) >> sr) - \
				 INT32_MAX),  2); \
		uint32_t s1 = sau_divi(-sign * \
				((sau_ranfast32(cycle) >> sr) - \
				 INT32_MAX),  2); \
		uint32_t s2 = sau_divi(sign * \
				((sau_ranfast32(cycle + 1) >> sr) - \
				 INT32_MAX),  2); \
		float a = sau_fscalei(s1 - s0, 0x1p-31f); \
		float b = sau_fscalei(s2 - s1, 0x1p-31f); \
/**/
		RASG_MAP_v_fixed
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

RASG_MAP_S_FUNC(v_fixed)

/**
 * Run for \p buf_len samples in 'fixed cycle' mode, generating output.
 * For an increasing \a level > 0 each new level halves the randomness,
 * the base frequency amplifying in its place (toward ultimate purity).
 */
static sauMaybeUnused void sauRasG_map_fixed(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	if (o->opt.level >= sau_ras_level(9)) {
		sauRasG_map_fixed_simple(o, buf_len, end_a_buf, end_b_buf,
				cycle_buf);
		return;
	}
	if (o->opt.flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_fixed(o, buf_len, end_a_buf, end_b_buf,
				cycle_buf);
		return;
	}
	int sr = o->opt.level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_fixed \
		uint32_t sign = sau_oddness_as_sign(cycle); \
		float a = sau_fscalei(-sign * \
				((sau_ranfast32(cycle) >> sr) - \
				 INT32_MAX), 0x1p-31f); \
		float b = sau_fscalei(sign * \
				((sau_ranfast32(cycle + 1) >> sr) - \
				 INT32_MAX), 0x1p-31f); \
/**/
		RASG_MAP_fixed
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

/**
 * Run for \p buf_len samples in 'fixed cycle' mode, generating output.
 *
 * Self-modulation version, slower and more flexible design.
 */
static sauMaybeUnused void sauRasG_map_fixed_s(sauRasG *restrict o,
		size_t buf_len,
		float *restrict main_buf,
		sauLine_val_f line_f,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf) {
	if (o->opt.level >= sau_ras_level(9)) {
		sauRasG_map_fixed_simple_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	if (o->opt.flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_fixed_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	int sr = o->opt.level;
	RASG_MAP_S_LOOP(fixed)
}

/**
 * Run for \p buf_len samples in 'additive recurrence' mode, generating output.
 */
static sauMaybeUnused void sauRasG_map_addrec(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
#define RASG_MAP_addrec \
		uint32_t s0 = cycle * o->opt.alpha; \
		uint32_t s1 = (cycle+1) * o->opt.alpha; \
		float a = sau_fscalei(s0, 0x1p-31f); \
		float b = sau_fscalei(s1, 0x1p-31f); \
/**/
		RASG_MAP_addrec
		end_a_buf[i] = a;
		end_b_buf[i] = b;
	}
}

RASG_MAP_S_FUNC(addrec)

static inline sauRasG_map_f sauRasG_get_map_f(unsigned func) {
	switch (func) {
	default:
	case SAU_RAS_F_URAND: return sauRasG_map_urand;
	case SAU_RAS_F_GAUSS: return sauRasG_map_gauss;
	case SAU_RAS_F_BIN: return sauRasG_map_bin;
	case SAU_RAS_F_TERN: return sauRasG_map_tern;
	case SAU_RAS_F_FIXED: return sauRasG_map_fixed;
	case SAU_RAS_F_ADDREC: return sauRasG_map_addrec;
	}
}

/**
 * Run for \p buf_len samples, generating output.
 * Expects phase values to be held inside \p main_buf;
 * they will be replaced by the output.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run(sauRasG *restrict o,
		size_t buf_len,
		void *restrict main_buf,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	sauRasG_map_f map = sauRasG_get_map_f(o->opt.func);
	map(o, buf_len, end_a_buf, end_b_buf, cycle_buf);
	sauPhasor_ui32tof(&o->phasor, main_buf, buf_len);
	const unsigned flags = o->opt.flags, line = o->opt.line;
	float *buf = main_buf;
	if (flags & SAU_RAS_O_PERLIN) {
		const float perlin_amp =
			(flags & (SAU_RAS_O_HALFSHAPE|SAU_RAS_O_ZIGZAG)) ?
			1.f :
			sauLine_coeffs[line].perlin_amp;
		for (size_t i = 0; i < buf_len; ++i) {
			float phase = buf[i];
			end_a_buf[i] *= perlin_amp*phase;
			end_b_buf[i] *= perlin_amp*(phase-1.f);
		}
	}
	if (flags & SAU_RAS_O_HALFSHAPE) {
		/* sort value-pairs, for a decreasing sawtooth-like waveform */
		for (size_t i = 0; i < buf_len; ++i) {
			float a = end_a_buf[i];
			float b = end_b_buf[i];
			end_a_buf[i] = sau_maxf(a, b);
			end_b_buf[i] = sau_minf(a, b);
		}
	}
	if (flags & SAU_RAS_O_ZIGZAG) {
		/* swap half-cycle ends for jagged shape on random amplitude */
		float *tmp_buf = end_a_buf;
		end_a_buf = end_b_buf; end_b_buf = tmp_buf;
	}
	if (flags & SAU_RAS_O_SQUARE) {
		/* square keeping sign; value uniformity to energy uniformity */
		for (size_t i = 0; i < buf_len; ++i) {
			end_a_buf[i] *= fabsf(end_a_buf[i]);
			end_b_buf[i] *= fabsf(end_b_buf[i]);
		}
	}
	sauLine_map_funcs[line](buf, buf_len,
			end_a_buf, end_b_buf);
#if RASG_MEASURE_LINE_AMP /* measure output, needed for Perlin mode coeff */
	static float min, max;
	for (size_t i = 0; i < buf_len; ++i) {
		min = sau_minf(buf[i], min);
		max = sau_maxf(buf[i], max);
	}
	printf("%.11f, %.11f\n", min, max);
#endif
}

static inline sauRasG_map_selfmod_f sauRasG_get_map_selfmod_f(unsigned func) {
	switch (func) {
	default:
	case SAU_RAS_F_URAND: return sauRasG_map_urand_s;
	case SAU_RAS_F_GAUSS: return sauRasG_map_gauss_s;
	case SAU_RAS_F_BIN: return sauRasG_map_bin_s;
	case SAU_RAS_F_TERN: return sauRasG_map_tern_s;
	case SAU_RAS_F_FIXED: return sauRasG_map_fixed_s;
	case SAU_RAS_F_ADDREC: return sauRasG_map_addrec_s;
	}
}

/**
 * Run for \p buf_len samples, generating output, with self-modulation.
 * Expects phase values to be held inside \p main_buf;
 * they will be replaced by the output.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run_selfmod(sauRasG *restrict o,
		size_t buf_len,
		void *restrict main_buf,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf) {
	sauPhasor_ui32tof(&o->phasor, main_buf, buf_len);
	sauRasG_map_selfmod_f map = sauRasG_get_map_selfmod_f(o->opt.func);
	sauLine_val_f line_f = sauLine_val_funcs[o->opt.line];
	map(o, buf_len, main_buf, line_f, cycle_buf, pm_abuf);
}
