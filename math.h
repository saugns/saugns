/* saugns: Math definitions.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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
#include "common.h"
#include <math.h>

#define SAU_PI          3.14159265358979323846
#define SAU_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define SAU_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define SAU_HUMMID    632.45553203367586639978 // human hearing range geom.mean
#define SAU_GLDA        2.39996322972865332223 // golden angle 2*PI*(2.0 - phi)
#define SAU_GLDA_1_2PI  0.38196601125010515180 // (in cycle %) 2.0 - phi
#define SAU_FIBH32                2654435769UL // 32-bit Fibonacci hash number

/** Rotate bits left, for 32-bit unsigned \p x, \p r positions. */
#define SAU_ROL32(x, r) \
	((uint32_t)(x) << ((r) & 31) | ((uint32_t)(x) >> (32-((r) & 31))))

/** Rotate bits right, for 32-bit unsigned \p x, \p r positions. */
#define SAU_ROR32(x, r) \
	((uint32_t)(x) >> ((r) & 31) | ((uint32_t)(x) << (32-((r) & 31))))

/** Multiplicatively mix bits using varying right-rotation,
    for 32-bit unsigned \p x value, \p r rotation, \p ro offset. */
#define SAU_MUVAROR32(x, r, ro) \
	(((uint32_t)(x) | ((1<<((ro) & 31))|1)) * SAU_ROR32((x), (r)+(ro)))

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint32_t SAU_ms_in_samples(uint64_t time_ms, uint64_t srate) {
	uint64_t time = time_ms * srate;
	time = (time + 500) / 1000;
	return time;
}

/**
 * Metallic value function. Golden ratio for \p x == 1, silver for x == 2, etc.
 * Also accepts zero (with the result one), and values in-between the integers.
 * (Maps negative infinity to 0.0, 0.0 to 1.0, and positive infinity to itself.
 * Negative values give how much the positive value would have been increased.)
 *
 * \return metallic value
 */
static inline double SAU_met(double x) {
	return 0.5f * (x + sqrt(x * x + 4.f));
}

/**
 * Random access noise. Chaotic waveshaper which turns evenly spaced, and other
 * simple, number sequences into something similar to white noise. Returns zero
 * for zero. The frequency spectrum when used with a counter is flat. The third
 * part is necessary in order to make the resulting pseudo-randomness passable,
 * for more general non-cryptographic purposes. (Without it there is still e.g.
 * fairness if used for dice throws and properties of sums of numbers are fine,
 * but lower bits become so much worse than higher ones that the bits and bytes
 * extracted from the output, if used as sequences of such, give poor results.)
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline int32_t SAU_ranoise32(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
	/*
	 * 14 below appears a good offset number. For a high-quality result, it
	 * may be best to use a number around 16 in 8-25 inclusive. Statistical
	 * testing shows 5-27 as the maximal range beyond which Diehard Squeeze
	 * fails. Subtle audio qualities vary with the number; 14 seems smooth.
	 */
	s *= SAU_ROR32(s, s + 14);
	s ^= (s >> 7) ^ (s >> 16); // improve worse lower bits with higher bits
	return s;
}

/**
 * Random access noise. Chaotic waveshaper which turns evenly spaced, and other
 * simple, number sequences into white noise. Returns zero for zero. This is an
 * improved version of SAU_ranoise32(), which passes more statistical tests and
 * with small overhead is more suitable for general non-cryptographic purposes.
 *
 * This function is mainly an alternative to using buffers of noise, for random
 * access. The index \p n can be used as a counter or varied for random access.
 *
 * \return pseudo-random number for index \p n
 */
static inline int32_t SAU_ranoise32b(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
	s ^= s >> 14;
	s = SAU_MUVAROR32(s, s >> 27, 0);
	s ^= s >> 13;
	return s;
}

static inline int32_t SAU_ranoise32c(uint32_t n) {
	uint32_t s = n * SAU_FIBH32;
//	s ^= s >> (27 - (s & 15));
	s ^= s >> (5 + (s & 15));
	s ^= s >> 14;
	s = SAU_ROR32(s, s + 14);
	return s;
}

/**
 * Random access approximation of velvet noise. The thresholds are selected for
 * a similar loudness to normal random noise; values used affect pulse density.
 *
 * \return ternary signed value for index \p n
 */
static inline int32_t SAU_ravelvet(uint32_t n) {
	uint32_t s = n * SAU_FIBH32, s0, s1;
	s0 = s * SAU_ROR32(s, s + 11);
	s1 = s * SAU_ROR32(s, s + 20);
	return (s0 >= (uint32_t)(1<<31) + (1<<30)) -
	       (s1 >= (uint32_t)(1<<31) + (1<<30));
}

/**
 * Math functions.
 */
enum {
	SAU_MATH_ABS = 0,
	SAU_MATH_EXP,
	SAU_MATH_LOG,
	SAU_MATH_MET,
	SAU_MATH_SQRT,
	SAU_MATH_FUNCTIONS
};

typedef double (*SAU_Math_val_f)(double x);

/** Names of math functions. */
extern const char *const SAU_Math_names[SAU_MATH_FUNCTIONS + 1];

/** Value functions for math functions. */
extern const SAU_Math_val_f SAU_Math_val_func[SAU_MATH_FUNCTIONS];
