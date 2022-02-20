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
#include "common.h"
#include <math.h>

#define SGS_PI          3.14159265358979323846
#define SGS_ASIN_1_2    0.52359877559829887308 // asin(0.5)
#define SGS_SQRT_1_2    0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)
#define SGS_HUMMID    632.45553203367586639978 // human hearing range geom.mean

/**
 * Convert time in ms to time in samples for a sample rate.
 */
static inline uint64_t SGS_ms_in_samples(uint64_t time_ms, uint64_t srate,
		int *carry) {
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
