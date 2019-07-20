/* saugns: Value ramp module.
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

const char *const SAU_RampCurve_names[SAU_RAC_TYPES + 1] = {
	"hold",
	"lin",
	"exp",
	"log",
	"esd",
	"lsd",
	NULL
};

const SAU_RampCurve_f SAU_RampCurve_funcs[SAU_RAC_TYPES] = {
	SAU_RampCurve_hold,
	SAU_RampCurve_lin,
	SAU_RampCurve_exp,
	SAU_RampCurve_log,
	SAU_RampCurve_esd,
	SAU_RampCurve_lsd,
};

/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
void SAU_RampCurve_hold(float *restrict buf, uint32_t len,
		float v0, float vt sauMaybeUnused,
		uint32_t pos sauMaybeUnused, uint32_t time sauMaybeUnused) {
	uint32_t i;
	for (i = 0; i < len; ++i)
		buf[i] = v0;
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void SAU_RampCurve_lin(float *restrict buf, uint32_t len,
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
 * Unlike a real exponential curve, it has a definite beginning
 * and end. (Uses one of 'esd' or 'lsd', depending on whether
 * the curve rises or falls.)
 */
void SAU_RampCurve_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	(v0 > vt ?
		SAU_RampCurve_esd :
		SAU_RampCurve_lsd)(buf, len, v0, vt, pos, time);
}

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Unlike a real "log(1 + x)" curve, it has a definite beginning
 * and end. (Uses one of 'esd' or 'lsd', depending on whether
 * the curve rises or falls.)
 */
void SAU_RampCurve_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	(v0 < vt ?
		SAU_RampCurve_esd :
		SAU_RampCurve_lsd)(buf, len, v0, vt, pos, time);
}

/**
 * Fill \p buf with \p len values along a trajectory which
 * exponentially saturates and decays (like a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural,
 * and symmetric to the "opposite" 'lsd' type.
 */
void SAU_RampCurve_esd(float *restrict buf, uint32_t len,
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
 * Fill \p buf with \p len values along a trajectory which
 * logarithmically saturates and decays (opposite of a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural,
 * and symmetric to the "opposite" 'esd' type.
 */
void SAU_RampCurve_lsd(float *restrict buf, uint32_t len,
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
void SAU_Ramp_reset(SAU_Ramp *restrict o) {
	*o = (SAU_Ramp){0};
	o->curve = SAU_RAC_LIN; // default if curve enabled
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void SAU_Ramp_copy(SAU_Ramp *restrict o,
		const SAU_Ramp *restrict src) {
	uint8_t mask = 0;
	if ((src->flags & SAU_RAMP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SAU_RAMP_STATE | SAU_RAMP_STATE_RATIO;
	}
	if ((src->flags & SAU_RAMP_CURVE) != 0) {
		o->vt = src->vt;
		o->time_ms = src->time_ms;
		o->curve = src->curve;
		mask |= SAU_RAMP_CURVE | SAU_RAMP_CURVE_RATIO;
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/*
 * Fill \p buf from \p from to \p to - 1 with copies of \a v0.
 *
 * If the SAU_RAMP_STATE_RATIO flag is set, multiply using \p mulbuf
 * for each value.
 */
static void fill_state(SAU_Ramp *restrict o, float *restrict buf,
		uint32_t from, uint32_t to,
		const float *restrict mulbuf) {
	if ((o->flags & SAU_RAMP_STATE_RATIO) != 0) {
		for (uint32_t i = from; i < to; ++i)
			buf[i] = o->v0 * mulbuf[i];
	} else {
		for (uint32_t i = from; i < to; ++i)
			buf[i] = o->v0;
	}
}

/**
 * Fill \p buf with \p buf_len values for the ramp.
 * If a curve is used, it will be applied; when elapsed,
 * the target value will become the new value.
 * If the initial and/or target value is a ratio,
 * \p mulbuf is used for a sequence of value multipliers.
 *
 * \return true if ramp target not yet reached
 */
bool SAU_Ramp_run(SAU_Ramp *restrict o, uint32_t *restrict pos,
		float *restrict buf, uint32_t buf_len, uint32_t srate,
		const float *restrict mulbuf) {
	if (!(o->flags & SAU_RAMP_CURVE)) {
		fill_state(o, buf, 0, buf_len, mulbuf);
		return false;
	}
	uint32_t time = SAU_MS_IN_SAMPLES(o->time_ms, srate);
	if ((o->flags & SAU_RAMP_CURVE_RATIO) != 0) {
		if (!(o->flags & SAU_RAMP_STATE_RATIO)) {
			// divide v0 and enable ratio to match curve and vt
			o->v0 /= mulbuf[0];
			o->flags |= SAU_RAMP_STATE_RATIO;
		}
	} else {
		if ((o->flags & SAU_RAMP_STATE_RATIO) != 0) {
			// multiply v0 and disable ratio to match curve and vt
			o->v0 *= mulbuf[0];
			o->flags &= ~SAU_RAMP_STATE_RATIO;
		}
	}
	uint32_t len = time - *pos;
	if (len > buf_len) len = buf_len;
	SAU_RampCurve_funcs[o->curve](buf, len, o->v0, o->vt, *pos, time);
	if ((o->flags & SAU_RAMP_CURVE_RATIO) != 0) {
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
		o->flags &= ~(SAU_RAMP_CURVE | SAU_RAMP_CURVE_RATIO);
		fill_state(o, buf, len, buf_len, mulbuf);
		return false;
	}
	return true;
}

/**
 * Skip ahead \p skip_len values for the ramp.
 * If a curve is elapsed, the target value will become the new value.
 *
 * Use to update ramp and its position without generating samples.
 *
 * \return true if ramp target not yet reached
 */
bool SAU_Ramp_skip(SAU_Ramp *restrict o, uint32_t *restrict pos,
		uint32_t skip_len, uint32_t srate) {
	if (!(o->flags & SAU_RAMP_CURVE))
		return false;
	uint32_t time = SAU_MS_IN_SAMPLES(o->time_ms, srate);
	uint32_t len = time - *pos;
	if (len > skip_len) len = skip_len;
	*pos += len;
	if (*pos == time) {
		/*
		 * Goal reached; turn into new initial value.
		 */
		o->v0 = o->vt;
		if ((o->flags & SAU_RAMP_CURVE_RATIO) != 0) {
			o->flags |= SAU_RAMP_STATE_RATIO;
		} else {
			o->flags &= ~SAU_RAMP_STATE_RATIO;
		}
		o->flags &= ~(SAU_RAMP_CURVE | SAU_RAMP_CURVE_RATIO);
		return false;
	}
	return true;
}
