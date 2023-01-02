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

/** Rotate bits right, for 32-bit unsigned \p x, \p r positions. */
#define MGS_ROR32(x, r) \
	((uint32_t)(x) >> ((r) & 31) | (uint32_t)(x) << ((32-(r)) & 31))

/** Multiplicatively mix bits using varying right-rotation,
    for 32-bit unsigned \p x value, \p r rotation, \p ro rotation offset. */
#define MGS_MUVAROR32(x, r, ro) \
	(((uint32_t)(x) | ((1<<((ro) & 31))|1)) * MGS_ROR32((x), (r)+(ro)))

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

/**
 * \return +1 if \p n & 1 is 0, otherwise -1.
 */
static inline int mgs_oddness_as_sign(int n) {
	return (1 - ((n & 1) * 2));
}

/*
 * Simple PRNGs
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
static inline int32_t mgs_ranoise32(uint32_t n) {
	uint32_t s = n * MGS_FIBH32;
	s = MGS_MUVAROR32(s, s >> 27, 0);
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
static inline int32_t mgs_ranoise32_next(uint32_t *restrict pos) {
	uint32_t s = *pos += MGS_FIBH32;
	s = MGS_MUVAROR32(s, s >> 27, 0);
	return s;
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
	x ^= x << 5; // Marsaglia's version (most common, better in dieharder)
	//x ^= x << 15; // WebDrake's version (also valid, worse in dieharder)
	return x;
}
