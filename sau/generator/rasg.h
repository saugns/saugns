/* SAU library: Random segments generator implementation.
 * Copyright (c) 2022-2024 Joel K. Pettersson
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
#include "../line.h"
#include "noise.h"

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define sauCyclor_COEFF(srate) SAU_INV_FREQ(32, srate)

typedef struct sauCyclor {
	uint64_t cycle_phase; /* cycle counter upper 32 bits, phase lower */
	float coeff;
	bool rate2x;
} sauCyclor;

typedef struct sauRasG {
	sauCyclor cyclor;
	uint8_t line, func, level, flags;
	float prev_s, fb_s;
	uint32_t alpha;
} sauRasG;

/**
 * Initialize instance for use.
 */
static inline void sau_init_RasG(sauRasG *restrict o, uint32_t srate) {
	*o = (sauRasG){
		.cyclor = (sauCyclor){
			.cycle_phase = 0,
			.coeff = sauCyclor_COEFF(srate),
			.rate2x = true,
		},
		.line = SAU_LINE_N_lin,
		.func = SAU_RAS_F_URAND,
		.level = sau_ras_level(9 /* max one-digit number */),
		.alpha = SAU_FIBH32, // use golden ratio as default
		.flags = SAU_RAS_O_LINE_SET |
			SAU_RAS_O_FUNC_SET |
			SAU_RAS_O_LEVEL_SET |
			SAU_RAS_O_ASUBVAL_SET,
	};
}

static inline void sauRasG_set_cycle(sauRasG *restrict o, uint32_t cycle) {
	o->cyclor.cycle_phase =
		(o->cyclor.cycle_phase & UINT32_MAX) | (((uint64_t)cycle)<<32);
}

static inline void sauRasG_set_phase(sauRasG *restrict o, uint32_t phase) {
	o->cyclor.cycle_phase =
		(o->cyclor.cycle_phase & ~(uint64_t)UINT32_MAX) | phase;
}

static void sauRasG_set_opt(sauRasG *restrict o,
		const sauRasOpt *restrict opt) {
	if (opt->flags & SAU_RAS_O_LINE_SET)
		o->line = opt->line;
	if (opt->flags & SAU_RAS_O_FUNC_SET)
		o->func = opt->func;
	if (opt->flags & SAU_RAS_O_LEVEL_SET)
		o->level = opt->level;
	if (opt->flags & SAU_RAS_O_ASUBVAL_SET)
		o->alpha = opt->alpha;
	o->flags = opt->flags |
		SAU_RAS_O_LINE_SET |
		SAU_RAS_O_FUNC_SET |
		SAU_RAS_O_LEVEL_SET;
	o->cyclor.rate2x = !(o->flags & SAU_RAS_O_HALFSHAPE);
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t sauRasG_cycle_len(sauRasG *restrict o, float freq) {
	return sau_ftoi(SAU_INV_FREQ(32, o->cyclor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t sauRasG_cycle_pos(sauRasG *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = sau_ftoi(o->cyclor.coeff * freq);
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
	uint32_t inc = sau_ftoi(o->cyclor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - sauWave_SLEN) / inc;
}

#define P(inc, ofs) \
	ofs + o->cycle_phase; (o->cycle_phase += inc)     /* post-increment */

/**
 * Fill cycle-value and phase-value buffers with 1x frequency rate.
 * Used for sawtooth-like waves needing one line segment per cycle.
 */
static sauMaybeUnused void sauCyclor_fill_rate1x(sauCyclor *restrict o,
		uint32_t *restrict cycle_ui32,
		float *restrict phase_f,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float inv_int32_max = 0x1p-31f;
	const float fpm_scale = 1.f / SAU_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f), 0);
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 32;
			phase = (cycle_phase >> 1) & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * 0x1p31f));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 32;
			phase = (cycle_phase >> 1) & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * 0x1p31f));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 32;
			phase = (cycle_phase >> 1) & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * 0x1p31f));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 32;
			phase = (cycle_phase >> 1) & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	}
}

/**
 * Fill cycle-value and phase-value buffers with 2x frequency rate.
 * Used for waveforms where each real cycle uses two "cycle" lines.
 */
static sauMaybeUnused void sauCyclor_fill_rate2x(sauCyclor *restrict o,
		uint32_t *restrict cycle_ui32,
		float *restrict phase_f,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float inv_int32_max = 0x1p-31f;
	const float fpm_scale = 1.f / SAU_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f), 0);
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * 0x1p31f));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * 0x1p31f));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * 0x1p31f));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1U<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	}
}

/**
 * Fill cycle-value and phase-value buffers for use with sauRasG_run().
 *
 * "Cycles" may have 2x the normal speed while mapped to line sgements.
 * Most simple waveforms need two line segments per cycle, sawtooth and
 * similar being the one-segment exceptions. Randomization maps a cycle
 * to a PRNG state with two neighboring states used for a line segment.
 */
