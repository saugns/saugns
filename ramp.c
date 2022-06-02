/* saugns: Value ramp module.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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

#include "ramp.h"

const char *const SAU_Ramp_names[SAU_RAMP_FILLS + 1] = {
	"hold",
	"lin",
	"sin",
	"exp",
	"log",
	"xpe",
	"lge",
	NULL
};

const SAU_Ramp_fill_f SAU_Ramp_fill_funcs[SAU_RAMP_FILLS] = {
	SAU_Ramp_fill_hold,
	SAU_Ramp_fill_lin,
	SAU_Ramp_fill_sin,
	SAU_Ramp_fill_exp,
	SAU_Ramp_fill_log,
	SAU_Ramp_fill_xpe,
	SAU_Ramp_fill_lge,
};

// the noinline use below works around i386 clang performance issue
/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
sauNoinline void SAU_Ramp_fill_hold(float *restrict buf, uint32_t len,
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
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void SAU_Ramp_fill_lin(float *restrict buf, uint32_t len,
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
void SAU_Ramp_fill_sin(float *restrict buf, uint32_t len,
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
 * Fill \p buf with \p len values along an exponential trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Unlike a real exponential curve, it has a definite beginning
 * and end. (Uses one of 'xpe' or 'lge', depending on whether
 * the curve rises or falls.)
 */
void SAU_Ramp_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 > vt ?
		SAU_Ramp_fill_xpe :
		SAU_Ramp_fill_lge)(buf, len, v0, vt, pos, time, mulbuf);
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
void SAU_Ramp_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	(v0 < vt ?
		SAU_Ramp_fill_xpe :
		SAU_Ramp_fill_lge)(buf, len, v0, vt, pos, time, mulbuf);
}

/**
 * Fill \p buf with \p len values along an "envelope" trajectory
 * which exponentially saturates and decays (like a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural,
 * and symmetric to the "opposite" 'lge' fill type.
 */
