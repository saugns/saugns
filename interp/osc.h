/* ssndgen: Oscillator implementation.
 * Copyright (c) 2011, 2017-2020 Joel K. Pettersson
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

typedef struct SSG_Osc {
	uint32_t phase;
	float coeff;
	const float *lut;
} SSG_Osc;

/**
 * Convert floating point phase value (0.0 = 0 deg., 1.0 = 360 deg.)
 * to 32-bit unsigned int, as used by oscillator.
 */
#define SSG_Osc_PHASE(p) ((uint32_t) lrint((p) * 4294967296.0))

/**
 * Calculate the coefficent, based on the sample rate,
 * used to give the per-sample phase increment
 * by multiplying with the frequency used.
 */
#define SSG_Osc_COEFF(srate) ((float) 4294967296.0/(srate))

/**
 * Get LUT for wave type enum.
 */
#define SSG_Osc_LUT(wave) \
	(SSG_Wave_luts[(wave) < SSG_WAVE_TYPES ? (wave) : SSG_WAVE_SIN])

/**
 * Initialize instance for use.
 */
static inline void SSG_init_Osc(SSG_Osc *restrict o, uint32_t srate) {
	o->phase = 0;
	o->coeff = SSG_Osc_COEFF(srate);
	o->lut = SSG_Osc_LUT(SSG_WAVE_SIN);
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t SSG_Osc_cycle_len(SSG_Osc *restrict o, float freq) {
	return lrintf(4294967296.0 / (o->coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t SSG_Osc_cycle_pos(SSG_Osc *restrict o,
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
static inline int32_t SSG_Osc_cycle_offs(SSG_Osc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - SSG_Wave_SCALE) / inc;
}

/**
 * Get next sample.
 *
 * \return value from -1.0 to 1.0
 */
static inline float SSG_Osc_get(SSG_Osc *restrict o,
		float freq, int32_t pm_s32) {
	uint32_t phase = o->phase + pm_s32;
	float s = SSG_Wave_get_lerp(o->lut, phase);
	o->phase += lrintf(o->coeff * freq);
	return s;
}

void SSG_Osc_run(SSG_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f);
void SSG_Osc_run_env(SSG_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f);
