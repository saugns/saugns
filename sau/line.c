/* SAU library: Value line module.
 * Copyright (c) 2011-2013, 2017-2023 Joel K. Pettersson
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

#include <sau/line.h>
#include <string.h>

const char *const sauLine_names[SAU_LINE_NAMED + 1] = {
	SAU_LINE__ITEMS(SAU_LINE__X_NAME)
	NULL
};

const sauLine_fill_f sauLine_fill_funcs[SAU_LINE_NAMED] = {
	SAU_LINE__ITEMS(SAU_LINE__X_FILL_ADDR)
};

const sauLine_apply_f sauLine_apply_funcs[SAU_LINE_NAMED] = {
	SAU_LINE__ITEMS(SAU_LINE__X_APPLY_ADDR)
};

// the noinline use below works around i386 clang performance issue
/**
 * Fill \p buf with \p len values along a "sample and hold"
 * straight horizontal line, i.e. \p len copies of \p v0.
 */
sauNoinline void sauLine_fill_sah(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(void)vt;
	(void)pos;
	(void)time;
	for (uint32_t i = 0; i < len; ++i)
		buf[i] = mulbuf ? (v0 * mulbuf[i]) : v0;
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) along a "sample and hold"
 * straight horizontal line, by writing \p len values from \p end0 into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_sah().
 */
void sauLine_apply_sah(float *restrict buf, uint32_t len) {
	memset(buf, 0, sizeof(float) * len);
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void sauLine_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		float v = vm + vd * x;
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to a linear trajectory,
 * by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_lin().
 */
void sauLine_apply_lin(float *restrict buf, uint32_t len) {
	(void)buf, (void)len; /* nothing to do */
}

/*
 * Scaled and shifted sine ramp, using degree 5 polynomial
 * with no error at ends and double the minimax max error.
 *
 * Note: Needs \p x in, returns in range from -0.5 to 0.5.
 *
 * If used for oscillator, would have a roughly -84 dB 5th
 * harmonic distortion but nothing else above 16-bit noise
 * floor. http://joelkp.frama.io/blog/modified-taylor.html
 */
static inline float sinramp(float x) {
	const float scale[] = {
		/* constants calculated with 80-bit "long double" use */
		+1.5702137061703461473139223358864L,
		-2.568278787380814155456160152724L,
		+1.1496958507977182668618673644367L,
	};
	float x2;
	x2 = x*x;
	return x*(scale[0] + x2*(scale[1] + x2*scale[2]));
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
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		float v = vm + vd * sinramp(x);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to a sinuous trajectory,
 * by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_cos().
 */
void sauLine_apply_cos(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i] - 0.5f;
		buf[i] = 0.5f + sinramp(x);
	}
}

/*
 * My 2011 exponential curve approximation.
 */
static inline float expramp(float x) {
	float x2 = x * x;
	float x3 = x2 * x;
	return x3 + (x2 * x3 - x2) *
		(x * (629.f/1792.f) + x2 * (1163.f/1792.f));
}

/**
 * Fill \p buf with \p len values along an exponential trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Unlike a real exponential curve, it has a definite beginning
 * and end. (Uses one of 'xpe' or 'lge', depending on whether
 * the curve rises or falls.)
 */
void sauLine_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 > vt ?
		sauLine_fill_xpe :
		sauLine_fill_lge)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to an exponential
 * trajectory, by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_exp().
 */
void sauLine_apply_exp(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = expramp(x);
	}
}

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Unlike a real "log(1 + x)" curve, it has a definite beginning
 * and end. (Uses one of 'xpe' or 'lge', depending on whether
 * the curve rises or falls.)
 */
void sauLine_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 < vt ?
		sauLine_fill_xpe :
		sauLine_fill_lge)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to a logarithmic
 * trajectory, by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_log().
 */
void sauLine_apply_log(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = 1.f - expramp(1.f-x);
	}
}

/**
 * Fill \p buf with \p len values along an "envelope" trajectory
 * which exponentially saturates and decays (like a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural for
 * frequency sweeping, and symmetric to the "opposite", 'lge' fill type.
 */
