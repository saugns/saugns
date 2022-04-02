/* mgensys: Noise generator implementation.
 * Copyright (c) 2020-2022 Joel K. Pettersson
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

#include "ngen.h"

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
void MGS_NGen_run(MGS_NGen *restrict o mgsMaybeUnused,
		float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float s = MGS_Noise_next(&o->pos);
		buf[i] = s;
	}
}
