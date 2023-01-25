/* SAU library: Wave module.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
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
#include "common.h"

/* table length in sample values */
#define sauWave_LENBITS 11
#define sauWave_LEN     (1<<sauWave_LENBITS) /* 2048 */
#define sauWave_LENMASK (sauWave_LEN - 1)

/* sample amplitude range */
#define sauWave_MAXVAL 1.f
#define sauWave_MINVAL (-sauWave_MAXVAL)

/* sample length in integer phase */
#define sauWave_SLENBITS (32-sauWave_LENBITS)
#define sauWave_SLEN     (1<<sauWave_SLENBITS)
#define sauWave_SLENMASK (sauWave_SLEN - 1)

/* Macro used to declare and define wave type sets of items.
   Note that the extra "PILUT" data isn't all fit into this. */
#define SAU_WAVE__ITEMS(X) \
	X(sin) \
	X(sqr) \
	X(tri) \
	X(saw) \
	X(ahs) \
	X(hrs) \
	X(srs) \
	X(ssr) \
	//
#define SAU_WAVE__X_ID(NAME) SAU_WAVE_N_##NAME,
#define SAU_WAVE__X_NAME(NAME) #NAME,

/**
 * Wave types.
 */
enum {
	SAU_WAVE__ITEMS(SAU_WAVE__X_ID)
	SAU_WAVE_NAMED
};

/** LUTs for wave types. */
extern float *const sauWave_luts[SAU_WAVE_NAMED];

/** Pre-integrated LUTs for wave types. */
extern float *const sauWave_piluts[SAU_WAVE_NAMED];

/** Information about or for use with a wave type. */
struct sauWaveCoeffs {
	float amp_scale;
	float amp_dc;
	int32_t phase_adj;
};

/** Extra values for use with PILUTs. */
extern const struct sauWaveCoeffs sauWave_picoeffs[SAU_WAVE_NAMED];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const sauWave_names[SAU_WAVE_NAMED + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define sauWave_INDEX(phase) \
	(((uint32_t)(phase)) >> sauWave_SLENBITS)

/**
 * Get LUT value for 32-bit unsigned phase using linear interpolation.
 *
 * \return sample
 */
static inline double sauWave_get_lerp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = sauWave_INDEX(phase);
	float s0 = lut[ind];
	float s1 = lut[(ind + 1) & sauWave_LENMASK];
	double x = ((phase & sauWave_SLENMASK) * (1.f / sauWave_SLEN));
	return s0 + (s1 - s0) * x;
}

/**
 * Get LUT value for 32-bit unsigned phase using Hermite interpolation.
 *
 * \return sample
 */
static inline double sauWave_get_herp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = sauWave_INDEX(phase);
	float s0 = lut[(ind - 1) & sauWave_LENMASK];
	float s1 = lut[ind];
	float s2 = lut[(ind + 1) & sauWave_LENMASK];
	float s3 = lut[(ind + 2) & sauWave_LENMASK];
	double x = ((phase & sauWave_SLENMASK) * (1.f / sauWave_SLEN));
	// 4-point, 3rd-order Hermite (x-form)
	double c0 = s1;
	double c1 = 1/2.0*(s2-s0);
	double c2 = s0 - 5/2.0*s1 + 2*s2 - 1/2.0*s3;
	double c3 = 1/2.0*(s3-s0) + 3/2.0*(s1-s2);
	return ((c3*x+c2)*x+c1)*x+c0;
}

/** Get scale constant to differentiate values in a pre-integrated table. */
#define sauWave_DVSCALE(wave) \
	(sauWave_picoeffs[wave].amp_scale * 0.125f * (float) UINT32_MAX)

/** Get offset constant to apply to result from using a pre-integrated table. */
#define sauWave_DVOFFSET(wave) \
	(sauWave_picoeffs[wave].amp_dc)

void sau_global_init_Wave(void);

void sauWave_print(uint8_t id);
