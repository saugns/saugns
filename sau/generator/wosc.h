/* SAU library: Wave oscillator implementation.
 * Copyright (c) 2011, 2017-2025 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "../wave.h"
#include "../math.h"
#include "phasor.h"

#define SAU_OSC_RESET_DIFF  (1<<0)
#define SAU_OSC_RESET       ((1<<1) - 1)
#define SAU_OSC_SKIPPED_I   (1<<1)

typedef struct sauWOsc {
	sauPhasor phasor;
	sauWaveOpt opt;
	uint32_t prev_phase;
	double prev_Is;
	float prev_s;
	float filt_s;
} sauWOsc;

/**
 * Initialize instance for use.
 */
static inline void sau_init_WOsc(sauWOsc *restrict o, uint32_t srate) {
	*o = (sauWOsc){
		.phasor = (sauPhasor){
			.cycle_phase = 0,
			.coeff = sauPhasor_COEFF(srate),
		},
		.opt.wave = SAU_WAVE_N_sin,
		.opt.func = SAU_WAVE_F_NAIVE,
		.opt.flags = 0,
	};
}

static inline void sauWOsc_set_phase(sauWOsc *restrict o, uint32_t phase) {
	uint32_t offset = sauWave_picoeffs[o->opt.wave].phase_adj;
	if (o->opt.func == SAU_WAVE_F_ADAA) o->phasor.cycle_phase -= offset;
	sauPhasor_set_phase(&o->phasor, phase);
	if (o->opt.func == SAU_WAVE_F_ADAA) o->phasor.cycle_phase += offset;
}

static inline void sauWOsc_set_wave(sauWOsc *restrict o, uint8_t wave) {
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		uint32_t old_offset = sauWave_picoeffs[o->opt.wave].phase_adj;
		uint32_t offset = sauWave_picoeffs[wave].phase_adj;
		o->phasor.cycle_phase += offset - old_offset;
		o->opt.wave = wave;
		o->opt.flags |= SAU_OSC_RESET_DIFF;
	} else {
		o->opt.wave = wave;
	}
}

/**
 * Update mode options. Will adjust settings which are dependent on the mode.
 */
static void sauWOsc_set_opt(sauWOsc *restrict o, const sauWaveOpt opt) {
	unsigned flags = opt.flags;
	if (opt.flags & SAU_WAVE_O_FUNC_SET) {
		uint32_t offset = sauWave_picoeffs[o->opt.wave].phase_adj;
		if (o->opt.func == SAU_WAVE_F_ADAA)
			o->phasor.cycle_phase -= offset;
		o->opt.func = opt.func;
		if (o->opt.func == SAU_WAVE_F_ADAA)
			o->phasor.cycle_phase += offset;
		o->phasor.preinc = (o->opt.func == SAU_WAVE_F_ADAA);
	}
	if (opt.flags & SAU_WAVE_O_WAVE_SET)
		sauWOsc_set_wave(o, opt.wave);
	o->opt.flags = flags;
}

/**
 * Calculate length of wave cycle for \p freq.
 *
 * \return number of samples
 */
static inline uint32_t sauWOsc_cycle_len(sauWOsc *restrict o, float freq) {
	return sau_ftoi(SAU_INV_FREQ(32, o->phasor.coeff * freq));
}

/**
 * Calculate position in wave cycle for \p freq, based on \p pos.
 *
 * \return number of samples
 */
