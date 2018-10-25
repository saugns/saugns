/* saugns: Script parameter module.
 * Copyright (c) 2018-2019 Joel K. Pettersson
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

#include "param.h"
#include "../math.h"

/**
 * Set instance to default values.
 *
 * (This does not include values specific to a particular parameter.)
 */
void SAU_TimedParam_reset(SAU_TimedParam *restrict o) {
	*o = (SAU_TimedParam){0};
	o->slope = SAU_SLOPE_LIN; // default if slope enabled
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void SAU_TimedParam_copy(SAU_TimedParam *restrict o,
		const SAU_TimedParam *restrict src) {
	uint8_t mask = 0;
	if ((src->flags & SAU_TPAR_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SAU_TPAR_STATE | SAU_TPAR_STATE_RATIO;
	}
	if ((src->flags & SAU_TPAR_SLOPE) != 0) {
		o->vt = src->vt;
		o->time_ms = src->time_ms;
		o->slope = src->slope;
		mask |= SAU_TPAR_SLOPE | SAU_TPAR_SLOPE_RATIO;
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/*
 * Fill \p buf from \p from to \p to - 1 with copies of \a v0.
 *
 * If the SAU_TPAR_STATE_RATIO flag is set, multiply using \p mulbuf
 * for each value.
 */
static void fill_state(SAU_TimedParam *restrict o, float *restrict buf,
		uint32_t from, uint32_t to,
		const float *restrict mulbuf) {
	if ((o->flags & SAU_TPAR_STATE_RATIO) != 0) {
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
bool SAU_TimedParam_run(SAU_TimedParam *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf) {
	if (!(o->flags & SAU_TPAR_SLOPE)) {
		fill_state(o, buf, 0, buf_len, mulbuf);
		return false;
	}
	uint32_t time = SAU_MS_TO_SRT(o->time_ms, srate);
	if ((o->flags & SAU_TPAR_SLOPE_RATIO) != 0) {
		if (!(o->flags & SAU_TPAR_STATE_RATIO)) {
			// divide v0 and enable ratio to match slope and vt
			o->v0 /= mulbuf[0];
			o->flags |= SAU_TPAR_STATE_RATIO;
		}
	} else {
		if ((o->flags & SAU_TPAR_STATE_RATIO) != 0) {
			// multiply v0 and disable ratio to match slope and vt
			o->v0 *= mulbuf[0];
			o->flags &= ~SAU_TPAR_STATE_RATIO;
		}
	}
	uint32_t len;
	len = time - *pos;
	if (len > buf_len) len = buf_len;
	SAU_Slope_funcs[o->slope](buf, len, o->v0, o->vt, *pos, time);
	if ((o->flags & SAU_TPAR_SLOPE_RATIO) != 0) {
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
		o->flags &= ~(SAU_TPAR_SLOPE | SAU_TPAR_SLOPE_RATIO);
		fill_state(o, buf, len, buf_len, mulbuf);
		return false;
	}
	return true;
}
