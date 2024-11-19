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
#include "../wave.h"
#include "../math.h"

/*
 * Use pre-integrated LUTs ("PILUTs")?
 *
 * Turn off to use the raw naive LUTs,
 * kept for testing/"viewing" of them.
 */
#define USE_PILUT 1

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

typedef struct sauWOsc {
	sauPhasor phasor;
	uint8_t wave;
	uint8_t flags;
#if USE_PILUT
	uint32_t prev_phase;
	double prev_Is;
#else
	float prev_s;
#endif
	float fb_s;
} sauWOsc;

/**
 * Initialize instance for use.
 */
static inline void sau_init_WOsc(sauWOsc *restrict o, uint32_t srate) {
	*o = (sauWOsc){
#if USE_PILUT
		.phasor = (sauPhasor){
			.phase = sauWave_picoeffs[SAU_WAVE_N_sin].phase_adj,
			.coeff = sauPhasor_COEFF(srate),
		},
#else
		.phasor = (sauPhasor){
			.phase = 0,
			.coeff = sauPhasor_COEFF(srate),
		},
#endif
		.wave = SAU_WAVE_N_sin,
		.flags = SAU_OSC_RESET,
	};
}

static inline void sauWOsc_set_phase(sauWOsc *restrict o, uint32_t phase) {
#if USE_PILUT
	o->phasor.phase = phase + sauWave_picoeffs[o->wave].phase_adj;
#else
	o->phasor.phase = phase;
#endif
}