static inline uint32_t sauWOsc_cycle_pos(sauWOsc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = sau_ftoi(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return phs / inc;
}

/**
 * Calculate offset relative to wave cycle for \p freq, based on \p pos.
 *
 * Can be used to reduce time length to something rounder and reduce clicks.
 */
static inline int32_t sauWOsc_cycle_offs(sauWOsc *restrict o,
		float freq, uint32_t pos) {
	uint32_t inc = sau_ftoi(o->phasor.coeff * freq);
	uint32_t phs = inc * pos;
	return (phs - sauWave_SLEN) / inc;
}

/**
 * Wrapper for applying PD synthesis distortion with proper phase offset.
 *
 * A NULL \p cycle_ui32 is allowed, skipping some work and handling phase
 * offsets in a cheaper way.
 */
void sauWOsc_pdist(sauWOsc *restrict o,
		unsigned pdist_fn,
		void *restrict phase_buf,
		uint32_t *restrict cycle_ui32,
		size_t len,
		const float *restrict v_f,
		const float *restrict f_f,
		float f_fval,
		float *restrict p_f,
		float p_fval) {
	uint32_t offset = (o->opt.func == SAU_WAVE_F_ADAA) ?
		sauWave_picoeffs[o->opt.wave].phase_adj :
		0;
	sauPhasor_pdist_fn fn = sauPhasor_get_pdist_fn(pdist_fn);
	if (cycle_ui32) {
		float offset_f = offset * 0x1p-32f;
		p_fval += offset_f;
		if (p_f) for (size_t i = 0; i < len; ++i)
			p_f[i] += offset_f;
		sauPhasor_ui32tof(&o->phasor, phase_buf, len);
		fn(&o->phasor, phase_buf, cycle_ui32, len,
				v_f, f_f, f_fval, p_f, p_fval);
	} else {
		/*
		 * Faster to use in-place int-float-int conversions, than
		 * the internal handling of phase offset as float values.
		 */
		uint32_t *x = phase_buf;
		float *y = phase_buf;
		uint32_t p_i = offset+sau_ftoi(p_fval*0x1p32f);
		if (p_f) {
			sau_phase_nftoui32(p_f, len);
			uint32_t *p_i = (void*)p_f;
			for (size_t i = 0; i < len; ++i)
				y[i] = (x[i]-(offset+p_i[i]))*0x1p-32f;
		} else {
			for (size_t i = 0; i < len; ++i)
				y[i] = (x[i]-p_i)*0x1p-32f;
		}
		fn(&o->phasor, phase_buf, NULL, len,
				v_f, f_f, f_fval, NULL, 0.f);
		if (p_f) {
			uint32_t *p_i = (void*)p_f;
			for (size_t i = 0; i < len; ++i)
				x[i] = sau_ftoi(y[i]*0x1p32f)+(offset+p_i[i]);
		} else {
			for (size_t i = 0; i < len; ++i)
				x[i] = sau_ftoi(y[i]*0x1p32f)+p_i;
		}
	}
}

/*
 * Naive LUTs sauWOsc_run().
 *
 * Uses post-incremented phase each sample.
 */
static void sauWOsc_naive_run(sauWOsc *restrict o,
		void *restrict main_buf, size_t buf_len) {
	float *buf = main_buf;
	const uint32_t *phase_buf = main_buf;
	const float *const lut = sauWave_luts[o->opt.wave];
	for (size_t i = 0; i < buf_len; ++i) {
		buf[i] = sauWave_get_lerp(lut, phase_buf[i]);
	}
}

/*
 * Naive LUTs sauWOsc_naive_run_selfmod().
 *
 * Uses post-incremented phase each sample.
 */
static void sauWOsc_naive_run_selfmod(sauWOsc *restrict o,
		void *restrict main_buf, size_t buf_len,
		const float *restrict pm_abuf) {
	float *buf = main_buf;
	const uint32_t *phase_buf = main_buf;
	const float fb_scale = 0x1p31f * 0.5f; // like level 6 in Yamaha chips
	const float *const lut = sauWave_luts[o->opt.wave];
	for (size_t i = 0; i < buf_len; ++i) {
		float s = buf[i] = sauWave_get_lerp(lut, phase_buf[i]
				+ sau_ftoi(o->filt_s * pm_abuf[i] * fb_scale));
		/*
		 * Suppress ringing. Use 1-zero filter (Tomisawa oscillator)
		 * for classic Yamaha FM synth chip behavior.
		 */
		o->filt_s = s + o->prev_s;
		o->prev_s = s;
	}
}

/* Set up for differentiation (re)start with usable state. */
static void sauWOsc_adaa_reset(sauWOsc *restrict o, uint32_t phase) {
	const float *const lut = sauWave_piluts[o->opt.wave];
	if (o->opt.flags & SAU_OSC_RESET_DIFF) {
		o->prev_phase = phase;
		o->prev_Is = sauWave_get_berp(lut, o->prev_phase);
	}
	o->opt.flags &= ~SAU_OSC_RESET;
}

/**
 * Run for \p buf_len samples, generating output.
 *
 * Uses pre-incremented phase each sample.
 */
static sauMaybeUnused void sauWOsc_run(sauWOsc *restrict o,
		void *restrict main_buf, size_t buf_len) {
	sauPhasor_ftoui32(&o->phasor, main_buf, buf_len);
	if (o->opt.func == SAU_WAVE_F_NAIVE) {
		sauWOsc_naive_run(o, main_buf, buf_len);
		return;
	}
	float *buf = main_buf;
	const uint32_t *phase_buf = main_buf;
	// Higher-quality audio (reduce wave, FM & PM aliasing).
	unsigned wave = o->opt.wave, flags = o->opt.flags;
	const float *const Ilut = sauWave_piluts[wave];
	const float *const lut = sauWave_luts[wave];
	const int32_t lut_offset = sauWave_picoeffs[wave].phase_adj;
	const float diff_scale = sauWave_DVSCALE(wave);
	const float diff_offset = sauWave_DVOFFSET(wave);
	if (buf_len > 0 && flags & SAU_OSC_RESET)
		sauWOsc_adaa_reset(o, phase_buf[0]);
	bool skipped_Is = flags & SAU_OSC_SKIPPED_I;
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t phase = phase_buf[i];
		uint32_t phase_diff = phase - o->prev_phase;
		if (phase_diff + sauWave_SLEN < 2 * sauWave_SLEN) {
			/*
			 * Phase difference in 1 LUT value range. No aliasing,
			 * naive lookup for reliable LFO and phase distortion.
			 */
			s = sauWave_get_berp(lut, phase - lut_offset);
			skipped_Is = true;
		} else if (skipped_Is) {
			double prev_Is = sauWave_get_berp(Ilut, o->prev_phase);
			double Is = sauWave_get_berp(Ilut, phase);
			double x = diff_scale / (int32_t) phase_diff;
			s = (Is - prev_Is) * x + diff_offset;
			o->prev_Is = Is;
			skipped_Is = false;
		} else {
			double Is = sauWave_get_berp(Ilut, phase);
			double x = diff_scale / (int32_t) phase_diff;
			s = (Is - o->prev_Is) * x + diff_offset;
			o->prev_Is = Is;
		}
		o->prev_phase = phase;
		buf[i] = s;
	}
	if (skipped_Is)
		o->opt.flags |= SAU_OSC_SKIPPED_I;
	else
		o->opt.flags &= ~SAU_OSC_SKIPPED_I;
}

