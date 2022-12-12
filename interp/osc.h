/* mgensys: Oscillator implementation.
 * Copyright (c) 2011, 2017-2022 Joel K. Pettersson
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
#include "../wave.h"
#include "../math.h"

/*
 * Use pre-integrated LUTs ("PILUTs")?
 *
 * Turn off to use the raw naive LUTs,
 * kept for testing/"viewing" of them.
 */
#define USE_PILUT 1

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define mgsPhasor_COEFF(srate) (((float) UINT32_MAX)/(srate))

typedef struct mgsPhasor {
	uint32_t phase;
	float coeff;
} mgsPhasor;

#define MGS_OSC_RESET_DIFF  (1<<0)
#define MGS_OSC_RESET       ((1<<1) - 1)

typedef struct mgsOsc {
	mgsPhasor phasor;
	uint8_t wave;
	uint8_t flags;
#if USE_PILUT
	uint32_t prev_phase;
	double prev_Is;
	float prev_diff_s;
#endif
} mgsOsc;

/**
 * Initialize instance for use.
 */
static inline void mgs_init_Osc(mgsOsc *restrict o, uint32_t srate) {
	*o = (mgsOsc){
#if USE_PILUT
		.phasor = (mgsPhasor){
			.phase = mgsWave_picoeffs[MGS_WAVE_N_sin].phase_adj,
			.coeff = mgsPhasor_COEFF(srate),
		},
#else
		.phasor = (mgsPhasor){
			.phase = 0,
			.coeff = mgsPhasor_COEFF(srate),
		},
#endif
		.wave = MGS_WAVE_N_sin,
		.flags = MGS_OSC_RESET,
	};
}

static inline void mgsOsc_set_phase(mgsOsc *restrict o, uint32_t phase) {
#if USE_PILUT
	o->phasor.phase = phase + mgsWave_picoeffs[o->wave].phase_adj;
#else
	o->phasor.phase = phase;
#endif
}

static inline void mgsOsc_set_wave(mgsOsc *restrict o, uint8_t wave) {
#if USE_PILUT
	int32_t old_offset = mgsWave_picoeffs[o->wave].phase_adj;
	int32_t offset = mgsWave_picoeffs[wave].phase_adj;
	o->phasor.phase += offset - old_offset;
	o->wave = wave;
	o->flags |= MGS_OSC_RESET_DIFF;
#else
	o->wave = wave;
#endif
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t mgsOsc_cycle_len(mgsOsc *restrict o, float freq) {
	return lrintf(((float) UINT32_MAX) / (o->phasor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t mgsOsc_cycle_pos(mgsOsc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return phs / inc;
}

/**
 * Calculate offset relative to wave cycle for \p freq, based on \p pos.
 *
 * Can be used to reduce time length to something rounder and reduce clicks.
 */
static inline int32_t mgsOsc_cycle_offs(mgsOsc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - mgsWave_SLEN) / inc;
}

#if !USE_PILUT
# define P(inc, ofs) ofs + o->phase; (o->phase += inc)     /* post-increment */
#else
# define P(inc, ofs) ofs + (o->phase += inc)               /* pre-increment */
#endif

/**
 * Fill phase-value buffer for use with mgsOsc_run().
 */
static mgsMaybeUnused void mgsPhasor_fill(mgsPhasor *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float fpm_scale = 1.f / MGS_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			phase_ui32[i] = P(lrintf(o->coeff * s_f), 0);
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			phase_ui32[i] = P(lrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			phase_ui32[i] = P(lrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			phase_ui32[i] = P(lrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
		}
	}
}

#undef P /* done */

#if !USE_PILUT
/*
 * Implementation of mgsOsc_run()
 * using naive LUTs with linear interpolation.
 *
 * Uses post-incremented phase each sample.
 */
static void mgsOsc_naive_run(mgsOsc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
	const float *const lut = mgsWave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		buf[i] = mgsWave_get_lerp(lut, phase_buf[i]);
	}
}
#endif

#if USE_PILUT
/* Set up for differentiation (re)start with usable state. */
static void mgsOsc_reset(mgsOsc *restrict o, int32_t phase) {
	const float *const lut = mgsWave_piluts[o->wave];
	const float diff_scale = mgsWave_DVSCALE(o->wave);
	const float diff_offset = mgsWave_DVOFFSET(o->wave);
	if (o->flags & MGS_OSC_RESET_DIFF) {
		/* one-LUT-value diff works fine for any freq, 0 Hz included */
		int32_t phase_diff = mgsWave_SLEN;
		o->prev_Is = mgsWave_get_herp(lut, phase - phase_diff);
		double Is = mgsWave_get_herp(lut, phase);
		double x = (diff_scale / phase_diff);
		o->prev_diff_s = (Is - o->prev_Is) * x + diff_offset;
		o->prev_Is = Is;
		o->prev_phase = phase;
	}
	o->flags &= ~MGS_OSC_RESET;
}
#endif

/**
 * Run for \p buf_len samples, generating output.
 *
 * Uses pre-incremented phase each sample.
 */
static mgsMaybeUnused void mgsOsc_run(mgsOsc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
#if USE_PILUT /* higher-quality audio */
	const float *const lut = mgsWave_piluts[o->wave];
	const float diff_scale = mgsWave_DVSCALE(o->wave);
	const float diff_offset = mgsWave_DVOFFSET(o->wave);
	if (buf_len > 0 && o->flags & MGS_OSC_RESET)
		mgsOsc_reset(o, phase_buf[0]);
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t phase = phase_buf[i];
		int32_t phase_diff = phase - o->prev_phase;
		if (phase_diff == 0) {
			s = o->prev_diff_s;
		} else {
			double Is = mgsWave_get_herp(lut, phase);
			double x = (diff_scale / phase_diff);
			s = (Is - o->prev_Is) * x + diff_offset;
			o->prev_Is = Is;
			o->prev_diff_s = s;
			o->prev_phase = phase;
		}
		buf[i] = s;
	}
#else /* test naive LUT */
	mgsOsc_naive_run(o, buf, buf_len, phase_buf);
#endif
}
