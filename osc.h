/* sgensys: oscillator module.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "math.h"

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

#define SGSOsc_LUT_LEN 1024
#define SGSOsc_LUT_INDEXBITS 10
#define SGSOsc_LUT_INDEXMASK ((1<<SGSOsc_LUT_INDEXBITS) - 1)
#define SGSOsc_LUT_MAX ((float)((1<<15) - 1))

#define SGSOsc_PHASE_LERPMASK ((1<<(32-SGSOsc_LUT_INDEXBITS)) - 1)

typedef int16_t SGSOscLUV; /* use const SGSOscLUV* as LUT pointer type */
typedef SGSOscLUV SGSOscLUT[SGSOsc_LUT_LEN];
extern SGSOscLUT SGSOsc_luts[SGS_WAVE_TYPES];

extern void SGSOsc_init_luts(void);

/**
 * Oscillator data. Only includes phase, as most parameters are
 * either common to several instances or may differ each sample.
 */
typedef struct SGSOsc {
	uint32_t phase;
} SGSOsc;

/**
 * Calculate sample rate-dependent coefficent. (This value is multiplied
 * by the frequency to give the phase increment for each sample.)
 */
#define SGSOsc_SR_COEFF(sr) \
	(4294967296.0/(sr))

/**
 * Convert floating point (0.0 to 1.0) phase value to 32-bit unsigned int,
 * as used by oscillator.
 */
#define SGSOsc_PHASE(p) \
	((uint32_t)((p) * 4294967296.0))

#define SGSOsc_GET_PHASE(o) \
	((o)->phase)

#define SGSOsc_SET_PHASE(o, p) \
	((void)((o)->phase = (p)))

/**
 * Produce 16-bit integer output.
 */
#define SGSOsc_RUN_S16(o, lut, sr_coeff, \
		freq, fm_s16, pm_s16, amp, s16_out) do{ \
	uint32_t SGSOsc__inc; \
	SET_I2FV(SGSOsc__inc, (sr_coeff)*(freq)); \
	SGSOsc__inc += (fm_s16) << 16; \
	uint32_t SGSOsc__phs = (o)->phase + ((pm_s16) << 16); \
	uint32_t SGSOsc__ind = SGSOsc__phs >> (32-SGSOsc_LUT_INDEXBITS); \
	int32_t SGSOsc__s16 = (lut)[SGSOsc__ind]; \
	/* write lerp'd & scaled result */ \
	SET_I2FV((s16_out), \
		 (((float)SGSOsc__s16) + \
		  ((float)((lut)[(SGSOsc__ind + 1) & SGSOsc_LUT_INDEXMASK] - \
		           SGSOsc__s16)) * \
		  ((float)(SGSOsc__phs & SGSOsc_PHASE_LERPMASK)) * \
		  (1.f / (1 << (32-SGSOsc_LUT_INDEXBITS)))) * \
		 (amp) \
	); \
	/* update phase */ \
	(o)->phase += SGSOsc__inc; \
}while(0)

/**
 * Produce floating point output in the 0.0 to 1.0 range.
 */
#define SGSOsc_RUN_SF(o, lut, sr_coeff, \
	       	freq, fm_s16, pm_s16, sf_out) do{ \
	uint32_t SGSOsc__inc; \
	SET_I2FV(SGSOsc__inc, (sr_coeff)*(freq)); \
	SGSOsc__inc += (fm_s16) << 16; \
	uint32_t SGSOsc__phs = (o)->phase + ((pm_s16) << 16); \
	uint32_t SGSOsc__ind = SGSOsc__phs >> (32-SGSOsc_LUT_INDEXBITS); \
	int32_t SGSOsc__s16 = (lut)[SGSOsc__ind]; \
	/* write lerp'd & scaled result */ \
	(sf_out) = (((float)SGSOsc__s16) + \
		    ((float)((lut)[(SGSOsc__ind + 1) & SGSOsc_LUT_INDEXMASK] - \
		             SGSOsc__s16)) * \
	            ((float)(SGSOsc__phs & SGSOsc_PHASE_LERPMASK)) * \
	            (1.f / (1 << (32-SGSOsc_LUT_INDEXBITS)))) * \
	           (1.f / (SGSOsc_LUT_MAX * 2)) + \
	           .5f; \
	/* update phase */ \
	(o)->phase += SGSOsc__inc; \
}while(0)

/**
 * Calculate the number of samples in a wave cycle.
 *
 * Can be used for timing adjustments (without accounting for
 * dynamic frequency).
 */
#define SGSOsc_CYCLE_SAMPLES(sr_coeff, freq, sc_out) do{ \
	uint32_t SGSOsc__inc; \
	SET_I2FV(SGSOsc__inc, (sr_coeff)*(freq)); \
	(sc_out) = (((uint32_t) -1) / SGSOsc__inc) + 1; \
}while(0)

/**
 * Calculate number of samples from the beginning of a wave cycle to
 * the given sample position.
 *
 * Can be used to adjust timing to wave cycle boundaries (without
 * accounting for dynamic frequency).
 */
#define SGSOsc_CYCLE_SAMPLE_OFFSET(sr_coeff, freq, spos, spos_out) do{ \
	uint32_t SGSOsc__inc; \
	SET_I2FV(SGSOsc__inc, (sr_coeff)*(freq)); \
	uint32_t SGSOsc__phs = SGSOsc__inc * (uint32_t)(spos); \
	(spos_out) = SGSOsc__phs / SGSOsc__inc; \
}while(0)
