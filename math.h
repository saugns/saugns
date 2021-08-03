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
 * Modified Taylor polynomial of degree 7 for sinf(x).
 *
 * Optimized for -PI/2 <= x <= PI/2; for use with pre-wrapped x values.
 *
 * Uses modified scale factors for several terms, to
 * minimize maximum error for -PI/2 <= x <= PI/2. In
 * that domain, the result is much-reduced error. To
 * compare, less max error than unmodified Taylor 9.
 *  - 1.568794e-04 (Max error, unmodified Taylor 7.)
 *  - 3.576279e-06 (Max error, unmodified Taylor 9.)
 *  - 8.940697e-07 (Max error, this Taylor 7 tweak.)
 */
static inline float SGS_sinf_t7(float x) {
	const float scale[3] = {
		-1.f/6    * 17010.f/17011,
		+1.f/120  *   772.f/773   * 821.f/822,
		-1.f/5040 *    66.f/67    *  42.f/43  * 31.f/32,
	};
	float x2 = x*x;
	return x + x*x2*(scale[0] + x2*(scale[1] + x2*scale[2]));
}

/**
 * Taylor polynomial of degree 9 for sinf(x).
 *
 * Modified with a scale factor for the last term
 * to keep the result closer to and below +/- 1.0
 * for -PI/2 <= x <= PI/2. In practice "perfect",
 * for single-precision values, within the range.
 *
 * For use with pre-wrapped x values, -PI/2 <= x <= PI/2
 * (unwrapped values give too large result near +/- PI).
 */
static inline float SGS_sinf_t9(float x) {
	const float scale9 = 1.f/362880 * 44.f/45;
	float x2 = x*x;
	return x + x*x2*(-1.f/6 + x2*(1.f/120 + x2*(-1.f/5040 + x2*scale9)));
}
