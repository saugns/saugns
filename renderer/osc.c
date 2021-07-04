/* sgensys: Oscillator implementation.
 * Copyright (c) 2011, 2017-2021 Joel K. Pettersson
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

/*
 * Use pre-integrated LUTs ("PILUTs")?
 * Turn off to use the raw naive LUTs.
 */
#define USE_PILUT 1

/*
 * Use naive oscillator when no reason
 * to pick something else for quality?
 */
#define USE_NAIVE 1

/*
 * Implementation of SGS_Osc_run()
 * using naive LUTs with linear interpolation.
 */
static void naive_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
	const float *const lut = SGS_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t phase = o->phase;
		if (pm_f != NULL) {
			phase += lrintf(pm_f[i] * (float) INT32_MAX);
		}
		float s = SGS_Wave_get_lerp(lut, phase) * amp[i];
		o->phase += lrintf(o->coeff * freq[i]);
		if (layer > 0) s += buf[i];
		buf[i] = s;
	}
}

/*
 * Implementation of SGS_Osc_run_env()
 * using naive LUTs with linear interpolation.
 */
static void naive_run_env(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
	const float *const lut = SGS_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t phase = o->phase;
		if (pm_f != NULL) {
			phase += lrintf(pm_f[i] * (float) INT32_MAX);
		}
		float s = SGS_Wave_get_lerp(lut, phase);
		o->phase += lrintf(o->coeff * freq[i]);
		float s_amp = amp[i] * 0.5f;
		s = (s * s_amp) + fabs(s_amp);
		if (layer > 0) s *= buf[i];
		buf[i] = s;
	}
}

/**
 * Run for \p buf_len samples, generating output
 * for carrier or PM input.
 *
 * For \p layer greater than zero, adds
 * the output to \p buf instead of assigning it.
 *
 * \p pm_f may be NULL for no PM input.
 */
void SGS_Osc_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
#if USE_PILUT /* higher-quality audio */
# if USE_NAIVE /* use as optimization */
	if (o->wave == SGS_WAVE_SIN) {
		naive_run(o, buf, buf_len, layer, freq, amp, pm_f);
		return;
	}
# endif
	const float *const lut = SGS_Wave_luts[o->wave];
	const float *const pilut = SGS_Wave_piluts[o->wave];
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (o->flags & SGS_OSC_RESET_DIFF) {
		/* Ensure no click accompanies differentiation start. */
		o->phase_inc = SGS_Wave_SCALE;
		o->prev_diff_s = diff_offset;
		o->flags &= ~SGS_OSC_RESET_DIFF;
	}
	if (pm_f != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			int32_t s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
			uint32_t phase = o->phase + s_pm;
			float s;
			if (o->phase_inc == 0) {
				s = SGS_Wave_get_lerp(lut, phase);
			} else {
				uint32_t prev_phase = phase - o->phase_inc;
				s = (SGS_Wave_get_lerp(pilut, phase) -
				     SGS_Wave_get_lerp(pilut, prev_phase)) *
				    (diff_scale / o->phase_inc) + diff_offset;
			}
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			s *= amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	} else {
		float prev_Is = SGS_Wave_get_lerp(pilut, o->phase-o->phase_inc);
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			if (o->phase_inc == 0) {
				s = SGS_Wave_get_lerp(lut, o->phase);
			} else {
				float Is = SGS_Wave_get_lerp(pilut, o->phase);
				s = (Is - prev_Is) *
				    (diff_scale / o->phase_inc) + diff_offset;
				prev_Is = Is;
			}
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			s *= amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	}
#else /* test naive LUT */
	naive_run(o, buf, buf_len, layer, freq, amp, pm_f);
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
 * \p pm_f may be NULL for no PM input.
 */
void SGS_Osc_run_env(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
#if USE_PILUT /* higher-quality audio */
# if USE_NAIVE /* use as optimization */
	if (o->wave == SGS_WAVE_SIN) {
		naive_run_env(o, buf, buf_len, layer, freq, amp, pm_f);
		return;
	}
# endif
	const float *const lut = SGS_Wave_luts[o->wave];
	const float *const pilut = SGS_Wave_piluts[o->wave];
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (o->flags & SGS_OSC_RESET_DIFF) {
		/* Ensure no click accompanies differentiation start. */
		o->phase_inc = SGS_Wave_SCALE;
		o->prev_diff_s = diff_offset;
		o->flags &= ~SGS_OSC_RESET_DIFF;
	}
	if (pm_f != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			int32_t s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
			uint32_t phase = o->phase + s_pm;
			float s;
			if (o->phase_inc == 0) {
				s = SGS_Wave_get_lerp(lut, phase);
			} else {
				uint32_t prev_phase = phase - o->phase_inc;
				s = (SGS_Wave_get_lerp(pilut, phase) -
				     SGS_Wave_get_lerp(pilut, prev_phase)) *
				    (diff_scale / o->phase_inc) + diff_offset;
			}
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	} else {
		float prev_Is = SGS_Wave_get_lerp(pilut, o->phase-o->phase_inc);
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			if (o->phase_inc == 0) {
				s = SGS_Wave_get_lerp(lut, o->phase);
			} else {
				float Is = SGS_Wave_get_lerp(pilut, o->phase);
				s = (Is - prev_Is) *
				    (diff_scale / o->phase_inc) + diff_offset;
				prev_Is = Is;
			}
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	}
#else /* test naive LUT */
	naive_run_env(o, buf, buf_len, layer, freq, amp, pm_f);
#endif
}
