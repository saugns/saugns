/* saugns: Oscillator implementation.
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

/*
 * Use pre-integrated LUTs ("PILUTs")?
 *
 * Turn off to use the raw naive LUTs,
 * kept for testing/"viewing" of them.
 */
#define USE_PILUT 1

#define SAU_OSC_RESET_DIFF  (1<<0)
#define SAU_OSC_RESET       ((1<<1) - 1)

typedef struct SAU_Osc {
	uint32_t phase;
	float coeff;
	uint8_t wave;
	uint8_t flags;
#if USE_PILUT
	uint32_t prev_phase;
	double prev_Is;
	float prev_diff_s;
#endif
} SAU_Osc;

/**
 * Convert floating point phase value (0.0 = 0 deg., 1.0 = 360 deg.)
 * to 32-bit unsigned int, as used by oscillator.
 */
#define SAU_Osc_PHASE(p) ((uint32_t) lrintf((p) * (float) UINT32_MAX))

/**
 * Calculate the coefficent, based on the sample rate,
 * used to give the per-sample phase increment
 * by multiplying with the frequency used.
 */
#define SAU_Osc_COEFF(srate) (((float) UINT32_MAX)/(srate))

/**
 * Initialize instance for use.
 */
static inline void SAU_init_Osc(SAU_Osc *restrict o, uint32_t srate) {
	*o = (SAU_Osc){
#if USE_PILUT
		.phase = SAU_Wave_picoeffs[SAU_WAVE_SIN].phase_adj,
#else
		.phase = 0,
#endif
		.coeff = SAU_Osc_COEFF(srate),
		.wave = SAU_WAVE_SIN,
		.flags = SAU_OSC_RESET,
	};
}

static inline void SAU_Osc_set_phase(SAU_Osc *restrict o, uint32_t phase) {
#if USE_PILUT
	o->phase = phase + SAU_Wave_picoeffs[o->wave].phase_adj;
#else
	o->phase = phase;
#endif
}

static inline void SAU_Osc_set_wave(SAU_Osc *restrict o, uint8_t wave) {
#if USE_PILUT
	int32_t old_offset = SAU_Wave_picoeffs[o->wave].phase_adj;
	int32_t offset = SAU_Wave_picoeffs[wave].phase_adj;
	o->phase += offset - old_offset;
	o->wave = wave;
	o->flags |= SAU_OSC_RESET_DIFF;
#else
	o->wave = wave;
#endif
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t SAU_Osc_cycle_len(SAU_Osc *restrict o, float freq) {
	return lrintf(((float) UINT32_MAX) / (o->coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t SAU_Osc_cycle_pos(SAU_Osc *restrict o,
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
static inline int32_t SAU_Osc_cycle_offs(SAU_Osc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - SAU_Wave_SLEN) / inc;
}

void SAU_Osc_run(SAU_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f);
void SAU_Osc_run_env(SAU_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f);
