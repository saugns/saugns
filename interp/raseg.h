/* mgensys: Random segments implementation.
 * Copyright (c) 2022-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "../line.h"
#include "../program.h"

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define mgsCyclor_COEFF(srate) (((float) UINT32_MAX)/(srate))

typedef struct mgsCyclor {
	uint64_t cycle_phase; /* cycle counter upper 32 bits, phase lower */
	float coeff;
} mgsCyclor;

typedef struct mgsRaseg {
	mgsCyclor cyclor;
	uint8_t line, mode, m_level;
	uint8_t flags;
//	float prev_x;
} mgsRaseg;

/**
 * Initialize instance for use.
 */
static inline void mgs_init_Raseg(mgsRaseg *restrict o, uint32_t srate) {
	*o = (mgsRaseg){
		.cyclor = (mgsCyclor){
			.cycle_phase = 0,
			.coeff = mgsCyclor_COEFF(srate),
		},
		.line = MGS_LINE_N_lin,
		.mode = MGS_RASEG_MODE_RAND,
		.m_level = mgsRaseg_level(9 /* max one-digit number */),
		.flags = 0,
//		.prev_x = 0,
	};
}

static inline void mgsRaseg_set_cycle(mgsRaseg *restrict o, uint32_t cycle) {
	o->cyclor.cycle_phase =
		(o->cyclor.cycle_phase & UINT32_MAX) | (((uint64_t)cycle)<<32);
}

static inline void mgsRaseg_set_phase(mgsRaseg *restrict o, uint32_t phase) {
	o->cyclor.cycle_phase =
		(o->cyclor.cycle_phase & ~(uint64_t)UINT32_MAX) | phase;
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t mgsRaseg_cycle_len(mgsRaseg *restrict o, float freq) {
	return lrintf(((float) UINT32_MAX) / (o->cyclor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t mgsRaseg_cycle_pos(mgsRaseg *restrict o,
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
static inline int32_t mgsRaseg_cycle_offs(mgsRaseg *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->cyclor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - mgsWave_SLEN) / inc;
}

#define P(inc, ofs) \
	ofs + o->cycle_phase; (o->cycle_phase += inc)     /* post-increment */

/**
 * Fill cycle-value and phase-value buffers for use with mgsRaseg_run().
 *
 * "Cycles" actually refer to PRNG states, advancing at 2x normal speed,
 * as two points (each from a state) are needed to match a normal cycle.
 */
static mgsMaybeUnused void mgsCyclor_fill(mgsCyclor *restrict o,
		uint32_t *restrict cycle_ui32,
		float *restrict phase_f,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float inv_int32_max = 1.f/(float)INT32_MAX;
	const float fpm_scale = 1.f / MGS_HUMMID;
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

typedef void (*mgsRaseg_map_f)(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf);

/**
 * Run for \p buf_len samples in 'uniform random' mode, generating output.
 */
static mgsMaybeUnused void mgsRaseg_map_rand(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	const float scale = 1.f/(float)INT32_MAX;
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		end_a_buf[i] = ((int32_t)mgs_ranfast32(cycle)) * scale;
		end_b_buf[i] = ((int32_t)mgs_ranfast32(cycle + 1)) * scale;
	}
}

static inline float mgs_fgaussrand32(uint32_t n) {
	uint32_t s0 = mgs_ranfast32(n);
	uint32_t s1 = mgs_mcg32(s0);
	float a = s0 * 1.f/(float)UINT32_MAX;
	float b = s1 * 1.f/(float)UINT32_MAX;
	float c = 0.15566118875375304694f /* make overshoot rare (measured) */
		* sqrtf(-2.f * logf(a));
	a = c * cosf(2.f*(float)MGS_PI * b);
//	b = c * sinf(2.f*(float)MGS_PI * b);
//	a = b;
	return a;
}

/**
 * Run for \p buf_len samples in 'Gaussian random' mode, generating output.
 */
static mgsMaybeUnused void mgsRaseg_map_gauss(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		end_a_buf[i] = mgs_fgaussrand32(cycle);
		end_b_buf[i] = mgs_fgaussrand32(cycle + 1);
	}
}

/**
 * Run for \p buf_len samples in 'binary random' mode, generating output.
 * For increasing \a m_level > 0, each new level is half as squiggly, for
 * a near-binary mode when above 5 (with best quality seemingly from 27).
 */
static mgsMaybeUnused void mgsRaseg_map_bin(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->m_level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		int32_t offs = INT32_MAX + (cycle & 1) * 2;
		end_a_buf[i] = (mgs_sar32(mgs_ranfast32(cycle), sar)
				+ offs) * scale;
		end_b_buf[i] = (mgs_sar32(mgs_ranfast32(cycle + 1), sar)
				- offs) * scale;
	}
}

/**
 * Run for \p buf_len samples in 'ternary random' mode, generating output.
 * For increasing \a m_level > 0, each new level is half as squiggly, with
 * a practically ternary mode when above 5, but 30 is technically perfect.
 */
static mgsMaybeUnused void mgsRaseg_map_tern(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->m_level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		int32_t sb = (cycle & 1) << 31;
		end_a_buf[i] = (mgs_sar32(mgs_ranfast32(cycle), sar)
				+ (1<<31)-sb) * scale; // is first to cos-align
		end_b_buf[i] = (mgs_sar32(mgs_ranfast32(cycle + 1), sar)
				+ sb) * scale;
	}
}

/**
 * Run for \p buf_len samples in 'fixed cycle' mode, generating output.
 * Simple version, optimizing high level (pure base frequency) setting.
 */
static mgsMaybeUnused void mgsRaseg_map_fixed_simple(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	(void)o;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		float a =
		end_a_buf[i] = mgs_oddness_as_sign(cycle);
		end_b_buf[i] = -a;
	}
}