static sauMaybeUnused void sauCyclor_fill(sauCyclor *restrict o,
		uint32_t *restrict cycle_ui32,
		float *restrict phase_f,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	(o->rate2x ? sauCyclor_fill_rate2x : sauCyclor_fill_rate1x)
		(o, cycle_ui32, phase_f, buf_len, freq_f, pm_f, fpm_f);
}

#undef P /* done */

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
for (size_t i = 0; i < buf_len; ++i) { \
	float pm_a = o->fb_s * pm_abuf[i] * 0.5f; \
	float phase = main_buf[i] + pm_a; \
	int32_t cycle_adj = floorf(phase); \
	uint32_t cycle = cycle_buf[i] + cycle_adj; \
	/**/ RASG_MAP_##loop_for_func \
	if (o->flags & SAU_RAS_O_HALFSHAPE) { \
		/* sort value-pairs, for a decreasing sawtooth-like waveform */\
		float max = sau_maxf(a, b); \
		float min = sau_minf(a, b); \
		a = max; \
		b = min; \
	} \
	if (o->flags & SAU_RAS_O_SQUARE) { \
		/* square keeping sign; value uniformity to energy uniformity*/\
		a *= fabsf(a); \
		b *= fabsf(b); \
	} \
	if (o->flags & SAU_RAS_O_ZIGZAG) { \
		/* swap half-cycle ends for jagged shape on random amplitude */\
		float tmp = a; a = b; b = tmp; \
	} \
	float s = line_f(phase - cycle_adj, a, b); \
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
	int sauMaybeUnused sr = o->level; \
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
	if (o->flags & SAU_RAS_O_VIOLET) {
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
	if (o->flags & SAU_RAS_O_VIOLET) {
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
	int sr = o->level;
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
	int sr = o->level;
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
	if (o->flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_bin(o, buf_len, end_a_buf, end_b_buf, cycle_buf);
		return;
	}
	int sr = o->level;
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
	if (o->flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_bin_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	int sr = o->level;
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
	int sr = o->level;
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
	int sr = o->level;
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
	if (o->level >= sau_ras_level(9)) {
		sauRasG_map_fixed_simple(o, buf_len, end_a_buf, end_b_buf,
				cycle_buf);
		return;
	}
	if (o->flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_fixed(o, buf_len, end_a_buf, end_b_buf,
				cycle_buf);
		return;
	}
	int sr = o->level;
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
	if (o->level >= sau_ras_level(9)) {
		sauRasG_map_fixed_simple_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	if (o->flags & SAU_RAS_O_VIOLET) {
		sauRasG_map_v_fixed_s(o, buf_len, main_buf, line_f,
				cycle_buf, pm_abuf);
		return;
	}
	int sr = o->level;
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
		uint32_t s0 = cycle * o->alpha; \
		uint32_t s1 = (cycle+1) * o->alpha; \
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
		float *restrict main_buf,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	sauRasG_map_f map = sauRasG_get_map_f(o->func);
	map(o, buf_len, end_a_buf, end_b_buf, cycle_buf);
	if (o->flags & SAU_RAS_O_HALFSHAPE) {
		/* sort value-pairs, for a decreasing sawtooth-like waveform */
		for (size_t i = 0; i < buf_len; ++i) {
			float a = end_a_buf[i];
			float b = end_b_buf[i];
			end_a_buf[i] = sau_maxf(a, b);
			end_b_buf[i] = sau_minf(a, b);
		}
	}
	if (o->flags & SAU_RAS_O_SQUARE) {
		/* square keeping sign; value uniformity to energy uniformity */
		for (size_t i = 0; i < buf_len; ++i) {
			end_a_buf[i] *= fabsf(end_a_buf[i]);
			end_b_buf[i] *= fabsf(end_b_buf[i]);
		}
	}
	if (o->flags & SAU_RAS_O_ZIGZAG) {
		/* swap half-cycle ends for jagged shape on random amplitude */
		float *tmp_buf = end_a_buf;
		end_a_buf = end_b_buf; end_b_buf = tmp_buf;
	}
	sauLine_map_funcs[o->line](main_buf, buf_len, end_a_buf, end_b_buf);
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
		float *restrict main_buf,
		const uint32_t *restrict cycle_buf,
		const float *restrict pm_abuf) {
	sauRasG_map_selfmod_f map = sauRasG_get_map_selfmod_f(o->func);
	sauLine_val_f line_f = sauLine_val_funcs[o->line];
	map(o, buf_len, main_buf, line_f, cycle_buf, pm_abuf);
}
