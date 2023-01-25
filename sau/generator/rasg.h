/* SAU library: Random segments generator implementation.
 * Copyright (c) 2022-2023 Joel K. Pettersson
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
#include "../program.h"

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define sauCyclor_COEFF(srate) (((float) UINT32_MAX)/(srate))

typedef struct sauCyclor {
	uint64_t cycle_phase; /* cycle counter upper 32 bits, phase lower */
	float coeff;
} sauCyclor;

typedef struct sauRasG {
	sauCyclor cyclor;
	uint8_t line, func, level, flags;
} sauRasG;

/**
 * Initialize instance for use.
 */
static inline void sau_init_RasG(sauRasG *restrict o, uint32_t srate) {
	*o = (sauRasG){
		.cyclor = (sauCyclor){
			.cycle_phase = 0,
			.coeff = sauCyclor_COEFF(srate),
		},
		.line = SAU_LINE_N_lin,
		.func = SAU_RAS_F_RAND,
		.level = sau_ras_level(9 /* max one-digit number */),
		.flags = SAU_RAS_O_LINE_SET |
			SAU_RAS_O_FUNC_SET |
			SAU_RAS_O_LEVEL_SET,
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
	o->flags = opt->flags |
		SAU_RAS_O_LINE_SET |
		SAU_RAS_O_FUNC_SET |
		SAU_RAS_O_LEVEL_SET;
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t sauRasG_cycle_len(sauRasG *restrict o, float freq) {
	return lrintf(((float) UINT32_MAX) / (o->cyclor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t sauRasG_cycle_pos(sauRasG *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->cyclor.coeff * freq);
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
	uint32_t inc = lrintf(o->cyclor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - sauWave_SLEN) / inc;
}

#define P(inc, ofs) \
	ofs + o->cycle_phase; (o->cycle_phase += inc)     /* post-increment */

/**
 * Fill cycle-value and phase-value buffers for use with sauRasG_run().
 *
 * "Cycles" actually refer to PRNG states, advancing at 2x normal speed,
 * as two points (each from a state) are needed to match a normal cycle.
 */
static sauMaybeUnused void sauCyclor_fill(sauCyclor *restrict o,
		uint32_t *restrict cycle_ui32,
		float *restrict phase_f,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float inv_int32_max = 1.f/(float)INT32_MAX;
	const float fpm_scale = 1.f / SAU_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f), 0);
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
			uint32_t phase;
			cycle_ui32[i] = cycle_phase >> 31;
			phase = cycle_phase & ~(1<<31);
			phase_f[i] = ((int32_t) phase) * inv_int32_max;
		}
	}
}

#undef P /* done */

typedef void (*sauRasG_map_f)(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf);

/**
 * Run for \p buf_len samples in 'uniform random' mode, generating output.
 */
static sauMaybeUnused void sauRasG_map_rand(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	const float scale = 1.f/(float)INT32_MAX;
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		end_a_buf[i] = ((int32_t)sau_ranfast32(cycle)) * scale;
		end_b_buf[i] = ((int32_t)sau_ranfast32(cycle + 1)) * scale;
	}
}

/*
 * Approximation of a symmetric, highly soft-saturated modification
 * of expression "sqrtf(-2.f * logf(x + 0.5f))"; more precisely, of
 * "x > 0 ?
 *  sqrtf(-2.f * logf(x + 0.5f)) :
 *  (2.f*sqrtf(-2.f * logf(0.5f)) - sqrtf(-2.f * logf(0.5f - x)))".
 *
 * Unlike the original expression, the result is also downscaled by
 * "2.f*sqrtf(-2.f * logf(0.5f))" (otherwise the maximum value), to
 * the range of 0.0 to 1.0 inclusive.
 */
static inline float soft_sqrtm2logp1_2_r01(float x) {
	const float scale[] = {
		-0.80270565422983103084,
		+5.52274428214641442648,
		-138.87126103150588693697,
	};
	float x2 = x*x;
	float x4 = x2*x2;
	return 0.5f + x*(scale[0] + x4*(scale[1] + x4*scale[2]));
}

