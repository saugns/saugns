/* saugns: Oscillator module.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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
#include "../program/wave.h"
#include "../math.h"

/**
 * Oscillator data. Only includes phase, as most parameters are
 * either common to several instances or may differ each sample.
 */
typedef struct SAU_Osc {
	uint32_t phase;
} SAU_Osc;

/**
 * Get phase as 32-bit unsigned int value.
 */
#define SAU_Osc_GET_PHASE(o) ((uint32_t) (o)->phase)

/**
 * Set phase as 32-bit unsigned int value.
 */
#define SAU_Osc_SET_PHASE(o, p) ((void) ((o)->phase = (p)))

/**
 * Convert floating point phase value (0.0 = 0 deg., 1.0 = 360 deg.)
 * to 32-bit unsigned int, as used by oscillator.
 */
#define SAU_Osc_PHASE(p) ((uint32_t) lrint((p) * 4294967296.0))

/**
 * Calculate the sample rate-dependent coefficent multiplied
 * by the frequency to give the per-sample phase increment.
 */
#define SAU_Osc_SRATE_COEFF(srate) ((double) 4294967296.0/(srate))

/**
 * Calculate the number of samples in a wave cycle.
 *
 * Can be used to adjust timing.
 */
#define SAU_Osc_CYCLE_LEN(coeff, freq) \
	((uint32_t) lrint(4294967296.0 / ((coeff)*(freq))))

/**
 * Calculate the number of samples from the beginning of the
 * current wave cycle, using the current sample position.
 *
 * Can be used to adjust timing.
 */
#define SAU_Osc_CYCLE_POS(coeff, freq, spos, cpos_out) do{ \
	uint32_t SAU_Osc__inc = lrint((coeff)*(freq)); \
	uint32_t SAU_Osc__phs = SAU_Osc__inc * (uint32_t)(spos); \
	(cpos_out) = SAU_Osc__phs / SAU_Osc__inc; \
}while(0)

/**
 * Produce floating point output in the -1.0 to 1.0 range.
 */
static inline float SAU_Osc_run(SAU_Osc *restrict o,
		const float *restrict lut, double coeff,
		float freq, int32_t pm_s32) {
	uint32_t phase = o->phase + pm_s32;
	float s = SAU_Wave_get_lerp(lut, phase);
	uint32_t phase_inc = lrint(coeff * freq);
	o->phase += phase_inc;
	return s;
}