/**
 * Run for \p buf_len samples in 'fixed cycle' mode, generating output.
 * For increasing \a m_level > 0, each new level halves the randomness,
 * the base frequency amplifying in its place (toward ultimate purity).
 */
static mgsMaybeUnused void mgsRaseg_map_fixed(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	const float scale = 1.f/(float)INT32_MAX;
	int slr = o->m_level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		int32_t sign = mgs_oddness_as_sign(cycle);
		end_a_buf[i] = (-sign * (int32_t)
				(((uint32_t)mgs_ranfast32(cycle) >> slr) -
				 INT32_MAX)) * scale;
		end_b_buf[i] = (sign * (int32_t)
				(((uint32_t)mgs_ranfast32(cycle + 1) >> slr) -
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
static mgsMaybeUnused void mgsRaseg_run(mgsRaseg *restrict o,
		size_t buf_len,
		float *restrict main_buf,
		float *restrict end_a_buf,
		float *restrict end_b_buf,
		const uint32_t *restrict cycle_buf) {
	mgsRaseg_map_f map;
	switch (o->mode & MGS_RASEG_MFUNC_MASK) {
	default:
	case MGS_RASEG_MODE_RAND: map = mgsRaseg_map_rand; break;
	case MGS_RASEG_MODE_GAUSS: map = mgsRaseg_map_gauss; break;
	case MGS_RASEG_MODE_BIN: map = mgsRaseg_map_bin; break;
	case MGS_RASEG_MODE_TERN: map = mgsRaseg_map_tern; break;
	case MGS_RASEG_MODE_FIXED: map = mgsRaseg_map_fixed;
		if (o->m_level >= mgsRaseg_level(9))
			map = mgsRaseg_map_fixed_simple;
		break;
	}
	map(o, buf_len, end_a_buf, end_b_buf, cycle_buf);
	if (o->mode & MGS_RASEG_MEXT_SQ) {
		// square keeping sign; value uniformity to energy uniformity
		for (size_t i = 0; i < buf_len; ++i) {
			end_a_buf[i] *= fabsf(end_a_buf[i]);
			end_b_buf[i] *= fabsf(end_b_buf[i]);
		}
	}
	mgsLine_map_funcs[o->line](main_buf, buf_len, end_a_buf, end_b_buf);
}