/*
 * Function used to distort initial soft-saurated curve
 * so as to make it look and sound approximately right.
 * (Graphed, looks like half a bell curve on its side.)
 */
static inline float ssgauss_dist4(float x) {
	float x2 = x*x;
	float gx = (x + x2)*0.5f;
	return x*(1 - gx*(1 - x2));
}

/**
 * Random access soft-saturated Gaussian noise, using approximation.
 *
 * Described at: https://joelkp.frama.io/blog/ran-softsat-gauss.html
 *
 * \return pseudo-random number between -1.0 and +1.0 for index \p n
 */
static inline float sau_franssgauss32(uint32_t n) {
	int32_t s0 = sau_ranfast32(n);
	int32_t s1 = sau_mcg32(s0);
	float a = s0 * 1.f/(float)UINT32_MAX;
	float b = s1 * 1.f/(float)UINT32_MAX;
	float c = ssgauss_dist4(soft_sqrtm2logp1_2_r01(a));
	b = c * sau_sinpi_d5f(b); // simplified for only sin(), no cos() output
	return b;
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
		end_a_buf[i] = sau_franssgauss32(cycle);
		end_b_buf[i] = sau_franssgauss32(cycle + 1);
	}
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
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		int32_t offs = INT32_MAX + (cycle & 1) * 2;
		end_a_buf[i] = (sau_sar32(sau_ranfast32(cycle), sar)
				+ offs) * scale;
		end_b_buf[i] = (sau_sar32(sau_ranfast32(cycle + 1), sar)
				- offs) * scale;
	}
}

/**
 * Run for \p buf_len samples in 'ternary random' mode, generating output.
 * For an increasing \a level > 0 each new level is half as squiggly, with
 * a practically ternary mode when above 5, but 30 is technically perfect.
 */
static sauMaybeUnused void sauRasG_map_tern(sauRasG *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		int32_t sb = (cycle & 1) << 31;
		end_a_buf[i] = (sau_sar32(sau_ranfast32(cycle), sar)
				+ (1<<31)-sb) * scale; // is first to cos-align
		end_b_buf[i] = (sau_sar32(sau_ranfast32(cycle + 1), sar)
				+ sb) * scale;
	}
}

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
		float a =
		end_a_buf[i] = sau_oddness_as_sign(cycle);
		end_b_buf[i] = -a;
	}
}

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
	const float scale = 1.f/(float)INT32_MAX;
	int slr = o->level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		int32_t sign = sau_oddness_as_sign(cycle);
		end_a_buf[i] = (-sign * (int32_t)
				(((uint32_t)sau_ranfast32(cycle) >> slr) -
				 INT32_MAX)) * scale;
		end_b_buf[i] = (sign * (int32_t)
				(((uint32_t)sau_ranfast32(cycle + 1) >> slr) -
				 INT32_MAX)) * scale;
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
	sauRasG_map_f map;
	switch (o->func) {
	default:
	case SAU_RAS_F_RAND: map = sauRasG_map_rand; break;
	case SAU_RAS_F_GAUSS: map = sauRasG_map_gauss; break;
	case SAU_RAS_F_BIN: map = sauRasG_map_bin; break;
	case SAU_RAS_F_TERN: map = sauRasG_map_tern; break;
	case SAU_RAS_F_FIXED: map = sauRasG_map_fixed;
		if (o->level >= sau_ras_level(9))
			map = sauRasG_map_fixed_simple;
		break;
	}
	map(o, buf_len, end_a_buf, end_b_buf, cycle_buf);
	if (o->flags & SAU_RAS_O_SQUARE) {
		// square keeping sign; value uniformity to energy uniformity
		for (size_t i = 0; i < buf_len; ++i) {
			end_a_buf[i] *= fabsf(end_a_buf[i]);
			end_b_buf[i] *= fabsf(end_b_buf[i]);
		}
	}
	sauLine_map_funcs[o->line](main_buf, buf_len, end_a_buf, end_b_buf);
}