void sauLine_fill_xpe(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		float x = (i + pos) * inv_time;
		float v = vt + (v0 - vt) * expramp(1.f - x);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to an "envelope"
 * trajectory which exponentially saturates and decays (like a capacitor),
 * by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_xpe().
 */
void sauLine_apply_xpe(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = 1.f - expramp(1.f - x);
	}
}

/**
 * Fill \p buf with \p len values along an "envelope" trajectory
 * which logarithmically saturates and decays (opposite of a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural for
 * frequency sweeping, and symmetric to the "opposite", 'xpe' fill type.
 */
void sauLine_fill_lge(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		float x = (i + pos) * inv_time;
		float v = v0 + (vt - v0) * expramp(x);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to an "envelope"
 * trajectory which logarithmically saturates and decays (opposite of
 * a capacitor), by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_lge().
 */
void sauLine_apply_lge(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = expramp(x);
	}
}

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
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		float x = 0.5f - ((int32_t)i + adj_pos) * inv_time;
		float v = vt + (v0 - vt) * (x * x);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to an x-squared
 * "envelope" trajectory (the curve upside-down when increasing like 'xpe'),
 * by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_sqe().
 */
void sauLine_apply_sqe(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = 1.f - buf[i];
		buf[i] = 1.f - (x * x);
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
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float scale = 2 * inv_time;
	for (uint32_t i = 0; i < len; ++i) {
		float x = 0.f - ((int32_t)i + adj_pos) * scale;
		float v = vt + (v0 - vt) * (x * x * x * 0.5f + 0.5f);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to an x-cubed trajectory,
 * by writing \p len values.
 *
 * Mapping counterpart of filling function sauLine_fill_cub().
 */
void sauLine_apply_cub(float *restrict buf, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = (0.5f - buf[i])*2;
		buf[i] = 0.5f - (x * x * x * 0.5f);
	}
}

/**
 * Fill \p buf with \p len values of uniform white noise
 * between \p v0 and \p vt, seeded with position \p pos.
 */
void sauLine_fill_uwh(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float scale = 0.5f/(float)INT32_MAX;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0) * scale;
	(void)time;
	for (uint32_t i = 0; i < len; ++i) {
		int32_t s = sau_ranfast32(pos + i);
		float v = vm + vd * s;
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to uniform white noise
 * samples between \p end0 and \p end1, writing \p len values into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_uwh().
 */
void sauLine_apply_uwh(float *restrict buf, uint32_t len) {
	const float scale = 0.5f/(float)INT32_MAX;
	for (uint32_t i = 0; i < len; ++i) {
		union {float f; int32_t i;} x = {buf[i]};
		int32_t s = sau_ranfast32(x.i);
		float v = 0.5f + scale * s;
		buf[i] = v;
	}
}

/**
 * Fill \p buf with \p len values along "noise camel line" (line
 * plus two softer white noise bulges), between \p v0 and \p vt,
 * seeded with position \p pos.
 */
void sauLine_fill_ncl(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float scale = 0.5f/(float)INT32_MAX;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		float xb = x + 0.5f; xb -= (3.f - (xb+xb))*xb*xb;
		int32_t s = sau_ranfast32(pos + i);
		float v = vm + vd * (x + xb * s * scale);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to the trajectory of
 * a "noise camel line" (line plus two softer white noise bulges),
 * between \p end0 and \p end1, writing \p len values into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_ncl().
 */
void sauLine_apply_ncl(float *restrict buf, uint32_t len) {
	const float scale = 0.5f/(float)INT32_MAX;
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		float xb = x; xb -= (3.f - (xb+xb))*xb*xb;
		union {float f; int32_t i;} xs = {buf[i]};
		int32_t s = sau_ranfast32(xs.i);
		float v = x + xb * s * scale;
		buf[i] = v;
	}
}

/**
 * Fill \p buf with \p len values along "noise hump line" (line
 * plus broad, big white noise bulge), between \p v0 and \p vt,
 * seeded with position \p pos.
 */
void sauLine_fill_nhl(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const int32_t adj_pos = pos - (time / 2);
	const float inv_time = 1.f / time;
	const float scale = 2 * 0.5f/(float)INT32_MAX;
	const float vm = (v0 + vt) * 0.5f;
	const float vd = (vt - v0);
	for (uint32_t i = 0; i < len; ++i) {
		float x = ((int32_t)i + adj_pos) * inv_time;
		float xb = x + 0.5f; xb -= xb*xb;
		int32_t s = sau_ranfast32(pos + i);
		float v = vm + vd * (x + xb * s * scale);
		buf[i] = mulbuf ? (v * mulbuf[i]) : v;
	}
}

/**
 * Map positions in \p buf (values from 0.0 to 1.0) to the trajectory of
 * a "noise hump line" (line plus a broad, big white noise bulge),
 * between \p end0 and \p end1, writing \p len values into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_nhl().
 */
void sauLine_apply_nhl(float *restrict buf, uint32_t len) {
	const float scale = 2 * 0.5f/(float)INT32_MAX;
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		float xb = x; xb -= xb*xb;
		union {float f; int32_t i;} xs = {buf[i]};
		int32_t s = sau_ranfast32(xs.i);
		float v = x + xb * s * scale;
		buf[i] = v;
	}
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
	} else if ((o->flags & SAU_LINEP_GOAL) != 0) {
		/*
		 * If old goal not reached, pick value at its current position.
		 */
		if ((src->flags & SAU_LINEP_GOAL) != 0) {
			float f;
			sauLine_get(o, &f, 1, NULL);
			o->v0 = f;
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
 * Only fills values for an active (remaining) goal, none
 * if there's none. Will fill less than \p buf_len values
 * if the goal is reached first. Does not advance current
 * position for the line.
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
	if (!(o->flags & SAU_LINEP_GOAL))
		return 0;
	/*
	 * If only one of state and goal is a ratio value,
	 * adjust state value used for state-to-goal fill.
	 */
	if ((o->flags & SAU_LINEP_GOAL_RATIO) != 0) {
		if (!(o->flags & SAU_LINEP_STATE_RATIO)) {
			if (mulbuf != NULL) o->v0 /= mulbuf[0];
			o->flags |= SAU_LINEP_STATE_RATIO;
		}
		/* allow a missing mulbuf */
	} else {
		if ((o->flags & SAU_LINEP_STATE_RATIO) != 0) {
			if (mulbuf != NULL) o->v0 *= mulbuf[0];
			o->flags &= ~SAU_LINEP_STATE_RATIO;
		}
		mulbuf = NULL; /* no ratio handling past first value */
	}
	if (o->pos >= o->end)
		return 0;
	uint32_t len = o->end - o->pos;
	if (len > buf_len) len = buf_len;
	sauLine_fill_funcs[o->type](buf, len,
			o->v0, o->vt, o->pos, o->end, mulbuf);
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
		o->pos = 0;
		o->flags &= ~SAU_LINEP_TIME;
		return false;
	}
	return true;
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
 * \return true if line goal not yet reached
 */
bool sauLine_run(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	uint32_t len = 0;
	if (!(o->flags & SAU_LINEP_GOAL)) {
		advance_len(o, buf_len);
		goto FILL;
	}
	len = sauLine_get(o, buf, buf_len, mulbuf);
	o->pos += len;
	if (o->pos >= o->end) {
		/*
		 * Goal reached; turn into new state value,
		 * filling remaining buffer values with it.
		 */
		o->v0 = o->vt;
		o->pos = 0;
		o->flags&=~(SAU_LINEP_GOAL|SAU_LINEP_GOAL_RATIO|SAU_LINEP_TIME);
	FILL:
		if (!(o->flags & SAU_LINEP_STATE_RATIO))
			mulbuf = NULL;
		else if (mulbuf != NULL)
			mulbuf += len;
		sauLine_fill_sah(buf + len, buf_len - len,
				o->v0, o->v0, 0, 0, mulbuf);
		return false;
	}
	return true;
}

/**
 * Skip ahead \p skip_len values for the line, updating state
 * and run position without generating values.
 *
 * When a goal is reached and cleared, its \a vt value becomes
 * the new \a v0 value.
 *
 * \return true if line goal not yet reached
 */
bool sauLine_skip(sauLine *restrict o, uint32_t skip_len) {
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
		return false;
	}
	return (o->flags & SAU_LINEP_GOAL) != 0;
}
