/* SAU library: Math definitions.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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

#define SAU_PI          3.14159265358979323846
#define SAU_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define SAU_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define SAU_HUMMID    632.45553203367586639978 // human hearing range geom.mean
#define SAU_GLDA        2.39996322972865332223 // golden angle 2*PI*(2.0 - phi)
#define SAU_GLDA_1_2PI  0.38196601125010515180 // (in cycle %) 2.0 - phi
#define SAU_FIBH32      0x9e3779b9             // 32-bit Fibonacci hash constant
#define SAU_FIBH64      0x9e3779b97f4a7c15     // 64-bit Fibonacci hash constant

/*
 * Format conversion & general purpose functions.
 */

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint64_t sau_ms_in_samples(uint64_t time_ms, uint64_t srate,
		int *restrict carry) {
	uint64_t time = time_ms * srate;
	if (carry) {
		int64_t error;
		time += *carry;
		error = time % 1000;
		*carry = error;
	}
	time /= 1000;
	return time;
}

/** Apply lrint() if long can hold UINT32_MAX, otherwise llrint(). */
#define sau_ui32rint(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrint(x) : llrint(x)))

/** Apply lrintf() if long can hold UINT32_MAX, otherwise llrintf(). */
#define sau_ui32rintf(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrintf(x) : llrintf(x)))

/**
 * Convert cyclical value (0.0 = 0% and 1.0 = 100%, with ends
 * wrapping around) to 32-bit integer with 0 as the 0% value.
 */
static inline uint32_t sau_cyclepos_dtoui32(double x) {
	// needs long(er) range because 0.5 from remainder becomes INT32_MAX+1
	return sau_ui32rint(remainder(x, 1.f) * (float)UINT32_MAX);
}

/**
 * Convert an unsigned 64-bit integer to 0.0 to 1.0 value.
 */
static inline double sau_d01_from_ui64(uint64_t x) {
	return (x >> 11) * 0x1.0p-53;
}

/** \return +1 if \p n is even, -1 if it's odd. */
static inline int sau_oddness_as_sign(int n) {
	return (1 - ((n & 1) * 2));
}

/** Portable 32-bit arithmetic right shift. */
static inline int32_t sau_sar32(int32_t x, int s) {
	return x < 0 ? ~(~x >> s) : x >> s;
}

/** 32-bit right rotation. */
static inline uint32_t sau_ror32(uint32_t x, int r) {
	return x >> r | x << (32 - r);
}

/** Multiplicatively mix bits using varying right-rotation,
    for 32-bit unsigned \p x value, \p r rotation, \p ro rotation offset. */
static inline uint32_t sau_muvaror32(uint32_t x, int r, int ro) {
	return (x | (1U << (ro & 31)) | 1U) * sau_ror32(x, r + ro);
}

/*
 * Math functions for use in SAU scripts.
 */

/**
 * Metallic value function. Golden ratio for \p x == 1, silver for x == 2, etc.
 * Also accepts zero (with the result one), and values in-between the integers.
 * (Maps negative infinity to 0.0, 0.0 to 1.0, and positive infinity to itself.
 * Negative values give how much the positive value would have been increased.)
 *
 * \return metallic value
 */
static inline double sau_met(double x) {
	return 0.5f * (x + sqrt(x * x + 4.f));
}

/* Macro used to declare and define math symbol sets of items. */
#define SAU_MATH__ITEMS(X) \
	X(abs,       VAL_F, {.val = fabs}) \
	X(cos,       VAL_F, {.val = cos}) \
	X(exp,       VAL_F, {.val = exp}) \
	X(log,       VAL_F, {.val = log}) \
	X(met,       VAL_F, {.val = sau_met}) \
	X(mf,      NOARG_F, {.noarg = mf_const}) \
	X(pi,      NOARG_F, {.noarg = pi_const}) \
	X(rand,    STATE_F, {.state = sau_rand}) \
	X(rint,      VAL_F, {.val = rint}) \
	X(seed, STATEVAL_F, {.stateval = sau_seed}) \
	X(sin,       VAL_F, {.val = sin}) \
	X(sqrt,      VAL_F, {.val = sqrt}) \
	X(time,    STATE_F, {.state = sau_time}) \
	//
#define SAU_MATH__X_ID(NAME, PARAMS, SYM_F) SAU_MATH_N_##NAME,
#define SAU_MATH__X_NAME(NAME, PARAMS, SYM_F) #NAME,
#define SAU_MATH__X_PARAMS(NAME, PARAMS, SYM_F) SAU_MATH_##PARAMS,
#define SAU_MATH__X_SYM_F(NAME, PARAMS, SYM_F) SYM_F,

/**
 * Math symbol ids for functions and named constants.
 */
enum {
	SAU_MATH__ITEMS(SAU_MATH__X_ID)
	SAU_MATH_NAMED
};

