/* mgensys: Wave module.
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

#define MGS_Wave_LENBITS 11
#define MGS_Wave_LEN     (1<<MGS_Wave_LENBITS) /* 2048 */
#define MGS_Wave_LENMASK (MGS_Wave_LEN - 1)

#define MGS_Wave_MAXVAL 1.f
#define MGS_Wave_MINVAL (-MGS_Wave_MAXVAL)

#define MGS_Wave_SCALEBITS (32-MGS_Wave_LENBITS)
#define MGS_Wave_SCALE     (1<<MGS_Wave_SCALEBITS)
#define MGS_Wave_SCALEMASK (MGS_Wave_SCALE - 1)

/**
 * Wave types.
 */
enum {
	MGS_WAVE_SIN = 0,
	MGS_WAVE_SQR,
	MGS_WAVE_TRI,
	MGS_WAVE_SAW,
	MGS_WAVE_SHA,
	MGS_WAVE_SZH,
	MGS_WAVE_SSR,
	MGS_WAVE_TYPES
};

/** LUTs for wave types. */
extern float MGS_Wave_luts[MGS_WAVE_TYPES][MGS_Wave_LEN];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const MGS_Wave_names[MGS_WAVE_TYPES + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define MGS_Wave_INDEX(phase) \
	(((uint32_t)(phase)) >> MGS_Wave_SCALEBITS)

/**
 * Get LUT value for 32-bit unsigned phase using linear interpolation.
 *
 * \return sample
 */
static inline float MGS_Wave_get_lerp(const float *restrict lut,
		uint32_t phase) {
	uint32_t ind = MGS_Wave_INDEX(phase);
	float s = lut[ind];
	s += (lut[(ind + 1) & MGS_Wave_LENMASK] - s) *
		((phase & MGS_Wave_SCALEMASK) * (1.f / MGS_Wave_SCALE));
	return s;
}

void MGS_global_init_Wave(void);

void MGS_Wave_print(uint8_t id);
