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
#ifndef USE_PILUT
# define USE_PILUT 1
#endif

static inline double berp(double s[4]) {
	double x = 0.5;
	double s0ps3 = s[0]+s[3];
	double c0 = s[1];
	double c1 = 3/2.0*s[2] - 1/2.0*(s[1]+s0ps3);
	double c2 = 1/2.0*(s0ps3-s[1]-s[2]);
	return (c2*x+c1)*x+c0;

/*
	// 4-point, 3rd-order B-spline (x-form)
	double s0ps2 = s[0]+s[2];
	double c0 = 1/6.0*s0ps2 + 2/3.0*s[1];
	double c1 = 1/2.0*(s[2]-s[0]);
	double c2 = 1/2.0*s0ps2 - s[1];
	double c3 = 1/2.0*(s[1]-s[2]) + 1/6.0*(s[3]-s[0]);
	return ((c3*x+c2)*x+c1)*x+c0;
*/
}

#if !USE_PILUT
/*
 * Implementation of SGS_Osc_run()
 * using naive LUTs with linear interpolation.
 *
 * Post-increments phase each sample.
 */
static void naive_run(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
	const float *const lut = SGS_Wave_luts[o->wave];
	uint32_t phase = 0, prev_phase = o->prev_phase;
	double m[4];
	m[2] = o->m[0];
	m[3] = o->m[1];
	for (size_t i = 0; i < buf_len; ++i) {
		int32_t phase_inc = lrintf(o->coeff * freq[i]);
		o->phase += phase_inc;
		phase = o->phase;
		int32_t s_pm = 0;
		if (pm_f != NULL) {
			s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
		}
		phase += s_pm;
		int32_t phase_diff = phase - prev_phase;
		m[0] = m[2];
		m[1] = m[3];
		m[2] = SGS_Wave_get_lerp(lut, phase - (phase_diff >> 1));
		m[3] = SGS_Wave_get_lerp(lut, phase);
		float s = berp(m);
		prev_phase = phase;
		s *= amp[i];
		if (layer > 0) s += buf[i];
		buf[i] = s;
	}
	o->m[0] = m[2];
	o->m[1] = m[3];
	o->prev_phase = phase;
}

/*
 * Implementation of SGS_Osc_run_env()
 * using naive LUTs with linear interpolation.
 *
 * Post-increments phase each sample.
 */
static void naive_run_env(SGS_Osc *restrict o,
		float *restrict buf, size_t buf_len,
		uint32_t layer,
		const float *restrict freq,
		const float *restrict amp,
		const float *restrict pm_f) {
	const float *const lut = SGS_Wave_luts[o->wave];
	uint32_t phase = 0, prev_phase = o->prev_phase;
//	float ms[4];
	for (size_t i = 0; i < buf_len; ++i) {
		int32_t phase_inc = lrintf(o->coeff * freq[i]);
		o->phase += phase_inc;
		phase = o->phase;
		int32_t s_pm = 0;
		if (pm_f != NULL) {
			s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
		}
		phase += s_pm;
		int32_t phase_diff = phase - prev_phase;
		double s0 = SGS_Wave_get_lerp(lut, phase - (phase_diff >> 1));
		double s1 = SGS_Wave_get_lerp(lut, phase);
		float s = (s0 + s1) * 0.5f;
		prev_phase = phase;
		float s_amp = amp[i] * 0.5f;
		s = (s * s_amp) + fabs(s_amp);
		if (layer > 0) s *= buf[i];
		buf[i] = s;
	}
	o->prev_phase = phase;
}
#endif

#if USE_PILUT
static void SGS_Osc_reset(SGS_Osc *o) {
	const float *const lut = SGS_Wave_piluts[o->wave];
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (o->flags & SGS_OSC_RESET_DIFF) {
		/* Set up for differentiation start and a valid previous. */
		int32_t phase_diff = SGS_Wave_SLEN;
		int32_t phase = o->phase + phase_diff; /* good for 0 Hz case */
		o->prev_Is = SGS_Wave_get_lerp(lut, phase - phase_diff);
		double Is = SGS_Wave_get_lerp(lut, phase);
		double x = (diff_scale / phase_diff);
		o->prev_diff_s = (Is - o->prev_Is) * x + diff_offset;
		o->prev_Is = Is;
		o->prev_phase = phase;
	}
	o->flags &= ~SGS_OSC_RESET;
}
#endif

/**
 * Run for \p buf_len samples, generating output
 * for carrier or PM input.
 *
 * For \p layer greater than zero, adds
 * the output to \p buf instead of assigning it.
 *
 * Pre-increments phase each sample.
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
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (o->flags & SGS_OSC_RESET)
		SGS_Osc_reset(o);
	if (pm_f != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			int32_t s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
			int32_t phase_inc = lrintf(o->coeff * freq[i]);
			uint32_t phase = (o->phase += phase_inc) + s_pm;
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SGS_Wave_get_lerp(lut, phase);
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
			int32_t phase_inc = lrintf(o->coeff * freq[i]);
			uint32_t phase = (o->phase += phase_inc);
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SGS_Wave_get_lerp(lut, phase);
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
 * Pre-increments phase each sample.
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
	const float diff_scale = SGS_Wave_DVSCALE(o->wave);
	const float diff_offset = SGS_Wave_DVOFFSET(o->wave);
	if (o->flags & SGS_OSC_RESET)
		SGS_Osc_reset(o);
	if (pm_f != NULL) {
		for (size_t i = 0; i < buf_len; ++i) {
			float s;
			int32_t s_pm = lrintf(pm_f[i] * (float) INT32_MAX);
			int32_t phase_inc = lrintf(o->coeff * freq[i]);
			uint32_t phase = (o->phase += phase_inc) + s_pm;
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SGS_Wave_get_lerp(lut, phase);
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
			int32_t phase_inc = lrintf(o->coeff * freq[i]);
			uint32_t phase = (o->phase += phase_inc);
			int32_t phase_diff = phase - o->prev_phase;
			if (phase_diff == 0) {
				s = o->prev_diff_s;
			} else {
				double Is = SGS_Wave_get_lerp(lut, phase);
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
	naive_run_env(o, buf, buf_len, layer, freq, amp, pm_f);
#endif
}
