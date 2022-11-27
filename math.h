/* sgensys: Math definitions.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "sgensys.h"
#include <math.h>

#define SGS_PI       3.14159265358979323846
#define SGS_ASIN_1_2 0.52359877559829887308 // asin(0.5)
#define SGS_SQRT_1_2 0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)

/**
 * Convert time in ms to time in samples for a sample-rate.
 */
static inline uint64_t SGS_ms_in_samples(uint64_t time_ms, uint64_t srate) {
	uint64_t time = time_ms * srate;
	time = (time + 500) / 1000;
	return time;
}

/**
 * Convert cyclical value (0.0 = 0% and 1.0 = 100%, with ends
 * wrapping around) to 32-bit integer with 0 as the 0% value.
 *
 * Note that if \p x is exactly 0.5 or some integer multiples
 * of it, and long is 32-bit, the result is undefined; but in
 * practice it'll be either INT32_MIN or INT32_MAX, depending
 * on if lrint() overflow wraps or saturates on the platform.
 */
static inline int32_t SGS_cyclepos_dtoi32(double x) {
	return lrint(remainder(x, 1.f) * 2.f * (float)INT32_MAX);
}
