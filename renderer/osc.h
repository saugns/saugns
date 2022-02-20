/* sgensys: Oscillator implementation.
 * Copyright (c) 2011, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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
 * Convert floating point phase value (0.0 = 0 deg., 1.0 = 360 deg.)
 * to 32-bit unsigned int, as used by freqor for oscillator.
 */
#define SGS_Freqor_PHASE(p) ((uint32_t) lrintf((p) * (float) UINT32_MAX))

/**
 * Calculate the coefficent, based on the sample rate,
 * used to give the per-sample phase increment
 * by multiplying with the frequency used.
 */
#define SGS_Freqor_COEFF(srate) (((float) UINT32_MAX)/(srate))

typedef struct SGS_Freqor {
	uint32_t phase;
	float coeff;
} SGS_Freqor;

void SGS_Freqor_fill(SGS_Freqor *restrict o,
		int32_t *restrict pinc_i32,
		int32_t *restrict pofs_i32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f);

#define SGS_OSC_RESET_DIFF  (1<<0)
#define SGS_OSC_RESET       ((1<<1) - 1)

typedef struct SGS_Osc {
	SGS_Freqor freqor;
	uint8_t wave;
	uint8_t flags;
#if USE_PILUT
	uint32_t prev_phase;
	double prev_Is;
	float prev_diff_s;
#endif
} SGS_Osc;

/**
 * Initialize instance for use.
 */
static inline void SGS_init_Osc(SGS_Osc *restrict o, uint32_t srate) {
	*o = (SGS_Osc){
#if USE_PILUT
		.freqor = (SGS_Freqor){
			.phase = SGS_Wave_picoeffs[SGS_WAVE_SIN].phase_adj,
			.coeff = SGS_Freqor_COEFF(srate),
		},
#else
		.freqor = (SGS_Freqor){
			.phase = 0,
			.coeff = SGS_Freqor_COEFF(srate),
		},
#endif
		.wave = SGS_WAVE_SIN,
		.flags = SGS_OSC_RESET,
	};
}

static inline void SGS_Osc_set_phase(SGS_Osc *restrict o, uint32_t phase) {
#if USE_PILUT
	o->freqor.phase = phase + SGS_Wave_picoeffs[o->wave].phase_adj;
#else
	o->freqor.phase = phase;
#endif
}

static inline void SGS_Osc_set_wave(SGS_Osc *restrict o, uint8_t wave) {
#if USE_PILUT
	int32_t old_offset = SGS_Wave_picoeffs[o->wave].phase_adj;
	int32_t offset = SGS_Wave_picoeffs[wave].phase_adj;
	o->freqor.phase += offset - old_offset;
	o->wave = wave;
	o->flags |= SGS_OSC_RESET_DIFF;
#else
	o->wave = wave;
#endif
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t SGS_Osc_cycle_len(SGS_Osc *restrict o, float freq) {
	return lrintf(((float) UINT32_MAX) / (o->freqor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t SGS_Osc_cycle_pos(SGS_Osc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->freqor.coeff * freq);
	uint32_t phs = inc * pos;
	return phs / inc;
}

/**
 * Calculate offset relative to wave cycle for \p freq, based on \p pos.
 *
 * Can be used to reduce time length to something rounder and reduce clicks.
 */
static inline int32_t SGS_Osc_cycle_offs(SGS_Osc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->freqor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - SGS_Wave_SLEN) / inc;
}

void SGS_Osc_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		const int32_t *restrict pinc_buf,
		const int32_t *restrict pofs_buf);
