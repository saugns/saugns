/* SAU library: Wave oscillator implementation.
 * Copyright (c) 2011, 2017-2024 Joel K. Pettersson
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
#include <sau/wave.h>
#include <sau/math.h>

/**
 * Calculate the coefficent, based on the sample rate, used for
 * the per-sample phase by multiplying with the frequency used.
 */
#define sauPhasor_COEFF(srate) SAU_INV_FREQ(32, srate)

typedef struct sauPhasor {
	uint32_t phase;
	float coeff;
} sauPhasor;

#define SAU_OSC_RESET_DIFF  (1<<0)
#define SAU_OSC_RESET       ((1<<1) - 1)
#define SAU_OSC_SKIPPED_I   (1<<1)

typedef struct sauWOsc {
	sauPhasor phasor;
	sauWaveOpt opt;
	uint32_t prev_phase;
	double prev_Is;
	float prev_s;
	float fb_s;
} sauWOsc;

/**
 * Initialize instance for use.
 */
static inline void sau_init_WOsc(sauWOsc *restrict o, uint32_t srate) {
	*o = (sauWOsc){
		.phasor = (sauPhasor){
			.phase = 0,
			.coeff = sauPhasor_COEFF(srate),
		},
		.opt.wave = SAU_WAVE_N_sin,
		.opt.func = SAU_WAVE_F_NAIVE,
		.opt.flags = 0,
	};
}

static inline void sauWOsc_set_phase(sauWOsc *restrict o, uint32_t phase) {
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		phase += sauWave_picoeffs[o->opt.wave].phase_adj;
	}
	o->phasor.phase = phase;
}

static inline void sauWOsc_set_wave(sauWOsc *restrict o, uint8_t wave) {
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		uint32_t old_offset = sauWave_picoeffs[o->opt.wave].phase_adj;
		uint32_t offset = sauWave_picoeffs[wave].phase_adj;
		o->phasor.phase += offset - old_offset;
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
		if (o->opt.func == SAU_WAVE_F_ADAA) o->phasor.phase -= offset;
		o->opt.func = opt.func;
		if (o->opt.func == SAU_WAVE_F_ADAA) o->phasor.phase += offset;
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
 * Fill phase-value buffer for use with sauWOsc_run().
 */
static sauMaybeUnused void sauWOsc_fill(sauWOsc *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f) {
#define PRE(inc, ofs)  ofs + (o->phasor.phase += inc) // be ahead one sample
#define POST(inc, ofs) ofs + o->phasor.phase; (o->phasor.phase += inc)
#define FILL(FREQ, P, PM_IN) \
	for (size_t i = 0; i < buf_len; ++i) { \
		phase_ui32[i] = \
			P(sau_ftoi(o->phasor.coeff * (FREQ)), (PM_IN)); \
	} \
/**/
	if (o->opt.func == SAU_WAVE_F_ADAA) { // compensate for 1-sample off
		if (!pm_f) FILL(freq_f[i], PRE, 0)
		else       FILL(freq_f[i], PRE, sau_ftoi(pm_f[i] * 0x1p31f))
	} else {
		if (!pm_f) FILL(freq_f[i], POST, 0)
		else       FILL(freq_f[i], POST, sau_ftoi(pm_f[i] * 0x1p31f))
	}
#undef PRE
#undef POST
#undef FILL
}

typedef void (*sauWOsc_pdist_f)(sauWOsc *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f,
		float f_mul);

/*
 * PD get/set phase macros, for use inside loops.
 */
#define PD_GET_SUBF(x, phase_ui32, offset, f_mul) \
	uint32_t p_i = phase_ui32 - (offset); \
	float x = p_i; \
	x *= f_mul; \
	int32_t f_adj = floorf(x); \
	x -= f_adj; \
//
#define PD_SET_SUBF(x, phase_ui32, offset, f_mul_inv) \
	x += f_adj; \
	x *= f_mul_inv; \
	phase_ui32 = sau_ftoi(x) + (offset); \
//

/*
 * Generate a simple version of a PD loop, with no extra frequency control,
 * having \p VAL_EXPR at its heart.
 */
#define PD_SIMPLE(offset, VAL_EXPR) PD_FMUL(offset, 1, VAL_EXPR)

/*
 * Generate a a PD loop with frequency multiplier as for zoom-PDs,
 * having \p VAL_EXPR at its heart.
 */
#define PD_FMUL(offset, f_mul, VAL_EXPR) \
	for (size_t i = 0; i < buf_len; ++i) {                   \
		uint32_t p_i = phase_ui32[i] - (offset);         \
		float x = p_i;                                   \
		x = (VAL_EXPR);                                  \
		phase_ui32[i] = sau_ftoi(x * f_mul) + (offset);  \
	}

/*
 * Generate a PD loop with a constant value for subfrequency control,
 * having \p VAL_EXPR at its heart.
 */
#define PD_SUBF_CONST(offset, f_mul, VAL_EXPR) \
	f_mul *= 0x1p-32f; /* factor out, use for scaling */     \
	const float f_mul_inv = 1.f / f_mul;                     \
	for (size_t i = 0; i < buf_len; ++i) {                   \
		PD_GET_SUBF(x, phase_ui32[i], offset, f_mul)     \
		x = (VAL_EXPR);                                  \
		PD_SET_SUBF(x, phase_ui32[i], offset, f_mul_inv) \
	}

/*
 * Phase distortion: cycle length. Below 1 zooms in resulting in jagged shapes,
 * above 1 zooms out adding padding (the amplitude at the cycle beginning/end).
 */
static sauMaybeUnused void
sauWOsc_pdist_pulwm_mul(sauWOsc *restrict o sauMaybeUnused,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f,
		float f_mul) {
	int32_t c = 0;
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		c = sauWave_picoeffs[o->opt.wave].phase_adj;
	}
	if (f_mul == 1.f) {
		PD_SIMPLE(c, sau_fclampf(p_i * pd_f[i], -0x1p32f, 0x1p32f))
	} else {
		PD_FMUL(c, f_mul,
		        sau_fclampf(p_i * pd_f[i], -0x1p32f, 0x1p32f))
	}
}

