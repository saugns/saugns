/* SAU library: Wave module.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
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
   The PILUT coefficients affect use by the oscillator code. */
#define SAU_WAVE__ITEMS(X) \
	X(sin, (.amp_scale = 1.27324153848, \
		.amp_dc    = 0.0, \
		.phase_adj = (INT32_MIN/2))) \
	X(tri, (.amp_scale = 1.00097751711, \
		.amp_dc    = 0.0, \
		.phase_adj = 0)) \
	X(srs, (.amp_scale = 1.52547437578, \
		.amp_dc    = 0.0, \
		.phase_adj = 0)) \
	X(sqr, (.amp_scale = 2.00000000000, \
		.amp_dc    = 0.0, \
		.phase_adj = (INT32_MIN/2))) \
	X(ean, (.amp_scale = 1.20275515347, \
		.amp_dc    = -0.24257955076, \
		.phase_adj = 0)) \
	X(cat, (.amp_scale = 1.37070880305, \
		.amp_dc    = -0.23725526633, \
		.phase_adj = 0)) \
	X(eto, (.amp_scale = 1.26113986272 * -1 /* flip sign */, \
		.amp_dc    = 0.0, \
		.phase_adj = -(INT32_MIN/2))) \
	X(par, (.amp_scale = 1.02639326795, \
		.amp_dc    = -0.33333333333, \
		.phase_adj = 0)) \
	X(mto, (.amp_scale = 1.57268451738, \
		.amp_dc    = -0.23724704918, \
		.phase_adj = 0)) \
	X(saw, (.amp_scale = 1.00048851979 * -1 /* flip sign */, \
		.amp_dc    = 0.0, \
		.phase_adj = -(INT32_MIN/2))) \
	X(hsi, (.amp_scale = 1.40333871035, \
		.amp_dc    = -0.36334126990, \
		.phase_adj = 0)) \
	X(spa, (.amp_scale = 1.07213756312, \
		.amp_dc    = 0.27322393756, \
		.phase_adj = 0)) \
	//
#define SAU_WAVE__X_ID(NAME, COEFFS) SAU_WAVE_N_##NAME,
#define SAU_WAVE__X_NAME(NAME, COEFFS) #NAME,
#define SAU_WAVE__X_COEFFS(NAME, COEFFS) {SAU_ARGS COEFFS},

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

void sauWave_print(uint8_t id, bool verbose);
