/* SAU library: Audio generator module.
 * Copyright (c) 2011-2012, 2017-2026 Joel K. Pettersson
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
#include "../math.h"

/*
 * One-pole filters from my older fluffDSP project, and similar code.
 */

/** Run exponential averaging for 1 sample, updating and returning state. */
#define RC_AVG_NEXT(state, in, coeff) \
	((state) = (in) + (coeff)*((state)-(in)))

/** Run exponential decay with reset condition and level. */
#define RC_DECAY_NEXT(state, cond, reset, coeff) \
	((state) = (cond) ? (reset) : ((coeff)*(state)))

/** Run HPF for 1 sample, updating and returning state. */
#define RC_HPF_NEXT(state, in, in_prev, coeff) \
	((state) = (in) - (in_prev) + (coeff)*(state))

/** Run zero-attack envelope for 1 sample, updating and returning state. */
#define RC_ZAENV_NEXT(state, in, coeff) \
	((state) = (in) + (((state)-(in)) > 0.f) ? (coeff)*((state)-(in)) : 0.f)

/** Run zero-release envelope for 1 sample, updating and returning state. */
#define RC_ZRENV_NEXT(state, in, coeff) \
	((state) = (in) + (((state)-(in)) < 0.f) ? (coeff)*((state)-(in)) : 0.f)

/** Run attack-release envelope for 1 sample, updating and returning state. */
#define RC_ARENV_NEXT(state, in, a_coeff, r_coeff) \
	((state) = (in) + ((((state)-(in)) < 0.f) ? (a_coeff) : (r_coeff)) * \
	                  ((state)-(in)))

/*
 * Code to apply LPF & HPF filters...
 */

struct FilterCoeff {
	float a[2];
};

struct Filter {
	float t[2];
};

static inline void sau_set_filt_1p_time(struct FilterCoeff *restrict c,
		double time_ms, uint32_t srate) {
	c->a[0] = sau_rc_time_coeff(time_ms, srate);
}

static inline uint32_t sau_set_filt_1p(struct FilterCoeff *restrict c,
		double freq, uint32_t srate) {
	c->a[0] = sau_rc_freq_coeff(freq, srate);
	double time = srate / freq;
	return sau_dtoi(time);
}

static void sau_arr_lpf_1p(float *restrict x, size_t n,
		struct Filter *restrict s,
		const struct FilterCoeff *restrict c) {
	for (size_t i=0; i<n; ++i) {
		s->t[0] = x[i] + c->a[0] * (s->t[0] - x[i]);
		x[i] = s->t[0];
	}
}

static void sau_arr_hpf_1p(float *restrict x, size_t n,
		struct Filter *restrict s,
		const struct FilterCoeff *restrict c) {
	for (size_t i=0; i<n; ++i) {
		s->t[0] = x[i] - s->t[1] + c->a[0] * s->t[0];
		s->t[1] = x[i];
		x[i] = s->t[0];
	}
}