/*
 * Phase distortion: duty cycle. Below 1 zooms out adding padding,
 * above 1 zooms in resulting in jagged shapes.
 */
static sauMaybeUnused void
sauWOsc_pdist_pulwm_div(sauWOsc *restrict o sauMaybeUnused,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f,
		float f_mul) {
	int32_t c = 0;
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		c = sauWave_picoeffs[o->opt.wave].phase_adj;
	}
	if (f_mul == 1.f) {
		PD_SIMPLE(c, sau_fclampf(p_i / pd_f[i], -0x1p32f, 0x1p32f))
	} else {
		PD_FMUL(c, f_mul,
		        sau_fclampf(p_i / pd_f[i], -0x1p32f, 0x1p32f))
	}
}

/*
 * Phase distortion: hold from beginning/end for part of a cycle.
 * Positive values hold forwards, negative values hold backwards.
 */
static sauMaybeUnused void
sauWOsc_pdist_hold(sauWOsc *restrict o sauMaybeUnused,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f,
		float f_mul) {
	int32_t c = 0;
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		c = sauWave_picoeffs[o->opt.wave].phase_adj;
	}
	if (f_mul == 1.f) {
		PD_SIMPLE(c, sau_pdist_hold(x, pd_f[i], 0x1p32f))
	} else {
		PD_SUBF_CONST(c, f_mul, sau_pdist_hold(x, pd_f[i], 1))
	}
}

/*
 * Phase distortion: half-cycle width a.k.a. size proportion of each half.
 */
static sauMaybeUnused void
sauWOsc_pdist_halfx(sauWOsc *restrict o sauMaybeUnused,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f,
		float f_mul) {
	int32_t c = 0;
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		c = sauWave_picoeffs[o->opt.wave].phase_adj;
	}
	if (f_mul == 1.f) {
		PD_SIMPLE(c, sau_pdist_halfx(x, pd_f[i], 0x1p32f))
	} else {
		PD_SUBF_CONST(c, f_mul, sau_pdist_halfx(x, pd_f[i], 1))
	}
}

