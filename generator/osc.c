/* sgensys: Oscillator implementation.
 * Copyright (c) 2011, 2017-2022 Joel K. Pettersson
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

#if !USE_PILUT
# define P(inc, ofs) ofs + o->phase; (o->phase += inc)     /* post-increment */
#else
# define P(inc, ofs) ofs + (o->phase += inc)               /* pre-increment */
#endif

/**
 * Fill phase-value buffer for use with SGS_Osc_run().
 */
void SGS_Phasor_fill(SGS_Phasor *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float fpm_scale = 1.f / SGS_HUMMID;
	if (!pm_f && !fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			phase_ui32[i] = P(lrintf(o->coeff * s_f), 0);
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			phase_ui32[i] = P(lrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			phase_ui32[i] = P(lrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			phase_ui32[i] = P(lrintf(o->coeff * s_f),
					llrintf(s_pofs * (float)INT32_MAX));
		}
	}
}

#undef P /* done */

#if !USE_PILUT
/*
 * Implementation of SGS_Osc_run()
 * using naive LUTs with linear interpolation.
 *
 * Uses post-incremented phase each sample.
 */
static void naive_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
	const float *const lut = SGS_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		buf[i] = SGS_Wave_get_lerp(lut, phase_buf[i]);
	}
}
#endif

#if USE_PILUT
/* Set up for differentiation (re)start with usable state. */
static void SGS_Osc_reset(SGS_Osc *restrict o, int32_t phase) {
	const float *const lut = SGS_Wave_piluts[o->wave];
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (o->flags & SGS_OSC_RESET_DIFF) {
		/* one-LUT-value diff works fine for any freq, 0 Hz included */
		int32_t phase_diff = SGS_Wave_SLEN;
		o->prev_Is = SGS_Wave_get_herp(lut, phase - phase_diff);
		double Is = SGS_Wave_get_herp(lut, phase);
		double x = (diff_scale / phase_diff);
		o->prev_diff_s = (Is - o->prev_Is) * x + diff_offset;
		o->prev_Is = Is;
		o->prev_phase = phase;
	}
	o->flags &= ~SGS_OSC_RESET;
}
#endif

/**
 * Run for \p buf_len samples, generating output.
 *
 * Uses pre-incremented phase each sample.
 */
void SGS_Osc_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
#if USE_PILUT /* higher-quality audio */
	const float *const lut = SGS_Wave_piluts[o->wave];
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (buf_len > 0 && o->flags & SGS_OSC_RESET)
		SGS_Osc_reset(o, phase_buf[0]);
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t phase = phase_buf[i];
		int32_t phase_diff = phase - o->prev_phase;
		if (phase_diff == 0) {
			s = o->prev_diff_s;
		} else {
			double Is = SGS_Wave_get_herp(lut, phase);
			double x = (diff_scale / phase_diff);
			s = (Is - o->prev_Is) * x + diff_offset;
			o->prev_Is = Is;
			o->prev_diff_s = s;
			o->prev_phase = phase;
		}
		buf[i] = s;
	}
#else /* test naive LUT */
	naive_run(o, buf, buf_len, phase_buf);
#endif
}
