/* SAU library: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2026 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <sau/math.h>

/*
 * General math functions defined for use by audio rendering code.
 */

#define sau_dtoi sau_i64rint  // use for wrap-around behavior
#define sau_ftoi sau_i64rintf // use for wrap-around behavior
#define sau_dscalei(i, scale) (((int32_t)(i)) * (double)(scale))
#define sau_fscalei(i, scale) (((int32_t)(i)) * (float)(scale))
#define sau_divi(i, div) (((int32_t)(i)) / (int32_t)(div))

/*
 * Array math functions, single-precision floating point.
 */

#define sau_nzero(a, n) memset((a), 0, sizeof((a)[0]) * (n))

static inline void sau_nsetf(float *restrict a, size_t n, float v) {
	for (size_t i=0; i<n; ++i) a[i]=v;
}

static inline void sau_naddnf(float *restrict a, size_t n,
		const float *restrict b) {
	for (size_t i=0; i<n; ++i) a[i]+=b[i];
}

static inline void sau_nmulf(float *restrict a, size_t n, float b) {
	for (size_t i=0; i<n; ++i) a[i]*=b;
}

static inline void sau_nmulnf(float *restrict a, size_t n,
		const float *restrict b) {
	for (size_t i=0; i<n; ++i) a[i]*=b[i];
}

static inline void sau_nmulnff(float *restrict a, size_t n,
		const float *restrict b, float c) {
	for (size_t i=0; i<n; ++i) a[i]*=b[i]*c;
}
