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
	uint8_t line, mode;
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
		.flags = 0,
//		.prev_x = 0,
	};
}

static inline void mgsRaseg_set_cycle(mgsRaseg *restrict o, uint32_t cycle) {
	o->cyclor.cycle_phase =
		(o->cyclor.cycle_phase & UINT32_MAX) | (((uint64_t)cycle)<<32);
}

static inline void mgsRaseg_set_phase(mgsRaseg *restrict o, uint32_t phase) {
	o->cyclor.cycle_phase = (o->cyclor.cycle_phase & ~UINT32_MAX) | phase;
}

static inline void mgsRaseg_set_line(mgsRaseg *restrict o, uint8_t line) {
	o->line = line;
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

static void mgsRaseg_run(mgsRaseg *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf);

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
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float fpm_scale = 1.f / MGS_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f), 0);
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 31;
			phase_ui32[i] = cycle_phase << 1;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
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
static mgsMaybeUnused void mgsRaseg_run_rand(mgsRaseg *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	mgsLine_map_f map = mgsLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		float a = mgs_ranoise32(cycle) * scale;
		float b = mgs_ranoise32(cycle + 1) * scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'smoothed random' mode, generating output.
 *
 * Uses post-incremented phase each sample.
 */
static mgsMaybeUnused void mgsRaseg_run_smooth(mgsRaseg *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	mgsLine_map_f map = mgsLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		int32_t sb = (cycle & 1) << 31;
		float a = (sb + mgs_ars32(mgs_ranoise32(cycle), 1))
			* scale;
		float b = ((1<<31)-sb + mgs_ars32(mgs_ranoise32(cycle + 1), 1))
			* scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'super-smoothed ternary random' mode,
 * generating output.
 *
 * Uses post-incremented phase each sample.
 */
static mgsMaybeUnused void mgsRaseg_run_tern(mgsRaseg *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	mgsLine_map_f map = mgsLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		int32_t sb = (cycle & 1) << 31;
		float a = (sb + mgs_ars32(mgs_ranoise32(cycle), 31))
			* scale;
		float b = ((1<<31)-sb + mgs_ars32(mgs_ranoise32(cycle + 1), 31))
			* scale;
		float p = ((int32_t) (phase >> 1)) * scale;
		map(&buf[i], 1, a, b, &p);
	}
}

/**
 * Run for \p buf_len samples in 'fixed binary cycle' mode, generating output.
 *
 * Uses post-incremented phase each sample.
 */
static mgsMaybeUnused void mgsRaseg_run_fixed(mgsRaseg *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	mgsLine_map_f map = mgsLine_map_funcs[o->line];
	const float scale = 1.f/(float)INT32_MAX;
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		float a = mgs_oddness_as_sign(cycle);
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
static mgsMaybeUnused void mgsRaseg_run(mgsRaseg *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict cycle_buf,
		const uint32_t *restrict phase_buf) {
	switch (o->mode) {
	case MGS_RASEG_MODE_RAND:
		mgsRaseg_run_rand(o, buf, buf_len, cycle_buf, phase_buf); break;
	case MGS_RASEG_MODE_SMOOTH:
		mgsRaseg_run_smooth(o, buf, buf_len, cycle_buf,phase_buf);break;
	case MGS_RASEG_MODE_TERN:
		mgsRaseg_run_tern(o, buf, buf_len, cycle_buf, phase_buf); break;
	case MGS_RASEG_MODE_FIXED:
		mgsRaseg_run_fixed(o, buf, buf_len, cycle_buf, phase_buf);break;
	}
}
