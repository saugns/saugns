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
 *
 * Turn off to use the raw naive LUTs,
 * kept for testing/"viewing" of them.
 */
#define USE_PILUT 1

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
	const float *const lut = SGS_Wave_piluts[o->wave];
	if (o->flags & SGS_OSC_RESET_DIFF) {
		/* Ensure no click accompanies differentiation start. */
		o->phase_inc = 0;
		o->flags &= ~SGS_OSC_RESET_DIFF;
	}
	float diff_scale = SGS_Wave_DIFFSCALE(o->wave);
	float diff_offset = SGS_Wave_DIFFOFFSET(o->wave);
	if (pm_f != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			int32_t s_pm = llrintf(pm_f[i] * (float)INT32_MAX);
			uint32_t phase = o->phase + s_pm;
			uint32_t prev_phase = phase - o->phase_inc;
			float prev_s = SGS_Wave_get_lerp(lut, prev_phase);
			float s = SGS_Wave_get_lerp(lut, phase);
			float diff_s = SGS_Wave_get_diffv(s, prev_s,
					diff_scale, o->phase_inc) + diff_offset;
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			s = diff_s * amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	} else {
		float prev_s = SGS_Wave_get_lerp(lut, o->phase - o->phase_inc);
		for (size_t i = 0; i < buf_len; ++i) {
			float s = SGS_Wave_get_lerp(lut, o->phase);
			float diff_s = SGS_Wave_get_diffv(s, prev_s,
					diff_scale, o->phase_inc) + diff_offset;
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			prev_s = s;
			s = diff_s * amp[i];
			if (layer > 0) s += buf[i];
			buf[i] = s;
		}
	}
#else /* test naive LUT */
	const float *const lut = SGS_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		int32_t s_pm = 0;
		if (pm_f != NULL) {
			s_pm = llrintf(pm_f[i] * (float)INT32_MAX);
		}
		uint32_t phase = o->phase + s_pm;
		int32_t phase_inc = lrintf(o->coeff * freq[i]);
		float s = SGS_Wave_get_lerp(lut, phase) * amp[i];
		o->phase += phase_inc;
		if (layer > 0) s += buf[i];
		buf[i] = s;
	}
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
	const float *const lut = SGS_Wave_piluts[o->wave];
	if (o->flags & SGS_OSC_RESET_DIFF) {
		/* Ensure no click accompanies differentiation start. */
		o->phase_inc = 0;
		o->flags &= ~SGS_OSC_RESET_DIFF;
	}
	float diff_scale = SGS_Wave_DIFFSCALE(o->wave);
	float diff_offset = SGS_Wave_DIFFOFFSET(o->wave);
	if (pm_f != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			int32_t s_pm = llrintf(pm_f[i] * (float)INT32_MAX);
			uint32_t phase = o->phase + s_pm;
			uint32_t prev_phase = phase - o->phase_inc;
			float prev_s = SGS_Wave_get_lerp(lut, prev_phase);
			float s = SGS_Wave_get_lerp(lut, phase);
			float diff_s = SGS_Wave_get_diffv(s, prev_s,
					diff_scale, o->phase_inc) + diff_offset;
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			s = diff_s;
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	} else {
		float prev_s = SGS_Wave_get_lerp(lut, o->phase - o->phase_inc);
		for (size_t i = 0; i < buf_len; ++i) {
			float s = SGS_Wave_get_lerp(lut, o->phase);
			float diff_s = SGS_Wave_get_diffv(s, prev_s,
					diff_scale, o->phase_inc) + diff_offset;
			o->phase_inc = lrintf(o->coeff * freq[i]);
			o->phase += o->phase_inc;
			prev_s = s;
			s = diff_s;
			float s_amp = amp[i] * 0.5f;
			s = (s * s_amp) + fabs(s_amp);
			if (layer > 0) s *= buf[i];
			buf[i] = s;
		}
	}
#else /* test naive LUT */
	const float *const lut = SGS_Wave_luts[o->wave];
	for (size_t i = 0; i < buf_len; ++i) {
		int32_t s_pm = 0;
		if (pm_f != NULL) {
			s_pm = llrintf(pm_f[i] * (float)INT32_MAX);
		}
		uint32_t phase = o->phase + s_pm;
		int32_t phase_inc = lrintf(o->coeff * freq[i]);
		float s = SGS_Wave_get_lerp(lut, phase);
		o->phase += phase_inc;
		float s_amp = amp[i] * 0.5f;
		s = (s * s_amp) + fabs(s_amp);
		if (layer > 0) s *= buf[i];
		buf[i] = s;
	}
#endif
}
