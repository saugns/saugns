/* sgensys: Math definitions.
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
#include "sgensys.h"
#include <math.h>
#include <limits.h>

#define SGS_PI          3.14159265358979323846
#define SGS_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define SGS_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define SGS_HUMMID    632.45553203367586639978 // human hearing range geom.mean

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint64_t SGS_ms_in_samples(uint64_t time_ms, uint64_t srate,
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
#define SGS_ui32rint(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrint(x) : llrint(x)))

/** Apply lrintf() if long can hold UINT32_MAX, otherwise llrintf(). */
#define SGS_ui32rintf(x) ((uint32_t) \
	(LONG_MAX >= UINT32_MAX ? lrintf(x) : llrintf(x)))

/** Portably wrap-around behaving lrint() within 64-bit int boundaries.
    Apply lrint() if long can hold INT64_MAX, otherwise llrint(). */
#define SGS_i64rint(x) ((int64_t) \
	(LONG_MAX >= INT64_MAX ? lrint(x) : llrint(x)))

/** Portably wrap-around behaving lrintf() within 64-bit int boundaries.
    Apply lrintf() if long can hold INT64_MAX, otherwise llrintf(). */
#define SGS_i64rintf(x) ((int64_t) \
	(LONG_MAX >= INT64_MAX ? lrintf(x) : llrintf(x)))

/**
 * Convert cyclical value (0.0 = 0% and 1.0 = 100%, with ends
 * wrapping around) to 32-bit integer with 0 as the 0% value.
 */
static inline uint32_t SGS_cyclepos_dtoui32(double x) {
	// needs long(er) range because 0.5 from remainder becomes INT32_MAX+1
	return SGS_ui32rint(remainder(x, 1.f) * (float)UINT32_MAX);
}
