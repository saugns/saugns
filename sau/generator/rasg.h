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
	uint8_t line, mode, m_level;
	uint8_t flags;
//	float prev_x;
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
		.mode = SAU_RAS_F_RAND,
		.m_level = sau_ras_level(9 /* max one-digit number */),
		.flags = 0,
//		.prev_x = 0,
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
		o->mode = opt->func;
	if (opt->flags & SAU_RAS_O_LEVEL_SET)
		o->m_level = opt->level;
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
	return sau_ftoi(((float) UINT32_MAX) / (o->cyclor.coeff * freq));
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

static void sauRasG_run(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf);

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
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float fpm_scale = 1.f / SAU_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f), 0);
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * (float)INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * (float)INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			uint64_t cycle_phase = P(sau_ftoi(o->coeff * s_f),
					sau_ftoi(s_pofs * (float)INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	}
}

#undef P /* done */

/**
 * Run for \p buf_len samples in 'uniform random' mode, generating output.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run_rand(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	sauLine_map_f map = sauLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		float a = sau_ranoise32(cycle) * scale;
		float b = sau_ranoise32(cycle + 1) * scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'smoothed random' mode, generating output.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run_smooth(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	sauLine_map_f map = sauLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->m_level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		int32_t offs = INT32_MAX + (cycle & 1) * 2;
		float a = (sau_sar32(sau_ranoise32(cycle), sar)
				+ offs) * scale;
		float b = (sau_sar32(sau_ranoise32(cycle + 1), sar)
				- offs) * scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'binary random' mode, generating output.
 * For increasing \a m_level > 0, each new level is half as squiggly, for
 * a near-binary mode when above 5 (with best quality seemingly from 27).
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run_bin(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	sauLine_map_f map = sauLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->m_level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		int32_t offs = INT32_MAX + (cycle & 1) * 2;
		float a = (sau_sar32(sau_ranoise32(cycle), sar)
				+ offs) * scale;
		float b = (sau_sar32(sau_ranoise32(cycle + 1), sar)
				- offs) * scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'ternary random' mode, generating output.
 * For increasing \a m_level > 0, each new level is half as squiggly, with
 * a practically ternary mode when above 5, but 30 is technically perfect.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run_tern(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	sauLine_map_f map = sauLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	int sar = o->m_level;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		int32_t sb = (cycle & 1) << 31;
		float a = (sau_sar32(sau_ranoise32(cycle), sar)
				+ (1<<31)-sb) * scale; // is first to cos-align
		float b = (sau_sar32(sau_ranoise32(cycle + 1), sar)
				+ sb) * scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'fixed binary cycle' mode, generating output.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run_fixed(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	sauLine_map_f map = sauLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		float a = sau_oddness_as_sign(cycle);
		float b = -a;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples, generating output.
 *
 * Uses post-incremented phase each sample.
 */
static sauMaybeUnused void sauRasG_run(sauRasG *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	switch (o->mode) {
	case SAU_RAS_F_RAND:
		sauRasG_run_rand(o, buf, buf_len, cycle_buf, phase_buf); break;
	case SAU_RAS_F_BIN:
		sauRasG_run_bin(o, buf, buf_len, cycle_buf, phase_buf); break;
	case SAU_RAS_F_TERN:
		sauRasG_run_tern(o, buf, buf_len, cycle_buf, phase_buf); break;
	case SAU_RAS_F_SMOOTH:
		sauRasG_run_smooth(o, buf, buf_len, cycle_buf,phase_buf);break;
	case SAU_RAS_F_FIXED:
		sauRasG_run_fixed(o, buf, buf_len, cycle_buf, phase_buf);break;
	}
}
