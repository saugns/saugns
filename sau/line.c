/* SAU library: Value line module.
 * Copyright (c) 2011-2013, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sau/line.h>

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
void sauLine_map_sah(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	(void)end1;
	for (uint32_t i = 0; i < len; ++i)
		buf[i] = end0[i];
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
 * by writing \p len values between those of \p end0 and \p end1 into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_lin().
 */
void sauLine_map_lin(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = end0[i] + (end1[i] - end0[i]) * x;
	}
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
 * by writing \p len values between those of \p end0 and \p end1 into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_cos().
 */
void sauLine_map_cos(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i] - 0.5f;
		buf[i] = end0[i] + (end1[i] - end0[i]) * (0.5f + sinramp(x));
	}
}

/*
 * My 2011 exponential curve approximation.
 *
 * Steepness matches a downscaled exp(6*x), for 0 <= x <= 1.
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
 * trajectory, by writing \p len values between those of \p end0 and \p end1
 * into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_exp().
 */
void sauLine_map_exp(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		if (end0[i] > end1[i])
			buf[i] = end1[i] + (end0[i] - end1[i]) * expramp(1.f-x);
		else
			buf[i] = end0[i] + (end1[i] - end0[i]) * expramp(x);
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
 * trajectory, by writing \p len values between those of \p end0 and \p end1
 * into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_log().
 */
void sauLine_map_log(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		if (end0[i] < end1[i])
			buf[i] = end1[i] + (end0[i] - end1[i]) * expramp(1.f-x);
		else
			buf[i] = end0[i] + (end1[i] - end0[i]) * expramp(x);
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
 * by writing \p len values between those of \p end0 and \p end1 into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_xpe().
 */
void sauLine_map_xpe(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = end1[i] + (end0[i] - end1[i]) * expramp(1.f - x);
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
 * a capacitor), by writing \p len values between those of \p end0
 * and \p end1 into \p buf.
 *
 * Mapping counterpart of filling function sauLine_fill_lge().
 */
void sauLine_map_lge(float *restrict buf, uint32_t len,
		const float *restrict end0, const float *restrict end1) {
	for (uint32_t i = 0; i < len; ++i) {
		float x = buf[i];
		buf[i] = end0[i] + (end1[i] - end0[i]) * expramp(x);
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
