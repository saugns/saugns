/* mgensys: Noise module.
 * Copyright (c) 2020, 2022 Joel K. Pettersson
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

/**
 * Get next noise sample for and update current position \p pos.
 *
 * \return sample
 */
static inline float MGS_Noise_next(uint32_t *restrict pos) {
	return MGS_ranoise32_next(pos) * (1.f/(float)INT32_MAX);
}

/**
 * Get noise value at arbitrary position \p n from 0 to 32-bit max.
 *
 * \return sample
 */
static inline float MGS_Noise_get(uint32_t n) {
	return MGS_ranoise32(n) * (1.f/(float)INT32_MAX);
}

/** Names of noise types, with an extra NULL pointer at the end. */
extern const char *const MGS_Noise_names[MGS_NOISE_TYPES + 1];

void MGS_global_init_Noise(void);
