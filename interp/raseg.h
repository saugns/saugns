/* mgensys: Random segments implementation.
 * Copyright (c) 2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once
#include "../line.h"

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define mgsCyclor_COEFF(srate) (((float) UINT32_MAX)/(srate))

typedef struct mgsCyclor {
	uint64_t cycle_phase; /* cycle counter upper 32 bits, phase lower */
	float coeff;
} mgsCyclor;

static void mgsCyclor_fill(mgsCyclor *restrict o,
		uint32_t *restrict cycle_ui32,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f);

//#define MGS_OSC_RESET_DIFF  (1<<0)
//#define MGS_OSC_RESET       ((1<<1) - 1)

typedef struct mgsRaseg {
	mgsCyclor cyclor;
	uint8_t line;
	uint8_t flags;
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
		.flags = 0,
	};
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
 * Fill phase-value buffer for use with mgsRaseg_run().
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
			cycle_ui32[i] = cycle_phase >> 32;
			phase_ui32[i] = cycle_phase;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float) INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 32;
			phase_ui32[i] = cycle_phase;
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float) INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 32;
			phase_ui32[i] = cycle_phase;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			uint64_t cycle_phase = P(llrintf(o->coeff * s_f),
					llrintf(s_pofs * (float) INT32_MAX));
			cycle_ui32[i] = cycle_phase >> 32;
			phase_ui32[i] = cycle_phase;
		}
	}
}

#undef P /* done */

static mgsMaybeUnused void mgsRaseg_reset(mgsRaseg *restrict o, int32_t phase) {
	//if (o->flags & MGS_OSC_RESET_DIFF) {
	//}
	//o->flags &= ~MGS_OSC_RESET;
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
//	if (buf_len > 0 && o->flags & MGS_OSC_RESET)
//		mgsRaseg_reset(o, phase_buf[0]);
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t cycle = cycle_buf[i];
		uint32_t phase = phase_buf[i];
		int32_t x = mgs_ranoise32(cycle);
		s = x * 1.f/(float)INT32_MAX;
		buf[i] = s;
	}
}