/**
 * Run for \p buf_len samples, generating output, with self-modulation.
 *
 * Uses pre-incremented phase each sample.
 */
static void sauWOsc_run_selfmod(sauWOsc *restrict o,
		void *restrict main_buf, size_t buf_len,
		const float *restrict pm_abuf) {
	sauPhasor_ftoui32(&o->phasor, main_buf, buf_len);
	if (o->opt.func == SAU_WAVE_F_NAIVE) {
		sauWOsc_naive_run_selfmod(o, main_buf, buf_len, pm_abuf);
		return;
	}
	float *buf = main_buf;
	const uint32_t *phase_buf = main_buf;
	// Higher-quality audio (reduce wave, FM & PM, feedback aliasing).
	unsigned wave = o->opt.wave, flags = o->opt.flags;
	const float *const Ilut = sauWave_piluts[wave];
	const float *const lut = sauWave_luts[wave];
	const int32_t lut_offset = sauWave_picoeffs[wave].phase_adj;
	const float diff_scale = sauWave_DVSCALE(wave);
	const float diff_offset = sauWave_DVOFFSET(wave);
	const float fb_scale = 0x1p31f; // like level 6 in Yamaha chips
	if (buf_len > 0 && flags & SAU_OSC_RESET)
		sauWOsc_adaa_reset(o, phase_buf[0]);
	bool skipped_Is = flags & SAU_OSC_SKIPPED_I;
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t phase = phase_buf[i] +
			sau_ftoi(o->filt_s * pm_abuf[i] * fb_scale);
		uint32_t phase_diff = phase - o->prev_phase;
		if (phase_diff + sauWave_SLEN < 2 * sauWave_SLEN) {
			/*
			 * Phase difference in 1 LUT value range. No aliasing,
			 * naive lookup for reliable LFO and phase distortion.
			 */
			s = sauWave_get_berp(lut, phase - lut_offset);
			skipped_Is = true;
		} else if (skipped_Is) {
			double prev_Is = sauWave_get_berp(Ilut, o->prev_phase);
			double Is = sauWave_get_berp(Ilut, phase);
			double x = diff_scale / (int32_t) phase_diff;
			s = (Is - prev_Is) * x + diff_offset;
			o->prev_Is = Is;
			skipped_Is = false;
		} else {
			double Is = sauWave_get_berp(Ilut, phase);
			double x = diff_scale / (int32_t) phase_diff;
			s = (Is - o->prev_Is) * x + diff_offset;
			o->prev_Is = Is;
		}
		o->prev_phase = phase;
		buf[i] = s;
		/*
		 * Suppress ringing. 1-pole filter is a little better than
		 * 1-zero. (Yamaha's synths and Tomisawa design use 1-zero.)
		 * The differentiation above is like adding an extra 1-zero.
		 */
		o->filt_s = (o->filt_s + s) * 0.5f;
	}
	if (skipped_Is)
		o->opt.flags |= SAU_OSC_SKIPPED_I;
	else
		o->opt.flags &= ~SAU_OSC_SKIPPED_I;
}
