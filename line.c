/* mgensys: Value line module.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
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

#include "line.h"

const char *const mgsLine_names[MGS_LINE_NAMED + 1] = {
	MGS_LINE__ITEMS(MGS_LINE__X_NAME)
	NULL
};

const mgsLine_fill_f mgsLine_fill_funcs[MGS_LINE_NAMED] = {
	MGS_LINE__ITEMS(MGS_LINE__X_FILL_ADDR)
};

const mgsLine_map_f mgsLine_map_funcs[MGS_LINE_NAMED] = {
	MGS_LINE__ITEMS(MGS_LINE__X_MAP_ADDR)
};

// the noinline use below works around i386 clang performance issue
/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
mgsNoinline void mgsLine_fill_hor(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(void)vt;
	(void)pos;
	(void)time;
	if (!mulbuf) {
		for (uint32_t i = 0; i < len; ++i)
			buf[i] = v0;
	} else {
		for (uint32_t i = 0; i < len; ++i)
			buf[i] = v0 * mulbuf[i];
	}
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to a straight horizontal line,
 * by simply writing \p len copies of \p v0 into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_hor().
 */
void mgsLine_map_hor(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	(void)vt;
	(void)t;
	for (uint32_t i = 0; i < len; ++i)
		buf[i] = v0;
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void mgsLine_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float v = v0 + (vt - v0) * (i_pos * inv_time);
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to a linear trajectory,
 * by writing \p len values between of \p v0 and \p vt into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_lin().
 */
void mgsLine_map_lin(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	for (uint32_t i = 0; i < len; ++i) {
		buf[i] = v0 + (vt - v0) * t[i];
	}
}

/*
 * Scaled and shifted sine ramp, using degree 5 polynomial
 * with no error at ends and double the minimax max error.
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
	x -= 0.5f;
	x2 = x*x;
	return 0.5f + x*(scale[0] + x2*(scale[1] + x2*scale[2]));
}

/**
 * Fill \p buf with \p len values along a sinuous trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Rises or falls similarly to how sin() moves from trough to
 * crest and back. Uses a ~99.993% accurate polynomial curve.
 */
void mgsLine_fill_sin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float x = i_pos * inv_time;
		float v = v0 + (vt - v0) * sinramp(x);
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to a sinuous trajectory,
 * by writing \p len values between of \p v0 and \p vt into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_sin().
 */
void mgsLine_map_sin(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	for (uint32_t i = 0; i < len; ++i) {
		buf[i] = v0 + (vt - v0) * sinramp(t[i]);
	}
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
void mgsLine_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 > vt ?
		mgsLine_fill_xpe :
		mgsLine_fill_lge)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to an exponential trajectory,
 * by writing \p len values between of \p v0 and \p vt into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_exp().
 */
void mgsLine_map_exp(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	(v0 > vt ?
		mgsLine_map_xpe :
		mgsLine_map_lge)(buf, len, v0, vt, t);
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
void mgsLine_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 < vt ?
		mgsLine_fill_xpe :
		mgsLine_fill_lge)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to a logarithmic trajectory,
 * by writing \p len values between of \p v0 and \p vt into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_log().
 */
void mgsLine_map_log(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	(v0 < vt ?
		mgsLine_map_xpe :
		mgsLine_map_lge)(buf, len, v0, vt, t);
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
 * Fill \p buf with \p len values along an "envelope" trajectory
 * which exponentially saturates and decays (like a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural for
 * frequency sweeping, and symmetric to the "opposite", 'lge' fill type.
 */
void mgsLine_fill_xpe(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float x = i_pos * inv_time;
		float v = vt + (v0 - vt) * expramp(1.f - x);
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to an "envelope" trajectory
 * which exponentially saturates and decays (like a capacitor),
 * by writing \p len values between of \p v0 and \p vt into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_xpe().
 */
void mgsLine_map_xpe(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	for (uint32_t i = 0; i < len; ++i) {
		buf[i] = vt + (v0 - vt) * expramp(1.f - t[i]);
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
void mgsLine_fill_lge(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float x = i_pos * inv_time;
		float v = v0 + (vt - v0) * expramp(x);
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Map positions \p t (values from 0.0 to 1.0) to an "envelope" trajectory
 * which logarithmically saturates and decays (opposite of a capacitor),
 * by writing \p len values between of \p v0 and \p vt into \p buf.
 *
 * Mapping counterpart of filling function mgsLine_fill_lge().
 */
void mgsLine_map_lge(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t) {
	for (uint32_t i = 0; i < len; ++i) {
		buf[i] = v0 + (vt - v0) * expramp(t[i]);
	}
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void mgsLine_copy(mgsLine *restrict o,
		const mgsLine *restrict src,
		uint32_t srate) {
	if (!src)
		return;
	uint8_t mask = 0;
	if ((src->flags & MGS_LINEP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= MGS_LINEP_STATE
			| MGS_LINEP_STATE_RATIO;
	} else if ((o->flags & MGS_LINEP_GOAL) != 0) {
		/*
		 * If old goal not reached, pick value at its current position.
		 */
		if ((src->flags & MGS_LINEP_GOAL) != 0) {
			float f;
			mgsLine_get(o, &f, 1, NULL);
			o->v0 = f;
		}
	}
	if ((src->flags & MGS_LINEP_GOAL) != 0) {
		o->vt = src->vt;
		if (src->flags & MGS_LINEP_TIME_IF_NEW)
			o->end -= o->pos;
		o->pos = 0;
		mask |= MGS_LINEP_GOAL
			| MGS_LINEP_GOAL_RATIO;
	}
	if ((src->flags & MGS_LINEP_TYPE) != 0) {
		o->type = src->type;
		mask |= MGS_LINEP_TYPE;
	}
	if (!(o->flags & MGS_LINEP_TIME) ||
	    !(src->flags & MGS_LINEP_TIME_IF_NEW)) {
		/*
		 * Time overridden.
		 */
		if ((src->flags & MGS_LINEP_TIME) != 0) {
			o->end = mgs_ms_in_samples(src->time_ms, srate);
			o->time_ms = src->time_ms;
			mask |= MGS_LINEP_TIME;
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
mgsNoinline uint32_t mgsLine_get(mgsLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	if (!(o->flags & MGS_LINEP_GOAL))
		return 0;
	/*
	 * If only one of state and goal is a ratio value,
	 * adjust state value used for state-to-goal fill.
	 */
	if ((o->flags & MGS_LINEP_GOAL_RATIO) != 0) {
		if (!(o->flags & MGS_LINEP_STATE_RATIO)) {
			if (mulbuf != NULL) o->v0 /= mulbuf[0];
			o->flags |= MGS_LINEP_STATE_RATIO;
		}
		/* allow a missing mulbuf */
	} else {
		if ((o->flags & MGS_LINEP_STATE_RATIO) != 0) {
			if (mulbuf != NULL) o->v0 *= mulbuf[0];
			o->flags &= ~MGS_LINEP_STATE_RATIO;
		}
		mulbuf = NULL; /* no ratio handling past first value */
	}
	if (o->pos >= o->end)
		return 0;
	uint32_t len = o->end - o->pos;
	if (len > buf_len) len = buf_len;
	mgsLine_fill_funcs[o->type](buf, len,
			o->v0, o->vt, o->pos, o->end, mulbuf);
	return len;
}

/*
 * Move time position up to \p buf_len samples for the line towards the end.
 *
 * \return true unless time has expired
 */
static bool advance_len(mgsLine *restrict o, uint32_t buf_len) {
	uint32_t len = 0;
	if (o->pos < o->end) {
		len = o->end - o->pos;
		if (len > buf_len) len = buf_len;
		o->pos += len;
	}
	if (o->pos >= o->end) {
		o->pos = 0;
		o->flags &= ~MGS_LINEP_TIME;
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
bool mgsLine_run(mgsLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	uint32_t len = 0;
	if (!(o->flags & MGS_LINEP_GOAL)) {
		advance_len(o, buf_len);
		goto FILL;
	}
	len = mgsLine_get(o, buf, buf_len, mulbuf);
	o->pos += len;
	if (o->pos >= o->end) {
		/*
		 * Goal reached; turn into new state value,
		 * filling remaining buffer values with it.
		 */
		o->v0 = o->vt;
		o->pos = 0;
		o->flags&=~(MGS_LINEP_GOAL|MGS_LINEP_GOAL_RATIO|MGS_LINEP_TIME);
	FILL:
		if (!(o->flags & MGS_LINEP_STATE_RATIO))
			mulbuf = NULL;
		else if (mulbuf != NULL)
			mulbuf += len;
		mgsLine_fill_hor(buf + len, buf_len - len,
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
bool mgsLine_skip(mgsLine *restrict o, uint32_t skip_len) {
	if (!advance_len(o, skip_len)) {
		if (!(o->flags & MGS_LINEP_GOAL))
			return false;
		/*
		 * Goal reached; turn into new state value.
		 */
		o->v0 = o->vt;
		if ((o->flags & MGS_LINEP_GOAL_RATIO) != 0) {
			o->flags |= MGS_LINEP_STATE_RATIO;
		} else {
			o->flags &= ~MGS_LINEP_STATE_RATIO;
		}
		o->flags &= ~(MGS_LINEP_GOAL | MGS_LINEP_GOAL_RATIO);
		return false;
	}
	return (o->flags & MGS_LINEP_GOAL) != 0;
}
