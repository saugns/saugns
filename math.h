/* mgensys: Math header.
 * Copyright (c) 2011, 2020-2023 Joel K. Pettersson
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
#include <math.h>
#include <limits.h>

#define MGS_PI          3.14159265358979323846
#define MGS_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define MGS_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define MGS_HUMMID    632.45553203367586639978 // human hearing range geom.mean
#define MGS_FIBH32      2654435769UL           // 32-bit Fibonacci hash constant

/*
 * Format conversions
 */

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint64_t mgs_ms_in_samples(uint64_t time_ms, uint64_t srate) {
	uint64_t time = time_ms * srate;
	time = (time + 500) / 1000;
	return time;
}

/** Apply lrint() if long can hold UINT32_MAX, otherwise llrint(). */
#define mgs_ui32rint(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrint(x) : llrint(x)))

/** Apply lrintf() if long can hold UINT32_MAX, otherwise llrintf(). */
#define mgs_ui32rintf(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrintf(x) : llrintf(x)))

/**
 * Convert cyclical value (0.0 = 0% and 1.0 = 100%, with ends
 * wrapping around) to 32-bit integer with 0 as the 0% value.
 */
static inline uint32_t mgs_cyclepos_dtoui32(double x) {
	// needs long(er) range because 0.5 from remainder becomes INT32_MAX+1
	return mgs_ui32rint(remainder(x, 1.f) * (float)UINT32_MAX);
}

/** \return +1 if \p n is even, -1 if it's odd. */
static inline int mgs_oddness_as_sign(int n) {
	return (1 - ((n & 1) * 2));
}

/** Portable 32-bit arithmetic right shift. */
static inline int32_t mgs_sar32(int32_t x, int s) {
	return x < 0 ? ~(~x >> s) : x >> s;
}

/** 32-bit right rotation. */
static inline uint32_t mgs_ror32(uint32_t x, int r) {
	return x >> r | x << (32 - r);
}

/**
 * Degree 5 sin(PI * x) approximation function for limited input range.
 *
 * For \p x domain -0.5 <= x <= +0.5; use with pre-wrapped values only.
 *
 * Almost clean spectrum, adds a 5th harmonic at slightly below -84 dB.
 */
static inline float mgs_sinpi_d5f(float x) {
	/*
	 * Coefficients generated for no end-point error,
	 * on top of minimax, roughly doubling the error.
	 * Slightly lower max error than Taylor degree 7.
	 */
	const float scale[] = {
		+3.14042741234069229463,
		-5.13655757476162831091,
		+2.29939170159543653372,
	};
	float x2 = x*x;
	return x*(scale[0] + x2*(scale[1] + x2*scale[2]));
}

/*
 * Simple PRNGs
 */

/**
 * Random access noise, fast version with bitshifts but no bitrotation. Chaotic
 * waveshaper, which turns e.g. sawtooth-ish number sequences into white noise.
 * Lower bits have lower quality; use SplitMix32 if those need to be good, too.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline uint32_t mgs_ranfast32(uint32_t n) {
	uint32_t s = n * MGS_FIBH32;
	s ^= s >> 14;
	s = (s | 1) * s;
	s ^= s >> 13;
	return s;
}

/**
 * Random access noise, fast version with bitshifts but no bitrotation. Chaotic
 * waveshaper, which turns e.g. sawtooth-ish number sequences into white noise.
 * Lower bits have lower quality; use SplitMix32 if those need to be good, too.
 *
 * This "next" function returns a new value each time, corresponding to a state
 * \p pos, which is increased. It may be initialized with any seed (0 is fine).
 *
 * \return next pseudo-random number for state \p pos
 */
static inline uint32_t mgs_ranfast32_next(uint32_t *restrict pos) {
	uint32_t s = *pos += MGS_FIBH32;
	s ^= s >> 14;
	s = (s | 1) * s;
	s ^= s >> 13;
	return s;
}

/**
 * A random access SplitMix32 variant, using an alternative function
 * by TheIronBorn & Christopher Wellons's "Hash Prospector" project.
 *
 * \return pseudo-random number for index \p n
 */
static inline uint32_t mgs_splitmix32(uint32_t n) {
	uint32_t z = (n * 0x9e3779b9);
	z = (z ^ (z >> 16)) * 0x21f0aaad;
	z = (z ^ (z >> 15)) * 0xf35a2d97; /* similar alt. 0x735a2d97 */
	return z ^ (z >> 15);
}

/**
 * Fixed-increment SplitMix32 variant, using an alternative function
 * by TheIronBorn & Christopher Wellons's "Hash Prospector" project.
 *
 * \return next pseudo-random number for state \p pos
 */
static inline uint32_t mgs_splitmix32_next(uint32_t *restrict pos) {
	uint32_t z = (*pos += 0x9e3779b9);
	z = (z ^ (z >> 16)) * 0x21f0aaad;
	z = (z ^ (z >> 15)) * 0xf35a2d97; /* similar alt. 0x735a2d97 */
	return z ^ (z >> 15);
}

/**
 * 32-bit MCG. Usable together with another PRNG, for additional values
 * extended in a perpendicular sequence in a computationally cheap way.
 */
static inline uint32_t mgs_mcg32(uint32_t seed) {
	return seed * 0xe47135; /* alt. 0x93d765dd; both Steele & Vigna 2021 */
}

/** Initial seed for mgs_xorshift32(). Other non-zero values can be used. */
#define MGS_XORSHIFT32_SEED 2463534242UL

/**
 * Get Marsaglia xorshift32 state from non-zero \p seed.
 */
static inline uint32_t mgs_xorshift32(uint32_t seed) {
	uint32_t x = seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}
