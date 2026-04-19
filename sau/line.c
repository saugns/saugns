/* SAU library: Value line module.
 * Copyright (c) 2011-2013, 2017-2026 Joel K. Pettersson
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

#include "line.h"

#define LINE_MAP_FUNC(NAME, ...) \
void sauLine_map_##NAME(float *restrict buf, uint32_t len, \
		const float *restrict end0, const float *restrict end1) { \
	for (uint32_t i = 0; i < len; ++i) \
		buf[i] = sauLine_val_##NAME(buf[i], end0[i], end1[i]); \
}

// all of them have the same form, so just generate them all
SAU_LINE__ITEMS(LINE_MAP_FUNC)

// fill functions not written in a different optimized form
#define LINE_FILL_FUNC(NAME, ...) \
void sauLine_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time) { \
	const float inv_time = 1.f / time; \
	for (uint32_t i = 0; i < len; ++i) { \
		float x = (i + pos) * inv_time; \
		buf[i] = sauLine_val_##NAME(x, v0, vt); \
	} \
}

// fill function which selects one of two other fill functions
#define LINE_FILL_FUNC_SELECT(NAME, COND, SEL1, SEL2) \
void sauLine_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time) { \
	(COND ? \
		sauLine_fill_##SEL1 : \
		sauLine_fill_##SEL2)(buf, len, v0, vt, pos, time); \
}

const struct sauLineCoeffs sauLine_coeffs[SAU_LINE_NAMED] = {
	SAU_LINE__ITEMS(SAU_LINE__X_COEFFS)
};

const char *const sauLine_names[SAU_LINE_NAMED + 1] = {
	SAU_LINE__ITEMS(SAU_LINE__X_NAME)
	NULL
};

const sauLine_fill_f sauLine_fill_funcs[SAU_LINE_NAMED] = {
	SAU_LINE__ITEMS(SAU_LINE__X_FILL_ADDR)
};

const sauLine_map_f sauLine_map_funcs[SAU_LINE_NAMED] = {
	SAU_LINE__ITEMS(SAU_LINE__X_MAP_ADDR)
};

const sauLine_val_f sauLine_val_funcs[SAU_LINE_NAMED] = {
	SAU_LINE__ITEMS(SAU_LINE__X_VAL_ADDR)
};

/**
 * Fill \p buf with \p len values along a "sample and hold"
 * straight horizontal line, i.e. \p len copies of \p v0.
 */
void sauLine_fill_sah(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	(void)vt;
	(void)pos;
	(void)time;
	for (uint32_t i = 0; i < len; ++i) buf[i] = v0;
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void sauLine_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		buf[i] = vm + vd * x;
	}
}

/**
 * Fill \p buf with \p len values along a sinuous trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Rises or falls similarly to how sin() moves from trough to
 * crest and back. Uses a ~99.993% accurate polynomial curve.
 */
void sauLine_fill_cos(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		buf[i] = vm + vd * sau_sinramp(x);
	}
}

/**
 * Fill \p buf with \p len values along an exponential trajectory
 * (steepness 6) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC_SELECT(exp, v0 > vt, xpe, lge)

/**
 * Fill \p buf with \p len values along an exponential trajectory
 * (steepness 11) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC_SELECT(exp11, v0 > vt, xpe11, lge11)

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * (steepness 6) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC_SELECT(log, v0 < vt, xpe, lge)

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * (steepness 11) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC_SELECT(log11, v0 < vt, xpe11, lge11)

/**
 * Fill \p buf with \p len values along an exponential saturate or decay curve
 * (steepness 6) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC(xpe, )

/**
 * Fill \p buf with \p len values along an exponential saturate or decay curve
 * (steepness 11) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC(xpe11, )

/**
 * Fill \p buf with \p len values along a logarithmic saturate or decay curve
 * (steepness 6) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC(lge, )

/**
 * Fill \p buf with \p len values along a logarithmic saturate or decay curve
 * (steepness 11) from \p v0 (at position 0) to \p vt (at position \p time),
 */
