/* sgensys: Value ramp module.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
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

#include "ramp.h"
#include "math.h"

const char *const SGS_Ramp_names[SGS_RAMP_TYPES + 1] = {
	"state",
	"lin",
	"exp",
	"log",
	NULL
};

static void fill_state(float *restrict buf, uint32_t len,
		float s0) {
	uint32_t i;
	for (i = 0; i < len; ++i)
		buf[i] = s0;
}

static void fill_lin(float *restrict buf, uint32_t len,
		float s0, float goal, uint32_t pos, double inv_time) {
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		(*buf++) = s0 + (goal - s0) * (i * inv_time);
	}
}

/*
 * Ear-tuned polynomial, designed to sound natural.
 */
static void fill_exp(float *restrict buf, uint32_t len,
		float s0, float goal, uint32_t pos, double inv_time) {
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		double mod = 1.f - i * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
		      (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		(*buf++) = goal + (s0 - goal) * mod;
	}
}

/*
 * Ear-tuned polynomial, designed to sound natural.
 */
static void fill_log(float *restrict buf, uint32_t len,
		float s0, float goal, uint32_t pos, double inv_time) {
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		double mod = i * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
		      (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		(*buf++) = s0 + (goal - s0) * mod;
	}
}

/**
 * Fill \p buf with \p buf_len values, shaped according to the
 * ramp and its attributes.
 *
 * \return true until goal reached
 */
bool SGS_Ramp_run(SGS_Ramp *restrict o, uint32_t srate,
		float *restrict buf, uint32_t buf_len, float s0) {
	uint32_t time = SGS_MS_IN_SAMPLES(o->time_ms, srate);
	uint32_t len, fill_len;
	double inv_time;
	inv_time = 1.f / time;
	len = time - o->pos;
	if (len > buf_len) {
		len = buf_len;
		fill_len = 0;
	} else {
		fill_len = buf_len - len;
	}
	switch (o->type) {
	case SGS_RAMP_STATE:
		fill_state(buf, len, s0);
		break;
	case SGS_RAMP_LIN:
		fill_lin(buf, len, s0, o->goal, o->pos, inv_time);
		break;
	case SGS_RAMP_EXP:
		fill_exp(buf, len, s0, o->goal, o->pos, inv_time);
		break;
	case SGS_RAMP_LOG:
		fill_log(buf, len, s0, o->goal, o->pos, inv_time);
		break;
	}
	o->pos += len;
	if (o->pos == time) {
		/*
		 * Set the remaining values, if any, using the goal.
		 */
		fill_state(buf + len, fill_len, o->goal);
		return false;
	}
	return true;
}
