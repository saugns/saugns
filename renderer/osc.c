/* saugns: Oscillator implementation.
 * Copyright (c) 2011, 2017-2022 Joel K. Pettersson
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

#include "osc.h"

/**
 * Fill phase-increment and (optionally) phase-offset buffer
 * for use with SAU_Osc_run() or SAU_Osc_run_env().
 */
void SAU_Phasor_fill(SAU_Phasor *restrict o,
		int32_t *restrict pinc_i32,
		int32_t *restrict pofs_i32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f,
		const float *restrict fpm_f) {
	const float fpm_scale = 1.f / SAU_HUMMID;
	if (!pofs_i32 || (!pm_f && !fpm_f)) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			pinc_i32[i] = lrintf(o->coeff * s_f);
			if (pofs_i32 != NULL)
				pofs_i32[i] = 0;
		}
	} else if (!fpm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i];
			pinc_i32[i] = lrintf(o->coeff * s_f);
			pofs_i32[i] = lrintf(s_pofs * (float) INT32_MAX);
		}
	} else if (!pm_f) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = fpm_f[i] * fpm_scale * s_f;
			pinc_i32[i] = lrintf(o->coeff * s_f);
			pofs_i32[i] = lrintf(s_pofs * (float) INT32_MAX);
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s_f = freq_f[i];
			float s_pofs = pm_f[i] + (fpm_f[i] * fpm_scale * s_f);
			pinc_i32[i] = lrintf(o->coeff * s_f);
			pofs_i32[i] = lrintf(s_pofs * (float) INT32_MAX);
		}
	}
}

#if !USE_PILUT
/*
 * Implementation of SAU_Osc_run()
 * using naive LUTs with linear interpolation.
 *
 * Post-increments phase each sample.
 */
static void naive_run(SAU_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const int32_t *restrict pinc_buf,
		const int32_t *restrict pofs_buf,
		const float *restrict amp) {
	const float *const lut = SAU_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t phase = o->phasor.phase;
		if (pofs_buf != NULL) {
			phase += pofs_buf[i];
		}
		float s = SAU_Wave_get_lerp(lut, phase) * amp[i];
		o->phasor.phase += pinc_buf[i];
		if (layer > 0) s += buf[i];
		buf[i] = s;
	}
}

/*
 * Implementation of SAU_Osc_run_env()
 * using naive LUTs with linear interpolation.
 *
 * Post-increments phase each sample.
 */
static void naive_run_env(SAU_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const int32_t *restrict pinc_buf,
		const int32_t *restrict pofs_buf,
		const float *restrict amp) {
	const float *const lut = SAU_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t phase = o->phasor.phase;
		if (pofs_buf != NULL) {
			phase += pofs_buf[i];
		}
		float s = SAU_Wave_get_lerp(lut, phase);
		o->phasor.phase += pinc_buf[i];
		float s_amp = amp[i] * 0.5f;
		s = (s * s_amp) + fabs(s_amp);
		if (layer > 0) s *= buf[i];
		buf[i] = s;
	}
}
#endif

#if USE_PILUT
/* Set up for differentiation (re)start with usable state. */
static void SAU_Osc_reset(SAU_Osc *o) {
	const float *const lut = SAU_Wave_piluts[o->wave];
	const float diff_scale = SAU_Wave_DVSCALE(o->wave);
	const float diff_offset = SAU_Wave_DVOFFSET(o->wave);
	if (o->flags & SAU_OSC_RESET_DIFF) {
		/* one-LUT-value diff works fine for any freq, 0 Hz included */
		int32_t phase_diff = SAU_Wave_SLEN;
		int32_t phase = o->phasor.phase + phase_diff;
		o->prev_Is = SAU_Wave_get_herp(lut, phase - phase_diff);
		double Is = SAU_Wave_get_herp(lut, phase);
		double x = (diff_scale / phase_diff);
		o->prev_diff_s = (Is - o->prev_Is) * x + diff_offset;
		o->prev_Is = Is;
		o->prev_phase = phase;
	}
	o->flags &= ~SAU_OSC_RESET;
}
#endif

#define SAU_FIBH32 2654435769UL // 32-bit Fibonnaci hashing constant
static inline int32_t warp(uint32_t phase) {
	uint32_t s = phase * SAU_FIBH32;
	return INT32_MIN + s;
}

/**
 * Run for \p buf_len samples, generating output
 * for carrier or PM input.
 *
 * For \p layer greater than zero, adds
 * the output to \p buf instead of assigning it.
 *
 * Pre-increments phase each sample.
 *
 * \p pofs_buf may be NULL for no PM input.
 */
