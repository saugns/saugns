/* sgensys: Oscillator implementation.
 * Copyright (c) 2011, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
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
#define SGS_Phasor_COEFF(srate) (((float) UINT32_MAX)/(srate))

typedef struct SGS_Phasor {
	uint32_t phase;
	float coeff;
} SGS_Phasor;

void SGS_Phasor_fill(SGS_Phasor *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f);

#define SGS_OSC_RESET_DIFF  (1<<0)
#define SGS_OSC_RESET       ((1<<1) - 1)

typedef struct SGS_Osc {
	SGS_Phasor phasor;
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
		.phasor = (SGS_Phasor){
			.phase = SGS_Wave_picoeffs[SGS_WAVE_N_sin].phase_adj,
			.coeff = SGS_Phasor_COEFF(srate),
		},
#else
		.phasor = (SGS_Phasor){
			.phase = 0,
			.coeff = SGS_Phasor_COEFF(srate),
		},
#endif
		.wave = SGS_WAVE_N_sin,
		.flags = SGS_OSC_RESET,
	};
}

static inline void SGS_Osc_set_phase(SGS_Osc *restrict o, uint32_t phase) {
#if USE_PILUT
	o->phasor.phase = phase + SGS_Wave_picoeffs[o->wave].phase_adj;
#else
	o->phasor.phase = phase;
#endif
}

static inline void SGS_Osc_set_wave(SGS_Osc *restrict o, uint8_t wave) {
#if USE_PILUT
	int32_t old_offset = SGS_Wave_picoeffs[o->wave].phase_adj;
	int32_t offset = SGS_Wave_picoeffs[wave].phase_adj;
	o->phasor.phase += offset - old_offset;
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
	return lrintf(((float) UINT32_MAX) / (o->phasor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t SGS_Osc_cycle_pos(SGS_Osc *restrict o,
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
static inline int32_t SGS_Osc_cycle_offs(SGS_Osc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = lrintf(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - SGS_Wave_SLEN) / inc;
}

void SGS_Osc_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf);
