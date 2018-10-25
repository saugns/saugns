/* ssndgen: Wave module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "../common.h"

#define SSG_Wave_LENBITS 11
#define SSG_Wave_LEN     (1<<SSG_Wave_LENBITS) /* 2048 */
#define SSG_Wave_LENMASK (SSG_Wave_LEN - 1)

#define SSG_Wave_MAXVAL 1.f
#define SSG_Wave_MINVAL (-SSG_Wave_MAXVAL)

#define SSG_Wave_SCALEBITS (32-SSG_Wave_LENBITS)
#define SSG_Wave_SCALE     (1<<SSG_Wave_SCALEBITS)
#define SSG_Wave_SCALEMASK (SSG_Wave_SCALE - 1)

/**
 * Wave types.
 */
enum {
	SSG_WAVE_SIN = 0,
	SSG_WAVE_SQR,
	SSG_WAVE_TRI,
	SSG_WAVE_SAW,
	SSG_WAVE_SHA,
	SSG_WAVE_SZH,
	SSG_WAVE_SHH,
	SSG_WAVE_SSR,
//	SSG_WAVE_SZHHR,
	SSG_WAVE_TYPES
};

/** LUTs for wave types. */
extern float SSG_Wave_luts[SSG_WAVE_TYPES][SSG_Wave_LEN];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const SSG_Wave_names[SSG_WAVE_TYPES + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define SSG_Wave_INDEX(phase) \
	(((uint32_t)(phase)) >> SSG_Wave_SCALEBITS)

/**
 * Get LUT value for 32-bit unsigned phase using linear interpolation.
 *
 * \return sample
 */
static inline float SSG_Wave_get_lerp(const float *lut, uint32_t phase) {
	uint32_t ind = SSG_Wave_INDEX(phase);
	float s = lut[ind];
	s += (lut[(ind + 1) & SSG_Wave_LENMASK] - s) *
	     ((phase & SSG_Wave_SCALEMASK) * (1.f / SSG_Wave_SCALE));
	return s;
}

extern void SSG_global_init_Wave(void);

extern void SSG_Wave_print(uint8_t id);