struct sauMath_state;

/** State for math functions for each parsing and interpretation unit. */
struct sauMath_state {
	uint64_t seed64;
	uint32_t seed32;
	bool no_time;
};

/** Math function parameter type values. */
enum {
	SAU_MATH_VAL_F = 0,
	SAU_MATH_STATE_F,
	SAU_MATH_STATEVAL_F,
	SAU_MATH_NOARG_F
};
/** Math function pointer types. */
union sauMath_sym_f {
	double (*val)(double x);
	double (*state)(struct sauMath_state *o);
	double (*stateval)(struct sauMath_state *o, double x);
	double (*noarg)(void);
};

/** Names of math functions, with an extra NULL pointer at the end. */
extern const char *const sauMath_names[SAU_MATH_NAMED + 1];

/** Parameter types for math functions. */
extern const uint8_t sauMath_params[SAU_MATH_NAMED];

/** Function addresses for math functions. */
extern const union sauMath_sym_f sauMath_symbols[SAU_MATH_NAMED];

/*
 * Simple PRNGs.
 */

/**
 * Random access noise, minimal lower-quality version. Chaotic waveshaper which
 * turns sawtooth-ish number sequences into white noise. Returns zero for zero.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline int32_t sau_ranoise32(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
	s = sau_muvaror32(s, s >> 27, 0);
	return s;
}

/**
 * Random access noise, minimal lower-quality version. Chaotic waveshaper which
 * turns sawtooth-ish number sequences into white noise. Returns zero for zero.
 *
 * This "next" function returns a new value each time, corresponding to a state
 * \p pos, which is increased. It may be initialized with any seed (0 is fine).
 *
 * \return pseudo-random number for state \p pos
 */
static inline int32_t sau_ranoise32_next(uint32_t *restrict pos) {
	uint32_t s = *pos += SAU_FIBH32;
	s = sau_muvaror32(s, s >> 27, 0);
	return s;
}

/**
 * Random access noise, smoother more LCG-ish version. Chaotic waveshaper which
 * turns sawtooth-ish number sequences into white noise. Returns zero for zero.
 * Lower bits have very poor-quality randomness, but the whole sounds 'smooth'.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline int32_t sau_ransmooth32(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
	s *= sau_ror32(s, s >> 27);
	return s;
}

/**
 * Random access noise, smoother more LCG-ish version. Chaotic waveshaper which
 * turns sawtooth-ish number sequences into white noise. Returns zero for zero.
 * Lower bits have very poor-quality randomness, but the whole sounds 'smooth'.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline int32_t sau_ransmooth32_next(uint32_t *restrict pos) {
	uint32_t s = *pos += SAU_FIBH32;
	s *= sau_ror32(s, s >> 27);
	return s;
}

/**
 * Random access noise, simpler binary output version. Chaotic waveshaper which
 * turns sawtooth-ish number sequences into white noise. Returns zero for zero.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random 1 or 0 for index \p n
 */
static inline bool sau_ranbit32(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
	s *= sau_ror32(s, s >> 27);
	return ((int32_t)s) < 0;
}

/**
 * Random access noise, simpler binary output version. Chaotic waveshaper which
 * turns sawtooth-ish number sequences into white noise. Returns zero for zero.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random 1 or 0 for index \p n
 */
static inline bool sau_ranbit32_next(uint32_t *restrict pos) {
	uint32_t s = *pos += SAU_FIBH32;
	s *= sau_ror32(s, s >> 27);
	return ((int32_t)s) < 0;
}

/** Initial seed for mgs_xorshift32(). Other non-zero values can be used. */
#define SAU_XORSHIFT32_SEED 2463534242UL

/**
 * Get Marsaglia xorshift32 state from non-zero \p seed.
 */
static inline uint32_t sau_xorshift32(uint32_t seed) {
	uint32_t x = seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/**
 * Fixed-increment SplitMix64, based on C version provided by Vigna.
 *
 * \return next pseudo-random number for state \p pos
 */
static inline uint64_t sau_splitmix64_next(uint64_t *restrict pos) {
	uint64_t z = (*pos += SAU_FIBH64);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

/**
 * Standard 32-bit PRNG for use together with math functions for SAU scripts.
 */
static inline uint32_t sau_rand32(struct sauMath_state *restrict o) {
	if (o->seed32 == 0) o->seed32 = SAU_XORSHIFT32_SEED;
	return (o->seed32 = sau_xorshift32(o->seed32));
}

/*
 * Fast approximations.
 */

/**
 * Degree 5 sin(PI * x) approximation function for limited input range.
 *
 * For \p x domain -0.5 <= x <= +0.5; use with pre-wrapped values only.
 *
 * Almost clean spectrum, adds a 5th harmonic at slightly below -84 dB.
 */
static inline float sau_sinpi_d5f(float x) {
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