LINE_FILL_FUNC(lge11, )

/**
 * Fill \p buf with \p len values along an x-squared "envelope"
 * trajectory (the curve upside-down when increasing like 'xpe'),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses half a parabola shape for a monotonic trajectory.
 * A less-steep alternative to the exponential-ish 'xpe' fill type.
 */
void sauLine_fill_sqe(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		float x = 0.5f - ((int32_t)i + adj_pos) * inv_time;
		buf[i] = vt + (v0 - vt) * (x * x);
	}
}

/**
 * Fill \p buf with \p len values along an x-cubed trajectory,
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses both lower and upper parts (from -1 to +1) of a cube line.
 * A little bit like three stages in one (change, sustain, change).
 */
void sauLine_fill_cub(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float scale = -2 * inv_time;
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * scale;
		buf[i] = vt + (v0 - vt) * (x * x * x * 0.5f + 0.5f);
	}
}

/**
 * Fill \p buf with \p len smoothstep (degree 5) values,
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
LINE_FILL_FUNC(smo, )

/**
 * Fill \p buf with \p len values of uniform white noise
 * between \p v0 and \p vt, seeded with position \p pos.
 */
void sauLine_fill_uwh(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const float scale = 0.5f/(float)INT32_MAX;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0) * scale;
	(void)time;
	for (uint32_t i = 0; i < len; ++i) {
		int32_t s = sau_ranfast32(pos + i);
		buf[i] = vm + vd * s;
	}
}

/**
 * Fill \p buf with \p len values along "noise camel line" (line
 * plus two softer white noise bulges), between \p v0 and \p vt,
 * seeded with position \p pos.
 */
void sauLine_fill_ncl(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float scale = 0.5f/(float)INT32_MAX;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		float xb = x + 0.5f; xb -= (3.f - (xb+xb))*xb*xb;
		int32_t s = sau_ranfast32(pos + i);
		buf[i] = vm + vd * (x + xb * s * scale);
	}
}

/**
 * Fill \p buf with \p len values along "noise hump line" (line
 * plus broad, big white noise bulge), between \p v0 and \p vt,
 * seeded with position \p pos.
 */
void sauLine_fill_nhl(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float scale = 2 * 0.5f/(float)INT32_MAX;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		float xb = x + 0.5f; xb -= xb*xb;
		int32_t s = sau_ranfast32(pos + i);
		buf[i] = vm + vd * (x + xb * s * scale);
	}
}

/**
 * Copy parts of \p src not yet set in the instance to the instance.
 */
