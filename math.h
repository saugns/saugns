/* mgensys: Math header.
 * Copyright (c) 2011, 2020 Joel K. Pettersson
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

#define MGS_PI       3.14159265358979323846
#define MGS_ASIN_1_2 0.52359877559829887308 // asin(0.5)
#define MGS_SQRT_1_2 0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)

#define MGS_DC_OFFSET 1.0E-25

typedef int i16_16; /* fixed-point 16.16 */
typedef unsigned int ui16_16; /* unsigned fixed-point 16.16 */

/**
 * Convert time in ms to time in samples for a sample rate.
 */
#define MGS_MS_IN_SAMPLES(ms, srate) \
	lrintf(((ms) * .001f) * (srate))

/** Initial seed for MGS_xorshift32(). Other non-zero values can be used. */
#define MGS_XORSHIFT32_SEED 2463534242UL

/**
 * Get Marsaglia xorshift32 state from non-zero \p seed.
 */
static inline uint32_t MGS_xorshift32(uint32_t seed) {
	uint32_t x = seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5; /* Marsaglia's version */
	//x ^= x << 15; /* WebDrake version */
	return x;
}
