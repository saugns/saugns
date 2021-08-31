/* sgensys: Math definitions.
 * Copyright (c) 2011-2012, 2017-2021 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
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

#define SGS_PI       3.14159265358979323846
#define SGS_PI_2     1.57079632679489661923
#define SGS_ASIN_1_2 0.52359877559829887308 // asin(0.5)
#define SGS_SQRT_1_2 0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)

/**
 * Convert time in ms to time in samples for a sample rate.
 */
#define SGS_MS_IN_SAMPLES(ms, srate) \
	lrintf(((ms) * .001f) * (srate))

/**
 * Minimax-ified Taylor polynomial of degree 7 for sinf(x).
 *
 * Optimized for -PI/2 <= x <= PI/2; for use with pre-wrapped x values.
 *
 * Uses modified scale factors for several terms, to
 * minimize maximum error for -PI/2 <= x <= PI/2. In
 * that domain, the result is much-reduced error. To
 * compare, less max error than unmodified Taylor 9.
 *  - 1.568794e-04 (Max error, unmodified Taylor 7.)
 *  - 3.576279e-06 (Max error, unmodified Taylor 9.)
 *  - 5.891312e-07 (Max error, this Taylor 7 tweak.)
 */
static inline float SGS_sinf_t7(float x) {
	const float scale[] = {
		+1.f      * 0.99999661599039058046,
		-1.f/6    * 0.99988967477352019841,
		+1.f/120  * 0.99675900242704074117,
		-1.f/5040 * 0.92552840499774902128,
	};
	float x2 = x*x;
	return x*(scale[0] + x2*(scale[1] + x2*(scale[2] + x2*scale[3])));
}

/**
 * Like a sine squashed inward so as to more resemble a bell.
 *
 * Polynomial shape like sin(x * pi) except roughly as narrow
 * as sin(x * pi * 1.25) where value is far enough from zero.
 * The slope decreases to zero as the output approaches zero.
 * Allows input range of -1 <= x <= 1, with symmetric result.
 */
static inline float SGS_sinbell_r1(float x) {
	float xa = fabsf(x);
	float x2a = x*xa;
	return 16.f*xa*(x - (x2a+x2a) + xa*x2a); /* 16x^2 - 32x^3 + 16x^4 */
}

/**
 * Like a sine morphed to more resemble a bell around the end
 * of a cycle only, looking more like a plain sine elsewhere.
 *
 * Allows input range of -1 <= x <= 1, with symmetric result.
 */
static inline float SGS_sintilt_r1(float x) {
	float xa = fabsf(x);
	const float a = 5.f/1.00857799713379571722;
	return a*x*(1 - xa*(1 + xa*(1 - xa))); /* a(x^1 - x^2 - x^3 + x^4) */
}

static inline float SGS_quadsat_r1(float x, float c) {
	float xa = fabsf(x);
	float xc = c*16.f*(1 + xa*(-2 + xa));
	return x*(4.f + xa*(-6.f + xa*(4.f - xa) + xc));
}