/*
 * Phase distortion: half-cycle height a.k.a. change proportion of each half.
 */
static sauMaybeUnused void
sauWOsc_pdist_halfy(sauWOsc *restrict o sauMaybeUnused,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f,
		float f_mul) {
	int32_t c = 0;
	if (o->opt.func == SAU_WAVE_F_ADAA) {
		c = sauWave_picoeffs[o->opt.wave].phase_adj;
	}
	if (f_mul == 1.f) {
		PD_SIMPLE(c, sau_pdist_halfy(x, pd_f[i], 0x1p32f))
	} else {
		PD_SUBF_CONST(c, f_mul, sau_pdist_halfy(x, pd_f[i], 1))
	}
}

#undef PD_GET_SUBF
#undef PD_SET_SUBF
#undef PD_SIMPLE
#undef PD_FMUL
#undef PD_SUBF_CONST

static inline sauWOsc_pdist_f sauWOsc_get_pdist_f(unsigned func) {
	switch (func) {
	default:        return NULL;
	case SAU_PPD_C: return sauWOsc_pdist_pulwm_mul;
	case SAU_PPD_D: return sauWOsc_pdist_pulwm_div;
	case SAU_PPD_H: return sauWOsc_pdist_hold;
	case SAU_PPD_X: return sauWOsc_pdist_halfx;
	case SAU_PPD_Y: return sauWOsc_pdist_halfy;
	}
}

/*
 * Naive LUTs sauWOsc_run().
 *
 * Uses post-incremented phase each sample.
 */
static void sauWOsc_naive_run(sauWOsc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
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
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf,
		const float *restrict pm_abuf) {
	const float fb_scale = 0x1p31f * 0.5f; // like level 6 in Yamaha chips
	const float *const lut = sauWave_luts[o->opt.wave];
	for (size_t i = 0; i < buf_len; ++i) {
		float s = buf[i] = sauWave_get_lerp(lut, phase_buf[i]
				+ sau_ftoi(o->fb_s * pm_abuf[i] * fb_scale));
		/*
		 * Suppress ringing. 1-pole filter is a little better than
		 * 1-zero. (Yamaha's synths and Tomisawa design use 1-zero.)
		 * Combine the two to dampen enough given no anti-aliasing.
		 */
		o->fb_s = (o->fb_s + s + o->prev_s) * 0.5f;
		o->prev_s = s;
	}
}

/* Set up for differentiation (re)start with usable state. */
static void sauWOsc_adaa_reset(sauWOsc *restrict o, uint32_t phase) {
	const float *const lut = sauWave_piluts[o->opt.wave];
	if (o->opt.flags & SAU_OSC_RESET_DIFF) {
		o->prev_Is = sauWave_get_berp(lut, phase);
		o->prev_phase = phase;
	}
	o->opt.flags &= ~SAU_OSC_RESET;
}

/**
 * Run for \p buf_len samples, generating output.
 *
 * Uses pre-incremented phase each sample.
 */
static sauMaybeUnused void sauWOsc_run(sauWOsc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
	if (o->opt.func == SAU_WAVE_F_NAIVE) {
		sauWOsc_naive_run(o, buf, buf_len, phase_buf);
		return;
	}
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
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf,
		const float *restrict pm_abuf) {
	if (o->opt.func == SAU_WAVE_F_NAIVE) {
		sauWOsc_naive_run_selfmod(o, buf, buf_len, phase_buf, pm_abuf);
		return;
	}
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
			sau_ftoi(o->fb_s * pm_abuf[i] * fb_scale);
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
		o->fb_s = (o->fb_s + s) * 0.5f;
	}
	if (skipped_Is)
		o->opt.flags |= SAU_OSC_SKIPPED_I;
	else
		o->opt.flags &= ~SAU_OSC_SKIPPED_I;
}
