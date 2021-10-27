/* sgensys: Math definitions.
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
#include "common.h"
#include <math.h>

#define SGS_PI          3.14159265358979323846
#define SGS_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define SGS_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define SGS_GLDA        2.39996322972865332223 // golden angle 2*PI*(2.0 - phi)
#define SGS_GLDA_1_2PI  0.38196601125010515180 // (in cycle %) 2.0 - phi

/**
 * Convert time in ms to time in samples for a sample rate.
 */
#define SGS_MS_IN_SAMPLES(ms, srate) \
	lrintf(((ms) * .001f) * (srate))

/**
 * Metallic value function. Golden ratio for \p x == 1, silver for x == 2, etc.
 * Also accepts zero (with the result one), and values in-between the integers.
 * (Maps negative infinity to 0.0, 0.0 to 1.0, and positive infinity to itself.
 * Negative values give how much the positive value would have been increased.)
 *
 * \return metallic value
 */
static inline double SGS_met(double x) {
	return 0.5f * (x + sqrt(x * x + 4.f));
}

/**
 * Math functions.
 */
enum {
	SGS_MATH_ABS = 0,
	SGS_MATH_EXP,
	SGS_MATH_LOG,
	SGS_MATH_MET,
	SGS_MATH_SQRT,
	SGS_MATH_FUNCTIONS
};

typedef double (*SGS_Math_val_f)(double x);

/** Names of math functions. */
extern const char *const SGS_Math_names[SGS_MATH_FUNCTIONS + 1];

/** Value functions for math functions. */
extern const SGS_Math_val_f SGS_Math_val_func[SGS_MATH_FUNCTIONS];
