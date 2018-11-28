/* sgensys: Value slope module.
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

#include "slope.h"
#include "math.h"

const char *const SGS_Slope_names[SGS_SLOPE_TYPES + 1] = {
	"hold",
	"lin",
	"exp",
	"log",
	NULL
};

const SGS_SlopeFill_f SGS_Slope_fills[SGS_SLOPE_TYPES] = {
	SGS_Slope_fill_hold,
	SGS_Slope_fill_lin,
	SGS_Slope_fill_exp,
	SGS_Slope_fill_log,
};

/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
void SGS_Slope_fill_hold(float *restrict buf, uint32_t len,
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
void SGS_Slope_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const double inv_time = 1.f / time;
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
void SGS_Slope_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const double inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		double mod = 1.f - i * inv_time,
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
void SGS_Slope_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const double inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		double mod = i * inv_time,
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
void SGS_Slope_reset(SGS_Slope *restrict o) {
	*o = (SGS_Slope){0};
	o->slope = SGS_SLOPE_LIN; // default if slope enabled
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void SGS_Slope_copy(SGS_Slope *restrict o,
		const SGS_Slope *restrict src) {
	uint8_t mask = 0;
	if ((src->flags & SGS_SLP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SGS_SLP_STATE | SGS_SLP_STATE_RATIO;
	}
	if ((src->flags & SGS_SLP_SLOPE) != 0) {
		o->vt = src->vt;
		o->time_ms = src->time_ms;
		o->slope = src->slope;
		mask |= SGS_SLP_SLOPE | SGS_SLP_SLOPE_RATIO;
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/*
 * Fill \p buf from \p from to \p to - 1 with copies of \a v0.
 *
 * If the SGS_SLP_STATE_RATIO flag is set, multiply using \p mulbuf
 * for each value.
 */
static void fill_state(SGS_Slope *restrict o, float *restrict buf,
		uint32_t from, uint32_t to,
		const float *restrict mulbuf) {
	if ((o->flags & SGS_SLP_STATE_RATIO) != 0) {
		for (uint32_t i = from; i < to; ++i)
			buf[i] = o->v0 * mulbuf[i];
	} else {
		for (uint32_t i = from; i < to; ++i)
			buf[i] = o->v0;
	}
}

/**
 * Fill \p buf with \p buf_len values for the parameter.
 * If a slope is used, it will be applied; when elapsed,
 * the target value will become the new value.
 * If the initial and/or target value is a ratio,
 * \p mulbuf is used for a sequence of value multipliers.
 *
 * \return true if slope target not yet reached
 */
bool SGS_Slope_run(SGS_Slope *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf) {
	if (!(o->flags & SGS_SLP_SLOPE)) {
		fill_state(o, buf, 0, buf_len, mulbuf);
		return false;
	}
	uint32_t time = SGS_MS_TO_SRT(o->time_ms, srate);
	if ((o->flags & SGS_SLP_SLOPE_RATIO) != 0) {
		if (!(o->flags & SGS_SLP_STATE_RATIO)) {
			// divide v0 and enable ratio to match slope and vt
			o->v0 /= mulbuf[0];
			o->flags |= SGS_SLP_STATE_RATIO;
		}
	} else {
		if ((o->flags & SGS_SLP_STATE_RATIO) != 0) {
			// multiply v0 and disable ratio to match slope and vt
			o->v0 *= mulbuf[0];
			o->flags &= ~SGS_SLP_STATE_RATIO;
		}
	}
	uint32_t len;
	len = time - *pos;
	if (len > buf_len) len = buf_len;
	SGS_Slope_fills[o->slope](buf, len, o->v0, o->vt, *pos, time);
	if ((o->flags & SGS_SLP_SLOPE_RATIO) != 0) {
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
		o->flags &= ~(SGS_SLP_SLOPE | SGS_SLP_SLOPE_RATIO);
		fill_state(o, buf, len, buf_len, mulbuf);
		return false;
	}
	return true;
}
