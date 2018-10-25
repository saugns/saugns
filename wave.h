/* ssndgen: Wave module.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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
	SSG_WAVE_SSR,
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
static inline float SSG_Wave_get_lerp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = SSG_Wave_INDEX(phase);
	float s = lut[ind];
	s += (lut[(ind + 1) & SSG_Wave_LENMASK] - s) *
		((phase & SSG_Wave_SCALEMASK) * (1.f / SSG_Wave_SCALE));
	return s;
}

void SSG_global_init_Wave(void);

void SSG_Wave_print(uint8_t id);
