/* SAU library: Noise generator implementation.
 * Copyright (c) 2022-2024 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "../program.h"

// kept here, since there's nothing else to place in a separate noise module
const char *const sauNoise_names[SAU_NOISE_NAMED + 1] = {
        SAU_NOISE__ITEMS(SAU_NOISE__X_NAME)
        NULL
};

typedef struct sauNoiseG {
	uint32_t n;
	uint32_t prev;
	uint8_t type;
} sauNoiseG;

static inline void sauNoiseG_set_seed(sauNoiseG *restrict o, uint32_t seed) {
	o->n = seed;
}

static inline void sauNoiseG_set_noise(sauNoiseG *restrict o, uint8_t noise) {
	o->type = noise;
	o->prev = 0; // reset to middle value
}

typedef void (*sauNoiseG_run_f)(sauNoiseG *restrict o,
		float *restrict buf, size_t len);

static sauMaybeUnused void sauNoiseG_run_wh(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	const float scale = 0x1p-31;
	for (size_t i = 0; i < len; ++i) {
		uint32_t s = sau_ranfast32(o->n++);
		buf[i] = sau_fscalei(s, scale);
	}
}

/*
 * Approximation of a symmetric, highly soft-saturated modification
 * of expression "sqrtf(-2.f * logf(x + 0.5f))"; more precisely, of
 * "x > 0 ?
 *  sqrtf(-2.f * logf(x + 0.5f)) :
 *  (2.f*sqrtf(-2.f * logf(0.5f)) - sqrtf(-2.f * logf(0.5f - x)))".
 *
 * Unlike the original expression, the result is also downscaled by
 * "2.f*sqrtf(-2.f * logf(0.5f))" (otherwise the maximum value), to
 * the range of 0.0 to 1.0 inclusive.
 */
static inline float soft_sqrtm2logp1_2_r01(float x) {
	const float scale[] = {
		-0.80270565422983103084,
		+5.52274428214641442648,
		-138.87126103150588693697,
	};
	float x2 = x*x;
	float x4 = x2*x2;
	return 0.5f + x*(scale[0] + x4*(scale[1] + x4*scale[2]));
}

/*
 * Function used to distort initial soft-saurated curve
 * so as to make it look and sound approximately right.
 * (Graphed, looks like half a bell curve on its side.)
 */
static inline float ssgauss_dist4(float x) {
	float x2 = x*x;
	float gx = (x + x2)*0.5f;
	return x*(1 - gx*(1 - x2));
}

/**
 * Random access soft-saturated Gaussian noise, using approximation.
 *
 * Described at: https://joelkp.frama.io/blog/ran-softsat-gauss.html
 *
 * \return pseudo-random number between -1.0 and +1.0 for index \p n
 */
static inline float sau_franssgauss32(uint32_t n) {
	int32_t s0 = sau_ranfast32(n);
	int32_t s1 = sau_mcg32(s0);
	float a = s0 * 0x1p-32;
	float b = s1 * 0x1p-32;
	float c = ssgauss_dist4(soft_sqrtm2logp1_2_r01(a));
	b = c * sau_sinpi_d5f(b); // simplified for only sin(), no cos() output
	return b;
}

/*
 * Uses soft-saturated Gaussian function.
 *
 * Would be more effiicent to retain both outputs rather than just one.
 */
static sauMaybeUnused void sauNoiseG_run_gw(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		buf[i] = sau_franssgauss32(o->n++);
	}
}

static sauMaybeUnused void sauNoiseG_run_bw(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		uint32_t n = o->n++;
		int32_t s = sau_sar32(sau_ranfast32(n), 31) * 2 + 1;
		buf[i] = s;
	}
}

static sauMaybeUnused void sauNoiseG_run_tw(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		uint32_t n = o->n++;
		int32_t s = sau_sar32(sau_ranfast32(n), 31) * 2 + 1;
		buf[i] = (n & 1) ? s : 0.f;
	}
}

/*
 * Brown noise implementation which allows wrap-around, removing the
 * discontinuities with wavefolding. Wavwfolding blends in with 6 dB
 * per octave roll-off. This makes the signal maximally bassy at the
 * very-low frequency end. As loud as a DC-blocker version would be.
 */
static sauMaybeUnused void sauNoiseG_run_re(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	const float scale = 0x1p-31;
	uint32_t sum = o->prev;
	for (size_t i = 0; i < len; ++i) {
		int32_t s = sau_ranfast32(o->n++);
		sum += (s >> 6); // 5 alternatively makes a louder version
		s = sau_foldhd32(sum);
		buf[i] = sau_fscalei(s, scale);
	}
	o->prev = sum;
}

static sauMaybeUnused void sauNoiseG_run_vi(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	const float scale = 0x1p-31;
	uint32_t s0 = o->prev;
	for (size_t i = 0; i < len; ++i) {
		uint32_t s1 = sau_ranfast32(o->n++);
		buf[i] = sau_fscalei((s1 / 2) - (s0 / 2), scale);
		s0 = s1;
	}
	o->prev = s0;
}

static sauMaybeUnused void sauNoiseG_run_bv(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	int32_t s0 = o->prev;
	for (size_t i = 0; i < len; ++i) {
		uint32_t n = o->n++;
		int32_t s1 = sau_sar32(sau_ranfast32(n), 31);
		s1 = (n & 1) ? (s1 * 2 + 1) : 0;
		buf[i] = (s1 - s0);
		s0 = s1;
	}
	o->prev = s0;
}

#define SAU_NOISE__X_CASE(NAME) \
	case SAU_NOISE_N_##NAME: run = sauNoiseG_run_##NAME; break;

static sauMaybeUnused void sauNoiseG_run(sauNoiseG *restrict o,
		float *restrict buf, size_t len) {
	sauNoiseG_run_f run;
	switch (o->type) {
	default: /* fall-through */
	SAU_NOISE__ITEMS(SAU_NOISE__X_CASE)
	}
	run(o, buf, len);
}
