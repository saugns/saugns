/* mgensys: Noise module.
 * Copyright (c) 2020 Joel K. Pettersson
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
#include "math.h"

/**
 * Noise types.
 */
enum {
	//MGS_NOISE_RD = 0,
	//MGS_NOISE_PN = 0,
	MGS_NOISE_WH = 0,
	//MGS_NOISE_BL,
	//MGS_NOISE_VL,
	MGS_NOISE_TYPES
};

/** MGS_xorshift32() state for noise generation. */
extern uint32_t MGS_Noise_x32state;

/**
 * Get next random unsigned 32-bit number.
 */
#define MGS_Noise_NEXT() \
	(MGS_Noise_x32state = MGS_xorshift32(MGS_Noise_x32state))

/**
 * Get noise value.
 *
 * \return sample
 */
static inline float MGS_Noise_get(void) {
	int32_t s_i32 = INT32_MIN + MGS_Noise_NEXT();
	return s_i32 * (1.f / INT32_MAX);
}

/** Names of noise types, with an extra NULL pointer at the end. */
extern const char *const MGS_Noise_names[MGS_NOISE_TYPES + 1];

void MGS_global_init_Noise(void);
