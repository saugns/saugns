/* sgensys: Value ramp module.
 * Copyright (c) 2011-2013, 2017-2020 Joel K. Pettersson
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
	"hold",
	"lin",
	"exp",
	"log",
	NULL
};

const SGS_Ramp_fill_f SGS_Ramp_fill_funcs[SGS_RAMP_TYPES] = {
	SGS_Ramp_fill_hold,
	SGS_Ramp_fill_lin,
	SGS_Ramp_fill_exp,
	SGS_Ramp_fill_log,
};

/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
void SGS_Ramp_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt SGS__maybe_unused,
		uint32_t pos SGS__maybe_unused, uint32_t time SGS__maybe_unused) {
	uint32_t i;
	for (i = 0; i < len; ++i)
		buf[i] = v0;
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void SGS_Ramp_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const float inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		(*buf++) = v0 + (vt - v0) * (i * inv_time);
	}
}

/**
 * Fill \p buf with \p len values along an exponential trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural.
 * (Unlike a real exponential curve, it has a definite beginning
 * and end. It is symmetric to the corresponding logarithmic curve.)
 */
void SGS_Ramp_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const float inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		float mod = 1.f - i * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
			(mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		(*buf++) = vt + (v0 - vt) * mod;
	}
}

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural.
 * (Unlike a real logarithmic curve, it has a definite beginning
 * and end. It is symmetric to the corresponding exponential curve.)
 */
void SGS_Ramp_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const float inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		float mod = i * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
			(mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		(*buf++) = v0 + (vt - v0) * mod;
	}
}

/**
 * Set instance to default values.
 *
 * (This does not include values specific to a particular parameter.)
 */
void SGS_Ramp_reset(SGS_Ramp *restrict o) {
	*o = (SGS_Ramp){0};
	o->type = SGS_RAMP_LIN; // default if goal enabled
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void SGS_Ramp_copy(SGS_Ramp *restrict o,
		const SGS_Ramp *restrict src) {
	uint8_t mask = 0;
	if ((src->flags & SGS_RAMPP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SGS_RAMPP_STATE
			| SGS_RAMPP_STATE_RATIO;
	}
	if ((src->flags & SGS_RAMPP_GOAL) != 0) {
		o->vt = src->vt;
		o->time_ms = src->time_ms;
		o->type = src->type;
		mask |= SGS_RAMPP_GOAL
			| SGS_RAMPP_GOAL_RATIO
			| SGS_RAMPP_TIME;
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/*
 * Fill \p buf from \p from to \p to - 1 with copies of \a v0.
 *
 * If the SGS_RAMPP_STATE_RATIO flag is set, multiply using \p mulbuf
 * for each value.
 */
static void fill_state(SGS_Ramp *restrict o, float *restrict buf,
		uint32_t from, uint32_t to,
		const float *restrict mulbuf) {
	if ((o->flags & SGS_RAMPP_STATE_RATIO) != 0) {
		for (uint32_t i = from; i < to; ++i)
			buf[i] = o->v0 * mulbuf[i];
	} else {
		for (uint32_t i = from; i < to; ++i)
			buf[i] = o->v0;
	}
}

/**
 * Fill \p buf with \p buf_len values for the ramp.
 * If a goal is used, it will be ramped towards; when
 * reached, the goal \a vt will become the new state \a v0.
 *
 * If the initial and/or target value is a ratio,
 * \p mulbuf is used for a sequence of value multipliers.
 *
 * \return true if ramp target not yet reached
 */
bool SGS_Ramp_run(SGS_Ramp *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf) {
	if (!(o->flags & SGS_RAMPP_GOAL)) {
		fill_state(o, buf, 0, buf_len, mulbuf);
		return false;
	}
	uint32_t time = SGS_MS_IN_SAMPLES(o->time_ms, srate);
	if ((o->flags & SGS_RAMPP_GOAL_RATIO) != 0) {
		if (!(o->flags & SGS_RAMPP_STATE_RATIO)) {
			// divide v0 and enable ratio to match vt
			o->v0 /= mulbuf[0];
			o->flags |= SGS_RAMPP_STATE_RATIO;
		}
	} else {
		if ((o->flags & SGS_RAMPP_STATE_RATIO) != 0) {
			// multiply v0 and disable ratio to match vt
			o->v0 *= mulbuf[0];
			o->flags &= ~SGS_RAMPP_STATE_RATIO;
		}
	}
	uint32_t len;
	len = time - *pos;
	if (len > buf_len) len = buf_len;
	SGS_Ramp_fill_funcs[o->type](buf, len, o->v0, o->vt, *pos, time);
	if ((o->flags & SGS_RAMPP_GOAL_RATIO) != 0) {
		for (uint32_t i = 0; i < len; ++i)
			buf[i] *= mulbuf[i];
	}
	*pos += len;
	if (*pos == time) {
		/*
		 * Goal reached; turn into new initial value.
		 * Fill any remaining buffer values using it.
		 */
		o->v0 = o->vt;
		o->flags &= ~(SGS_RAMPP_GOAL | SGS_RAMPP_GOAL_RATIO);
		fill_state(o, buf, len, buf_len, mulbuf);
		return false;
	}
	return true;
}
