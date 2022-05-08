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

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint64_t SAU_ms_in_samples(uint64_t time_ms, uint64_t srate) {
	uint64_t time = time_ms * srate;
	time = (time + 500) / 1000;
	return time;
}

/**
 * Convert an unsigned 64-bit integer to 0.0 to 1.0 value.
 */
static inline double SAU_d01_from_ui64(uint64_t x) {
	return (x >> 11) * 0x1.0p-53;
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
 * Math symbol ids for functions and named constants.
 */
enum {
	SAU_MATH_ABS = 0,
	SAU_MATH_COS,
	SAU_MATH_EXP,
	SAU_MATH_LOG,
	SAU_MATH_MET,
	SAU_MATH_MF,
	SAU_MATH_PI,
	SAU_MATH_RAND,
	SAU_MATH_RINT,
	SAU_MATH_SEED,
	SAU_MATH_SIN,
	SAU_MATH_SQRT,
	SAU_MATH_TIME,
	SAU_MATH_SYMBOLS
};

struct SAU_Math_state;

/** State for math functions for each parsing and interpretation unit. */
struct SAU_Math_state {
	uint64_t seed;
};

/** Math function parameter type values. */
enum {
	SAU_MATH_VAL_F = 0,
	SAU_MATH_STATE_F,
	SAU_MATH_STATEVAL_F,
	SAU_MATH_NOARG_F
};
/** Math function pointer types. */
union SAU_Math_sym_f {
	double (*val)(double x);
	double (*state)(struct SAU_Math_state *o);
	double (*stateval)(struct SAU_Math_state *o, double x);
	double (*noarg)(void);
};

/** Names of math functions, with an extra NULL pointer at the end. */
extern const char *const SAU_Math_names[SAU_MATH_SYMBOLS + 1];

/** Parameter types for math functions. */
extern const uint8_t SAU_Math_params[SAU_MATH_SYMBOLS];

/** Function addresses for math functions. */
extern const union SAU_Math_sym_f SAU_Math_symbols[SAU_MATH_SYMBOLS];

/*
 * Simple PRNGs
 */

/**
 * Fixed-increment SplitMix64, based on C version provided by Vigna.
 *
 * \return next pseudo-random value
 */
static inline uint64_t SAU_splitmix64_next(uint64_t *restrict pos) {
	uint64_t z = (*pos += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}