void SAU_Ramp_fill_xpe(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float mod = 1.f - i_pos * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
			(mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		float v = vt + (v0 - vt) * mod;
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Fill \p buf with \p len values along an "envelope" trajectory
 * which logarithmically saturates and decays (opposite of a capacitor),
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural,
 * and symmetric to the "opposite" 'xpe' fill type.
 */
void SAU_Ramp_fill_lge(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf) {
	const float inv_time = 1.f / time;
	for (uint32_t i = 0; i < len; ++i) {
		const uint32_t i_pos = i + pos;
		float mod = i_pos * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
			(mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		float v = v0 + (vt - v0) * mod;
		if (!mulbuf)
			buf[i] = v;
		else
			buf[i] = v * mulbuf[i];
	}
}

/**
 * Copy changes from \p src to the instance,
 * preserving non-overridden parts of state.
 */
void SAU_Ramp_copy(SAU_Ramp *restrict o,
		const SAU_Ramp *restrict src,
		uint32_t srate) {
	if (!src)
		return;
	uint8_t mask = 0;
	if ((src->flags & SAU_RAMPP_STATE) != 0) {
		o->v0 = src->v0;
		mask |= SAU_RAMPP_STATE
			| SAU_RAMPP_STATE_RATIO;
	} else if ((o->flags & SAU_RAMPP_GOAL) != 0) {
		/*
		 * If old goal not reached, pick value at its current position.
		 */
		if ((src->flags & SAU_RAMPP_GOAL) != 0) {
			float f;
			SAU_Ramp_get(o, &f, 1, NULL);
			o->v0 = f;
		}
	}
	if ((src->flags & SAU_RAMPP_GOAL) != 0) {
		o->vt = src->vt;
		if (src->flags & SAU_RAMPP_TIME_IF_NEW)
			o->end -= o->pos;
		o->pos = 0;
		mask |= SAU_RAMPP_GOAL
			| SAU_RAMPP_GOAL_RATIO;
	}
	if ((src->flags & SAU_RAMPP_FILL_TYPE) != 0) {
		o->fill_type = src->fill_type;
		mask |= SAU_RAMPP_FILL_TYPE;
	}
	if (!(o->flags & SAU_RAMPP_TIME) ||
	    !(src->flags & SAU_RAMPP_TIME_IF_NEW)) {
		/*
		 * Time overridden.
		 */
		if ((src->flags & SAU_RAMPP_TIME) != 0) {
			o->end = SAU_ms_in_samples(src->time_ms, srate);
			o->time_ms = src->time_ms;
			mask |= SAU_RAMPP_TIME;
		}
	}
	o->flags &= ~mask;
	o->flags |= (src->flags & mask);
}

/**
 * Fill \p buf with up to \p buf_len values for the ramp.
 * Only fills values for an active (remaining) goal, none
 * if there's none. Will fill less than \p buf_len values
 * if the goal is reached first. Does not advance current
 * position for the ramp.
 *
 * If state and/or goal is a ratio, \p mulbuf is
 * used for value multipliers, to get "absolute"
 * values. (If \p mulbuf is NULL, it is ignored,
 * with the same result as if given 1.0 values.)
 * Otherwise \p mulbuf is ignored.
 *
 * \return number of next values got
 */
sauNoinline uint32_t SAU_Ramp_get(SAU_Ramp *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	if (!(o->flags & SAU_RAMPP_GOAL))
		return 0;
	/*
	 * If only one of state and goal is a ratio value,
	 * adjust state value used for state-to-goal fill.
	 */
	if ((o->flags & SAU_RAMPP_GOAL_RATIO) != 0) {
		if (!(o->flags & SAU_RAMPP_STATE_RATIO)) {
			if (mulbuf != NULL) o->v0 /= mulbuf[0];
			o->flags |= SAU_RAMPP_STATE_RATIO;
		}
		/* allow a missing mulbuf */
	} else {
		if ((o->flags & SAU_RAMPP_STATE_RATIO) != 0) {
			if (mulbuf != NULL) o->v0 *= mulbuf[0];
			o->flags &= ~SAU_RAMPP_STATE_RATIO;
		}
		mulbuf = NULL; /* no ratio handling past first value */
	}
	if (o->pos >= o->end)
		return 0;
	uint32_t len = o->end - o->pos;
	if (len > buf_len) len = buf_len;
	SAU_Ramp_fill_funcs[o->fill_type](buf, len,
			o->v0, o->vt, o->pos, o->end, mulbuf);
	return len;
}

/*
 * Move time position up to \p buf_len samples for the ramp towards the end.
 *
 * \return true unless time has expired
 */
static bool advance_len(SAU_Ramp *restrict o, uint32_t buf_len) {
	uint32_t len = 0;
	if (o->pos < o->end) {
		len = o->end - o->pos;
		if (len > buf_len) len = buf_len;
		o->pos += len;
	}
	if (o->pos >= o->end) {
		o->pos = 0;
		o->flags &= ~SAU_RAMPP_TIME;
		return false;
	}
	return true;
}

/**
 * Fill \p buf with \p buf_len values for the ramp.
 * A value is \a v0 if no goal is set, or a ramping
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
 * \return true if ramp goal not yet reached
 */
bool SAU_Ramp_run(SAU_Ramp *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf) {
	uint32_t len = 0;
	if (!(o->flags & SAU_RAMPP_GOAL)) {
		advance_len(o, buf_len);
		goto FILL;
	}
	len = SAU_Ramp_get(o, buf, buf_len, mulbuf);
	o->pos += len;
	if (o->pos >= o->end) {
		/*
		 * Goal reached; turn into new state value,
		 * filling remaining buffer values with it.
		 */
		o->v0 = o->vt;
		o->pos = 0;
		o->flags&=~(SAU_RAMPP_GOAL|SAU_RAMPP_GOAL_RATIO|SAU_RAMPP_TIME);
	FILL:
		if (!(o->flags & SAU_RAMPP_STATE_RATIO))
			mulbuf = NULL;
		else if (mulbuf != NULL)
			mulbuf += len;
		SAU_Ramp_fill_hold(buf + len, buf_len - len,
				o->v0, o->v0, 0, 0, mulbuf);
		return false;
	}
	return true;
}

/**
 * Skip ahead \p skip_len values for the ramp, updating state
 * and run position without generating values.
 *
 * When a goal is reached and cleared, its \a vt value becomes
 * the new \a v0 value.
 *
 * \return true if ramp goal not yet reached
 */
bool SAU_Ramp_skip(SAU_Ramp *restrict o, uint32_t skip_len) {
	if (!advance_len(o, skip_len)) {
		if (!(o->flags & SAU_RAMPP_GOAL))
			return false;
		/*
		 * Goal reached; turn into new state value.
		 */
		o->v0 = o->vt;
		if ((o->flags & SAU_RAMPP_GOAL_RATIO) != 0) {
			o->flags |= SAU_RAMPP_STATE_RATIO;
		} else {
			o->flags &= ~SAU_RAMPP_STATE_RATIO;
		}
		o->flags &= ~(SAU_RAMPP_GOAL | SAU_RAMPP_GOAL_RATIO);
		return false;
	}
	return (o->flags & SAU_RAMPP_GOAL) != 0;
}