void sauLine_inherit(sauLine *restrict o,
		const sauLine *restrict src) {
	if (!src)
		return;
	uint8_t mask = 0;
	if (!(o->flags & SAU_LINEP_STATE)) {
		/*
		 * Changing goal by default replaces state with value reached.
		 */
		if ((o->flags & SAU_LINEP_GOAL) != 0 &&
		    (src->flags & SAU_LINEP_GOAL) != 0) {
			sauLine_get((sauLine*)src, &o->v0, 1, NULL);
			if (src->flags & SAU_LINEP_GOAL_RATIO)
				o->flags |= SAU_LINEP_STATE_RATIO;
			else
				o->flags &= ~SAU_LINEP_STATE_RATIO;

		} else {
			o->v0 = src->v0;
			mask |= SAU_LINEP_STATE
				| SAU_LINEP_STATE_RATIO;
		}
	}
	if (!(o->flags & SAU_LINEP_GOAL)) {
		o->vt = src->vt;
		mask |= SAU_LINEP_GOAL
			| SAU_LINEP_GOAL_RATIO;
	}
	if (!(o->flags & SAU_LINEP_TYPE)) {
		o->type = src->type;
		mask |= SAU_LINEP_TYPE;
	}
	if (!(o->flags & SAU_LINEP_TIME)) {
		o->time_ms = src->time_ms;
		mask |= SAU_LINEP_TIME
			| SAU_LINEP_TIME_IF_NEW;
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void sauLine_copy(sauLine *restrict o,
		const sauLine *restrict src,
		uint32_t srate) {
	if (!src)
		return;
	uint8_t mask = 0;
	if ((src->flags & SAU_LINEP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SAU_LINEP_STATE
			| SAU_LINEP_STATE_RATIO;
	} else {
		/*
		 * Changing goal by default replaces state with value reached.
		 */
		if ((o->flags & SAU_LINEP_GOAL) != 0 &&
		    (src->flags & SAU_LINEP_GOAL) != 0) {
			float f;
			if (sauLine_get(o, &f, 1, NULL)) o->v0 = f;
			if (o->flags & SAU_LINEP_GOAL_RATIO)
				o->flags |= SAU_LINEP_STATE_RATIO;
			else
				o->flags &= ~SAU_LINEP_STATE_RATIO;
		}
	}
	if ((src->flags & SAU_LINEP_GOAL) != 0) {
		o->vt = src->vt;
		if (src->flags & SAU_LINEP_TIME_IF_NEW)
			o->end -= o->pos;
		o->pos = 0;
		mask |= SAU_LINEP_GOAL
			| SAU_LINEP_GOAL_RATIO;
	}
	if ((src->flags & SAU_LINEP_TYPE) != 0) {
		o->type = src->type;
		mask |= SAU_LINEP_TYPE;
	}
	if (!(o->flags & SAU_LINEP_TIME) ||
	    !(src->flags & SAU_LINEP_TIME_IF_NEW)) {
		/*
		 * Time overridden.
		 */
		if ((src->flags & SAU_LINEP_TIME) != 0) {
			o->end = sau_ms_in_samples(src->time_ms, srate, NULL);
			o->time_ms = src->time_ms;
			mask |= SAU_LINEP_TIME;
		}
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/**
 * Fill \p buf with up to \p buf_len values for the line.
 * Only fills values until the \a end time is reached.
 *
 * If state and/or goal is a ratio, \p mulbuf is
 * used for value multipliers, to get "absolute"
 * values. (If \p mulbuf is NULL, it is ignored,
 * with the same result as if given 1.0 values.)
 * Otherwise \p mulbuf is ignored.
 *
 * \return number of next values got
 */
sauNoinline uint32_t sauLine_get(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	if (o->pos >= o->end)
		return 0;
	uint32_t len = o->end - o->pos;
	if (len > buf_len) len = buf_len;
	sauLine_fill_f fill_fn = sauLine_fill_funcs[o->type];
	if (!mulbuf ||
	    !(o->flags & (SAU_LINEP_STATE_RATIO|SAU_LINEP_GOAL_RATIO))) {
		fill_fn(buf, len, o->v0, o->vt, o->pos, o->end);
	} else if ((o->flags & (SAU_LINEP_STATE_RATIO|SAU_LINEP_GOAL_RATIO)) ==
			(SAU_LINEP_STATE_RATIO|SAU_LINEP_GOAL_RATIO)) {
		fill_fn(buf, len, o->v0, o->vt, o->pos, o->end);
		for (uint32_t i = 0; i < len; ++i)
			buf[i] *= mulbuf[i];
	} else if (o->flags & SAU_LINEP_GOAL_RATIO) {
		fill_fn(buf, len, 0.0, 1.0, o->pos, o->end);
		float a = o->v0;
		for (uint32_t i = 0; i < len; ++i) {
			float b = o->vt * mulbuf[i];
			buf[i] = a + (b - a) * buf[i];
		}
	} else {
		fill_fn(buf, len, 0.0, 1.0, o->pos, o->end);
		float b = o->vt;
		for (uint32_t i = 0; i < len; ++i) {
			float a = o->v0 * mulbuf[i];
			buf[i] = a + (b - a) * buf[i];
		}
	}
	return len;
}

/*
 * Move time position up to \p buf_len samples for the line towards the end.
 *
 * \return true unless time has expired
 */
static bool advance_len(sauLine *restrict o, uint32_t buf_len) {
	uint32_t len = 0;
	if (o->pos < o->end) {
		len = o->end - o->pos;
		if (len > buf_len) len = buf_len;
		o->pos += len;
	}
	if (o->pos >= o->end) {
		o->pos = o->end = 0;
		o->flags &= ~SAU_LINEP_TIME;
		return false;
	}
	return true;
}

/*
 * Fill in state, with or without mulbuf, for tail end of buffer.
 */
static void fill_buf_tail(float *restrict buf,
		uint32_t buf_len, uint32_t skip_len,
		float v0, const float *restrict mulbuf) {
	if (mulbuf) {
		for (uint32_t i = skip_len; i < buf_len; ++i)
			buf[i] = v0 * mulbuf[i];
	} else {
		for (uint32_t i = skip_len; i < buf_len; ++i)
			buf[i] = v0;
	}
}

/**
 * Fill \p buf with \p buf_len values for the line.
 * A value is \a v0 if no goal is set, or a lineing
 * towards \a vt if a goal is set, unless converted
 * from a ratio.
 *
 * If state and/or goal is a ratio, \p mulbuf is
 * used for value multipliers, to get "absolute"
 * values. (If \p mulbuf is NULL, it is ignored,
 * with the same result as if given 1.0 values.)
 * Otherwise \p mulbuf is ignored.
 *
 * When a goal is reached and cleared, its \a vt value becomes
 * the new \a v0 value.
 *
 * \return true if line has/had goal and its state has changed
 */
bool sauLine_run(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	uint32_t len = 0;
	bool has_change = false;
	if (!(o->flags & SAU_LINEP_GOAL)) {
		advance_len(o, buf_len);
		goto FILL;
	}
	has_change = (len = sauLine_get(o, buf, buf_len, mulbuf)) > 0;
	if (has_change) o->flags |= SAU_LINEP; // mark as having a new change
	o->pos += len;
	if (o->pos >= o->end) {
		/*
		 * Goal reached; turn into new state value,
		 * filling remaining buffer values with it.
		 */
		o->v0 = o->vt;
		o->pos = o->end = 0;
		if ((o->flags & SAU_LINEP_GOAL_RATIO) != 0) {
			o->flags |= SAU_LINEP_STATE_RATIO;
		} else {
			o->flags &= ~SAU_LINEP_STATE_RATIO;
		}
		o->flags &=
			~(SAU_LINEP_GOAL|SAU_LINEP_GOAL_RATIO|SAU_LINEP_TIME);
	FILL:
		if (!(o->flags & SAU_LINEP_STATE_RATIO))
			mulbuf = NULL;
		fill_buf_tail(buf, buf_len, len, o->v0, mulbuf);
	}
	return has_change;
}

/**
 * Skip ahead \p skip_len values for the line, updating state
 * and run position without generating values.
 *
 * When a goal is reached and cleared, its \a vt value becomes
 * the new \a v0 value.
 *
 * \return true if line has/had goal and its state has changed
 */
bool sauLine_skip(sauLine *restrict o, uint32_t skip_len) {
	bool has_change = (o->flags & SAU_LINEP_GOAL) && skip_len > 0;
	if (has_change) o->flags |= SAU_LINEP; // mark as having a new change
	if (!advance_len(o, skip_len)) {
		if (!(o->flags & SAU_LINEP_GOAL))
			return false;
		/*
		 * Goal reached; turn into new state value.
		 */
		o->v0 = o->vt;
		if ((o->flags & SAU_LINEP_GOAL_RATIO) != 0) {
			o->flags |= SAU_LINEP_STATE_RATIO;
		} else {
			o->flags &= ~SAU_LINEP_STATE_RATIO;
		}
		o->flags &= ~(SAU_LINEP_GOAL | SAU_LINEP_GOAL_RATIO);
	}
	return has_change;
}
