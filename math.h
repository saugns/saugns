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
