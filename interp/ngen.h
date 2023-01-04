/* mgensys: Noise generator implementation.
 * Copyright (c) 2020-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
#include "../noise.h"
#include "../math.h"

typedef struct mgsNGen {
	uint32_t pos;
} mgsNGen;

/**
 * Initialize instance for use.
 */
static inline void mgs_init_NGen(mgsNGen *restrict o, uint32_t pos) {
	o->pos = pos;
}

static void mgsNGen_run(mgsNGen *restrict o,
		float *restrict buf, size_t buf_len);

#if 0
static inline uint32_t lcg32_next(uint32_t x) {
	return (x * 0x915f77f5) + 5;
}

static float lcg32_fnext(uint32_t *x) {
	*x = lcg32_next(*x);
	return (INT32_MIN + (int32_t)*x) * (1.f/INT32_MAX);
}
#endif

/**
 * Run for \p buf_len samples, generating output.
 */
static mgsMaybeUnused void mgsNGen_run(mgsNGen *restrict o mgsMaybeUnused,
		float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float s = mgsNoise_next(&o->pos);
		buf[i] = s;
	}
}