void SAU_Osc_run(SAU_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const int32_t *restrict pinc_buf,
		const int32_t *restrict pofs_buf,
		const float *restrict amp) {
#if 1 // TEST NOISE GEN
	if (pofs_buf != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			int32_t s_pm = pofs_buf[i];
			uint64_t nstate = (o->nstate += pinc_buf[i]) + s_pm;
			uint32_t nval = nstate >> 32;
			uint32_t phase = nstate << 32;
			s = warp(o->testcount++) * 1.f/INT32_MAX;
			s *= amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			uint64_t nstate = (o->nstate += pinc_buf[i]);
			uint32_t nval = nstate >> 32;
			uint32_t phase = nstate << 32;
			s = warp(o->testcount++) * 1.f/INT32_MAX;
			s *= amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	}
#elif USE_PILUT /* higher-quality audio */
	const float *const lut = SAU_Wave_piluts[o->wave];
	const float diff_scale = SAU_Wave_DVSCALE(o->wave);
	const float diff_offset = SAU_Wave_DVOFFSET(o->wave);
	if (o->flags & SAU_OSC_RESET)
		SAU_Osc_reset(o);
	if (pofs_buf != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			int32_t s_pm = pofs_buf[i];
			uint32_t phase = (o->phasor.phase += pinc_buf[i]) + s_pm;
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SAU_Wave_get_herp(lut, phase);
				double x = (diff_scale / phase_diff);
				s = (Is - o->prev_Is) * x + diff_offset;
				o->prev_Is = Is;
				o->prev_diff_s = s;
				o->prev_phase = phase;
			}
			s *= amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			uint32_t phase = (o->phasor.phase += pinc_buf[i]);
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SAU_Wave_get_herp(lut, phase);
				double x = (diff_scale / phase_diff);
				s = (Is - o->prev_Is) * x + diff_offset;
				o->prev_Is = Is;
				o->prev_diff_s = s;
				o->prev_phase = phase;
			}
			s *= amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	}
#else /* test naive LUT */
	naive_run(o, buf, buf_len, layer, pinc_buf, pofs_buf, amp);
#endif
}

/**
 * Run for \p buf_len samples, generating output
 * for FM or AM input (scaled to 0.0 - 1.0 range,
 * multiplied by \p amp).
 *
 * For \p layer greater than zero, multiplies
 * the output into \p buf instead of assigning it.
 *
 * Pre-increments phase each sample.
 *
 * \p pofs_buf may be NULL for no PM input.
 */
void SAU_Osc_run_env(SAU_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const int32_t *restrict pinc_buf,
		const int32_t *restrict pofs_buf,
		const float *restrict amp) {
#if 1 // TEST NOISE GEN
	if (pofs_buf != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			int32_t s_pm = pofs_buf[i];
			uint64_t nstate = (o->nstate += pinc_buf[i]) + s_pm;
			uint32_t nval = nstate >> 32;
			uint32_t phase = nstate << 32;
			s = warp(o->testcount++) * 1.f/INT32_MAX;
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			uint64_t nstate = (o->nstate += pinc_buf[i]);
			uint32_t nval = nstate >> 32;
			uint32_t phase = nstate << 32;
			s = warp(o->testcount++) * 1.f/INT32_MAX;
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	}
#elif USE_PILUT /* higher-quality audio */
	const float *const lut = SAU_Wave_piluts[o->wave];
	const float diff_scale = SAU_Wave_DVSCALE(o->wave);
	const float diff_offset = SAU_Wave_DVOFFSET(o->wave);
	if (o->flags & SAU_OSC_RESET)
		SAU_Osc_reset(o);
	if (pofs_buf != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			int32_t s_pm = pofs_buf[i];
			uint32_t phase = (o->phasor.phase += pinc_buf[i]) + s_pm;
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SAU_Wave_get_herp(lut, phase);
				double x = (diff_scale / phase_diff);
				s = (Is - o->prev_Is) * x + diff_offset;
				o->prev_Is = Is;
				o->prev_diff_s = s;
				o->prev_phase = phase;
			}
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	} else {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			uint32_t phase = (o->phasor.phase += pinc_buf[i]);
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SAU_Wave_get_herp(lut, phase);
				double x = (diff_scale / phase_diff);
				s = (Is - o->prev_Is) * x + diff_offset;
				o->prev_Is = Is;
				o->prev_diff_s = s;
				o->prev_phase = phase;
			}
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	}
#else /* test naive LUT */
	naive_run_env(o, buf, buf_len, layer, pinc_buf, pofs_buf, amp);
#endif
}
