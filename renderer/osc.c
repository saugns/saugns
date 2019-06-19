/* saugns: Oscillator module.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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

#include "osc.h"

/**
 * Run for \p buf_len samples, generating output
 * for carrier or PM input.
 *
 * For \p op_num greater than zero, adds
 * the output to \p buf instead of assigning it.
 *
 * \p pm_f may be NULL for no PM input.
 */
void SAU_Osc_block_add(SAU_Osc *restrict o,
		const float *restrict lut, double coeff,
		float *restrict buf, size_t buf_len,
		size_t op_num,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
	for (size_t i = 0; i < buf_len; ++i) {
		int32_t s_pm = 0;
		if (pm_f != NULL) {
			s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
		}
		float s = SAU_Osc_run(o, lut, coeff, freq[i], s_pm) * amp[i];
		if (op_num != 0) s += buf[i];
		buf[i] = s;
	}
}

/**
 * Run for \p buf_len samples, generating output
 * for FM or AM input (scaled to 0.0 - 1.0 range,
 * multiplied by \p amp).
 *
 * For \p op_num greater than zero, multiplies
 * the output into \p buf instead of assigning it.
 *
 * \p pm_f may be NULL for no PM input.
 */
void SAU_Osc_block_mul(SAU_Osc *restrict o,
		const float *restrict lut, double coeff,
		float *restrict buf, size_t buf_len,
		size_t op_num,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
	for (size_t i = 0; i < buf_len; ++i) {
		int32_t s_pm = 0;
		if (pm_f != NULL) {
			s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
		}
		float s = SAU_Osc_run(o, lut, coeff, freq[i], s_pm);
		float s_amp = amp[i] * 0.5f;
		s = (s * s_amp) + fabs(s_amp);
		if (op_num != 0) s *= buf[i];
		buf[i] = s;
	}
}
