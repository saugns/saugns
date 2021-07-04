/* sgensys: Oscillator implementation.
 * Copyright (c) 2011, 2017-2021 Joel K. Pettersson
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

#define SGS_OSC_RESET_DIFF (1<<0)

typedef struct SGS_Osc {
	uint32_t phase;
	float coeff;
	uint8_t wave;
	uint8_t flags;
	int32_t phase_inc;
	float prev_diff_s;
} SGS_Osc;

/**
 * Convert floating point phase value (0.0 = 0 deg., 1.0 = 360 deg.)
 * to 32-bit unsigned int, as used by oscillator.
 */
#define SGS_Osc_PHASE(p) ((uint32_t) lrintf((p) * (float) UINT32_MAX))

/**
 * Calculate the coefficent, based on the sample rate,
 * used to give the per-sample phase increment
 * by multiplying with the frequency used.
 */
#define SGS_Osc_COEFF(srate) (((float) UINT32_MAX)/(srate))

/**
 * Initialize instance for use.
 */
static inline void SGS_init_Osc(SGS_Osc *restrict o, uint32_t srate) {
	*o = (SGS_Osc){
		.phase = 0,
		.coeff = SGS_Osc_COEFF(srate),
		.wave = SGS_WAVE_SIN,
		.flags = SGS_OSC_RESET_DIFF,
	};
}

static inline void SGS_Osc_set_phase(SGS_Osc *restrict o, float phase) {
	o->phase = SGS_Osc_PHASE(phase);
	o->flags |= SGS_OSC_RESET_DIFF;
}

static inline void SGS_Osc_set_wave(SGS_Osc *restrict o, uint8_t wave) {
	o->wave = wave;
	o->flags |= SGS_OSC_RESET_DIFF;
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t SGS_Osc_cycle_len(SGS_Osc *restrict o, float freq) {
	return lrintf(((float) UINT32_MAX) / (o->coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t SGS_Osc_cycle_pos(SGS_Osc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->coeff * freq);
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
	uint32_t inc = lrintf(o->coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - SGS_Wave_SCALE) / inc;
}

void SGS_Osc_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f);
void SGS_Osc_run_env(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f);
