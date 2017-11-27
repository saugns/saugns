/* sgensys: Oscillator module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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
#include "math.h"

#define SGSOsc_LUT_INDEXBITS 11
#define SGSOsc_LUT_INDEXMASK (SGSOsc_LUT_LEN - 1)
#define SGSOsc_LUT_LEN       (1<<SGSOsc_LUT_INDEXBITS) /* 2048 */

#define SGSOsc_LUT_MAXVAL ((1<<15) - 1)
#define SGSOsc_LUT_MINVAL (-SGSOsc_LUT_MAXVAL)

#define SGSOsc_PHASE_LERPSCALE (1<<(32-SGSOsc_LUT_INDEXBITS))
#define SGSOsc_PHASE_LERPMASK  (SGSOsc_PHASE_LERPSCALE - 1)

/**
 * Oscillator wave types.
 */
enum {
	SGS_WAVE_SIN = 0,
	SGS_WAVE_SRS,
	SGS_WAVE_TRI,
	SGS_WAVE_SQR,
	SGS_WAVE_SAW,
	SGS_WAVE_TYPES
};

/** Oscillator LUTs, indexed by wave type value. */
extern int16_t SGSOsc_luts[SGS_WAVE_TYPES][SGSOsc_LUT_LEN];

/**
 * Oscillator data. Only includes phase, as most parameters are
 * either common to several instances or may differ each sample.
 */
typedef struct SGSOsc {
	uint32_t phase;
} SGSOsc;

/**
 * Get phase as 32-bit unsigned int value.
 */
#define SGSOsc_GET_PHASE(o) ((uint32_t) (o)->phase)

/**
 * Set phase as 32-bit unsigned int value.
 */
#define SGSOsc_SET_PHASE(o, p) ((void) ((o)->phase = (p)))

/**
 * Convert floating point (0.0 to 1.0) phase value to 32-bit unsigned int,
 * as used by oscillator.
 */
#define SGSOsc_PHASE(p) ((uint32_t) lrint((p) * 4294967296.0))

/**
 * Calculate the sample rate-dependent coefficent multiplied
 * by the frequency to give the phase increment.
 */
#define SGSOsc_SRATE_COEFF(srate) ((double) 4294967296.0/(srate))

/**
 * Calculate the number of samples in a wave cycle.
 *
 * Can be used to adjust timing.
 */
#define SGSOsc_CYCLE_LEN(coeff, freq) \
	((uint32_t) lrint(4294967296.0 / ((coeff)*(freq))))

/**
 * Calculate the number of samples from the beginning of the
 * current wave cycle, using the current sample position.
 *
 * Can be used to adjust timing.
 */
#define SGSOsc_CYCLE_POS(coeff, freq, spos, cpos_out) do{ \
	uint32_t SGSOsc__inc = lrint((coeff)*(freq)); \
	uint32_t SGSOsc__phs = SGSOsc__inc * (uint32_t)(spos); \
	(cpos_out) = SGSOsc__phs / SGSOsc__inc; \
}while(0)

/**
 * Produce 16-bit integer output.
 */
#define SGSOsc_RUN_S16(o, lut, coeff, freq, pm_s16, amp, s16_out) do{ \
	uint32_t SGSOsc__phs = (o)->phase + ((pm_s16) << 16); \
	uint32_t SGSOsc__ind = SGSOsc__phs >> (32-SGSOsc_LUT_INDEXBITS); \
	int32_t SGSOsc__s16 = (lut)[SGSOsc__ind]; \
	/* write lerp'd & scaled result */ \
	SGSOsc__s16 = lrintf( \
		(((float)SGSOsc__s16) + \
		 ((float)((lut)[(SGSOsc__ind + 1) & SGSOsc_LUT_INDEXMASK] - \
		          SGSOsc__s16)) * \
		 (((float)(SGSOsc__phs & SGSOsc_PHASE_LERPMASK)) * \
		  (1.f / SGSOsc_PHASE_LERPSCALE))) * \
		(amp)); \
	(s16_out) = SGSOsc__s16; \
	/* update phase */ \
	uint32_t SGSOsc__inc = lrint((coeff)*(freq)); \
	(o)->phase += SGSOsc__inc; \
}while(0)

/**
 * Produce floating point output in the 0.0 to 1.0 range.
 */
#define SGSOsc_RUN_SF(o, lut, coeff, freq, pm_s16, sf_out) do{ \
	uint32_t SGSOsc__phs = (o)->phase + ((pm_s16) << 16); \
	uint32_t SGSOsc__ind = SGSOsc__phs >> (32-SGSOsc_LUT_INDEXBITS); \
	int32_t SGSOsc__s16 = (lut)[SGSOsc__ind]; \
	/* write lerp'd & scaled result */ \
	(sf_out) = ((float)(SGSOsc__s16) + \
		    ((float)((lut)[(SGSOsc__ind + 1) & SGSOsc_LUT_INDEXMASK] - \
		             SGSOsc__s16)) * \
		    (((float)(SGSOsc__phs & SGSOsc_PHASE_LERPMASK)) * \
		     (1.f / SGSOsc_PHASE_LERPSCALE))) * \
	           (1.f / ((float) SGSOsc_LUT_MAXVAL * 2)) + \
	           .5f; \
	/* update phase */ \
	uint32_t SGSOsc__inc = lrint((coeff)*(freq)); \
	(o)->phase += SGSOsc__inc; \
}while(0)

extern void SGSOsc_global_init(void);
