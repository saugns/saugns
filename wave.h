/* sgensys: Wave module.
 * Copyright (c) 2011-2012, 2017-2021 Joel K. Pettersson
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
#include "math.h"

/* table length in sample values */
#define SGS_Wave_LENBITS 11
#define SGS_Wave_LEN     (1<<SGS_Wave_LENBITS) /* 2048 */
#define SGS_Wave_LENMASK (SGS_Wave_LEN - 1)

/* sample amplitude range */
#define SGS_Wave_MAXVAL 1.f
#define SGS_Wave_MINVAL (-SGS_Wave_MAXVAL)

/* sample length in integer phase */
#define SGS_Wave_SLENBITS (32-SGS_Wave_LENBITS)
#define SGS_Wave_SLEN     (1<<SGS_Wave_SLENBITS)
#define SGS_Wave_SLENMASK (SGS_Wave_SLEN - 1)

/**
 * Wave types.
 */
enum {
	SGS_WAVE_SIN = 0,
	SGS_WAVE_SQR,
	SGS_WAVE_TRI,
	SGS_WAVE_SAW,
	SGS_WAVE_AHS,
	SGS_WAVE_HRS,
	SGS_WAVE_SRS,
	SGS_WAVE_SSR,
	SGS_WAVE_TYPES
};

/** LUTs for wave types. */
extern float *const SGS_Wave_luts[SGS_WAVE_TYPES];

/** Pre-integrated LUTs for wave types. */
extern float *const SGS_Wave_piluts[SGS_WAVE_TYPES];

/** Information about or for use with a wave type. */
struct SGS_WaveCoeffs {
	float amp_scale;
	float amp_dc;
	int32_t phase_adj;
};

/** Extra values for use with PILUTs. */
extern const struct SGS_WaveCoeffs SGS_Wave_picoeffs[SGS_WAVE_TYPES];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const SGS_Wave_names[SGS_WAVE_TYPES + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define SGS_Wave_INDEX(phase) \
	(((uint32_t)(phase)) >> SGS_Wave_SLENBITS)

/**
 * Get LUT value for 32-bit unsigned phase using linear interpolation.
 *
 * \return sample
 */
static inline double SGS_Wave_get_lerp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = SGS_Wave_INDEX(phase);
	float s0 = lut[ind];
	float s1 = lut[(ind + 1) & SGS_Wave_LENMASK];
	double x = ((phase & SGS_Wave_SLENMASK) * (1.f / SGS_Wave_SLEN));
	return s0 + (s1 - s0) * x;
}

/** Get scale constant to differentiate values in a pre-integrated table. */
#define SGS_Wave_DVSCALE(wave) \
	(SGS_Wave_picoeffs[wave].amp_scale * 0.125f * (float) UINT32_MAX)

/** Get offset constant to apply to result from using a pre-integrated table. */
#define SGS_Wave_DVOFFSET(wave) \
	(SGS_Wave_picoeffs[wave].amp_dc)

/**
 * Get sinf() value for 32-bit integer phase using polynomial approximation.
 *
 * \return sample
 */
static inline float SGS_Wave_get_sinf(int32_t phase) {
	if (phase > (1<<30) || phase < -(1<<30))
		phase = INT32_MIN - phase;
	float x = phase * (float) SGS_PI/INT32_MAX;
	//float x = phase * (float) 1.f/(INT32_MAX*1.0);
	float tmp = 0.f;
	//f (x <= 1.f)
	//	tmp = SGS_sintilt_r1(x);
	tmp = SGS_sinf_t7(x);
	tmp = SGS_quadsat_r1(tmp, -2.f/16);
	return tmp;
}

void SGS_global_init_Wave(void);

void SGS_Wave_print(uint8_t id);