static inline void sauWOsc_set_wave(sauWOsc *restrict o, uint8_t wave) {
#if USE_PILUT
	uint32_t old_offset = sauWave_picoeffs[o->wave].phase_adj;
	uint32_t offset = sauWave_picoeffs[wave].phase_adj;
	o->phasor.phase += offset - old_offset;
	o->wave = wave;
	o->flags |= SAU_OSC_RESET_DIFF;
#else
	o->wave = wave;
#endif
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

#if !USE_PILUT
# define P(inc, ofs) ofs + o->phase; (o->phase += inc)     /* post-increment */
#else
# define P(inc, ofs) ofs + (o->phase += inc)               /* pre-increment */
#endif

/**
 * Fill phase-value buffer for use with sauWOsc_run().
 */
static sauMaybeUnused void sauPhasor_fill(sauPhasor *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict freq_f,
		const float *restrict pm_f) {
#define FILL(FREQ, PM_IN) \
	for (size_t i = 0; i < buf_len; ++i) { \
		phase_ui32[i] = P(sau_ftoi(o->coeff * (FREQ)), (PM_IN)); \
	} \
/**/
	if (!pm_f) FILL(freq_f[i], 0)
	else       FILL(freq_f[i], sau_ftoi(pm_f[i] * 0x1p31f))
#undef FILL
}

#undef P /* done */

/*
 * Phase distortion: cycle length. Below 1 zooms in resulting in jagged shapes,
 * above 1 zooms out adding padding (the amplitude at the cycle beginning/end).
 */
static sauMaybeUnused void sauWOsc_dist_length(sauWOsc *restrict o,
		uint32_t *restrict phase_ui32,
		size_t buf_len,
		const float *restrict pd_f) {
#if USE_PILUT
	int32_t c = sauWave_picoeffs[o->wave].phase_adj;
#else
	int32_t c = 0;
#endif
	for (size_t i = 0; i < buf_len; ++i) {
		uint32_t p_i = phase_ui32[i] - c;
		float x = sau_fclampf(p_i * pd_f[i], -0x1p32f, 0x1p32f);
		phase_ui32[i] = sau_ftoi(x) + c;
	}
}

#if !USE_PILUT
/*
 * Naive LUTs sauWOsc_run().
 *
 * Uses post-incremented phase each sample.
 */
static void sauWOsc_naive_run(sauWOsc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
	const float *const lut = sauWave_luts[o->wave];
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
	const float *const lut = sauWave_luts[o->wave];
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
#endif

#if USE_PILUT
/* Set up for differentiation (re)start with usable state. */
static void sauWOsc_reset(sauWOsc *restrict o, uint32_t phase) {
	const float *const lut = sauWave_piluts[o->wave];
	if (o->flags & SAU_OSC_RESET_DIFF) {
		o->prev_Is = sauWave_get_herp(lut, phase);
		o->prev_phase = phase;
	}
	o->flags &= ~SAU_OSC_RESET;
}
#endif

/**
 * Run for \p buf_len samples, generating output.
 *
 * Uses pre-incremented phase each sample.
 */
static sauMaybeUnused void sauWOsc_run(sauWOsc *restrict o,
		float *restrict buf, size_t buf_len,
		const uint32_t *restrict phase_buf) {
#if USE_PILUT // higher-quality audio (reduce wave, FM & PM aliasing)
	const float *const lut = sauWave_piluts[o->wave];
	const float *const lut_backup = sauWave_luts[o->wave];
	const int32_t lut_offset = sauWave_picoeffs[o->wave].phase_adj;
	const float diff_scale = sauWave_DVSCALE(o->wave);
	const float diff_offset = sauWave_DVOFFSET(o->wave);
	if (buf_len > 0 && o->flags & SAU_OSC_RESET)
		sauWOsc_reset(o, phase_buf[0]);
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t phase = phase_buf[i];
		int32_t phase_diff = phase - o->prev_phase;
		if (phase_diff == 0) {
			/*
			 * Use instead of "s = o->prev_s;" to avoid LF noise.
			 * This matters for phase distortion uses especially.
			 */
			s = sauWave_get_lerp(lut_backup, phase - lut_offset);
		} else {
			double Is = sauWave_get_herp(lut, phase);
			double x = (diff_scale / phase_diff);
			s = (Is - o->prev_Is) * x + diff_offset;
			o->prev_Is = Is;
			o->prev_phase = phase;
		}
		buf[i] = s;
	}
#else // test naive LUT
	sauWOsc_naive_run(o, buf, buf_len, phase_buf);
#endif
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
#if USE_PILUT // higher-quality audio (reduce wave, FM & PM, feedback aliasing)
	const float *const lut = sauWave_piluts[o->wave];
	const float *const lut_backup = sauWave_luts[o->wave];
	const int32_t lut_offset = sauWave_picoeffs[o->wave].phase_adj;
	const float diff_scale = sauWave_DVSCALE(o->wave);
	const float diff_offset = sauWave_DVOFFSET(o->wave);
	const float fb_scale = 0x1p31f; // like level 6 in Yamaha chips
	if (buf_len > 0 && o->flags & SAU_OSC_RESET)
		sauWOsc_reset(o, phase_buf[0]);
	for (size_t i = 0; i < buf_len; ++i) {
		float s;
		uint32_t phase = phase_buf[i] +
			sau_ftoi(o->fb_s * pm_abuf[i] * fb_scale);
		int32_t phase_diff = phase - o->prev_phase;
		if (phase_diff == 0) {
			/*
			 * Use instead of "s = o->prev_s;" to avoid LF noise.
			 * This matters for phase distortion uses especially.
			 */
			s = sauWave_get_lerp(lut_backup, phase - lut_offset);
		} else {
			double Is = sauWave_get_herp(lut, phase);
			double x = (diff_scale / phase_diff);
			s = (Is - o->prev_Is) * x + diff_offset;
			o->prev_Is = Is;
			o->prev_phase = phase;
		}
		buf[i] = s;
		/*
		 * Suppress ringing. 1-pole filter is a little better than
		 * 1-zero. (Yamaha's synths and Tomisawa design use 1-zero.)
		 * The differentiation above is like adding an extra 1-zero.
		 */
		o->fb_s = (o->fb_s + s) * 0.5f;
	}
#else // test naive LUT
	sauWOsc_naive_run_selfmod(o, buf, buf_len, phase_buf, pm_abuf);
#endif
}
