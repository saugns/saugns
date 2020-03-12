/* mgensys: Noise generator implementation.
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

#include "ngen.h"

/**
 * Run for \p buf_len samples, generating output
 * for carrier or PM input.
 *
 * For \p layer greater than zero, adds
 * the output to \p buf instead of assigning it.
 */
void MGS_NGen_run(MGS_NGen *restrict o mgsMaybeUnused,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict amp) {
	for (size_t i = 0; i < buf_len; ++i) {
		float s = MGS_Noise_get() * amp[i];
		if (layer > 0) s += buf[i];
		buf[i] = s;
	}
}

/**
 * Run for \p buf_len samples, generating output
 * for FM or AM input (scaled to 0.0 - 1.0 range,
 * multiplied by \p amp).
 *
 * For \p layer greater than zero, multiplies
 * the output into \p buf instead of assigning it.
 */
void MGS_NGen_run_env(MGS_NGen *restrict o mgsMaybeUnused,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict amp) {
	for (size_t i = 0; i < buf_len; ++i) {
		float s = MGS_Noise_get();
		float s_amp = amp[i] * 0.5f;
		s = (s * s_amp) + fabs(s_amp);
		if (layer > 0) s *= buf[i];
		buf[i] = s;
	}
}
