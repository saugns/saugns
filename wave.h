/* sgensys: Wave module.
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

#define SGS_Wave_LENBITS 11
#define SGS_Wave_LEN     (1<<SGS_Wave_LENBITS) /* 2048 */
#define SGS_Wave_LENMASK (SGS_Wave_LEN - 1)

#define SGS_Wave_MAXVAL 1.f
#define SGS_Wave_MINVAL (-SGS_Wave_MAXVAL)

#define SGS_Wave_SCALEBITS (32-SGS_Wave_LENBITS)
#define SGS_Wave_SCALE     (1<<SGS_Wave_SCALEBITS)
#define SGS_Wave_SCALEMASK (SGS_Wave_SCALE - 1)

/**
 * Wave types.
 */
enum {
	SGS_WAVE_SIN = 0,
	SGS_WAVE_SQR,
	SGS_WAVE_TRI,
	SGS_WAVE_SAW,
	SGS_WAVE_SHA,
	SGS_WAVE_SZH,
	SGS_WAVE_SSR,
	SGS_WAVE_TYPES
};

/** LUTs for wave types. */
extern float SGS_Wave_luts[SGS_WAVE_TYPES][SGS_Wave_LEN];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const SGS_Wave_names[SGS_WAVE_TYPES + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define SGS_Wave_INDEX(phase) \
	(((uint32_t)(phase)) >> SGS_Wave_SCALEBITS)

/**
 * Get LUT value for 32-bit unsigned phase using linear interpolation.
 *
 * \return sample
 */
static inline float SGS_Wave_get_lerp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = SGS_Wave_INDEX(phase);
	float s = lut[ind];
	s += (lut[(ind + 1) & SGS_Wave_LENMASK] - s) *
		((phase & SGS_Wave_SCALEMASK) * (1.f / SGS_Wave_SCALE));
	return s;
}

void SGS_global_init_Wave(void);

void SGS_Wave_print(uint8_t id);
