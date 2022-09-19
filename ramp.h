/* sgensys: Value ramp module.
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

#pragma once
#include "math.h"

/**
 * Ramp fill types.
 */
enum {
	SGS_RAMP_HOLD = 0,
	SGS_RAMP_LIN,
	SGS_RAMP_SIN,
	SGS_RAMP_EXP,
	SGS_RAMP_LOG,
	SGS_RAMP_XPE,
	SGS_RAMP_LGE,
	SGS_RAMP_FILLS
};

/** Names of ramp fill types, with an extra NULL pointer at the end. */
extern const char *const SGS_Ramp_names[SGS_RAMP_FILLS + 1];

typedef void (*SGS_Ramp_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

/** Fill functions for ramp fill types. */
extern const SGS_Ramp_fill_f SGS_Ramp_fill_funcs[SGS_RAMP_FILLS];

void SGS_Ramp_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);
void SGS_Ramp_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);
void SGS_Ramp_fill_sin(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);
void SGS_Ramp_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);
void SGS_Ramp_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);
void SGS_Ramp_fill_xpe(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);
void SGS_Ramp_fill_lge(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

/**
 * Ramp parameter flags.
 */
enum {
	SGS_RAMPP_STATE       = 1<<0, // v0 set
	SGS_RAMPP_STATE_RATIO = 1<<1,
	SGS_RAMPP_GOAL        = 1<<2, // vt set -- and timed fill will be done
	SGS_RAMPP_GOAL_RATIO  = 1<<3,
	SGS_RAMPP_FILL_TYPE   = 1<<4, // fill_type set
	SGS_RAMPP_TIME        = 1<<5, // time_ms set -- cleared on time expiry
	SGS_RAMPP_TIME_IF_NEW = 1<<6, // time_ms to be kept if currently set
};

/**
 * Ramp parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct SGS_Ramp {
	float v0, vt;
	uint32_t pos, end;
	uint32_t time_ms;
	uint8_t fill_type;
	uint8_t flags;
} SGS_Ramp;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define SGS_Ramp_ENABLED(o) \
	((o)->flags & (SGS_RAMPP_STATE | SGS_RAMPP_GOAL))

/** Needed before get, run, or skip when a ramp is not copy-initialized. */
static inline void SGS_Ramp_setup(SGS_Ramp *restrict o, uint32_t srate) {
	o->end = SGS_ms_in_samples(o->time_ms, srate, NULL);
}
void SGS_Ramp_copy(SGS_Ramp *restrict o,
		const SGS_Ramp *restrict src,
		uint32_t srate);

uint32_t SGS_Ramp_get(SGS_Ramp *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool SGS_Ramp_run(SGS_Ramp *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool SGS_Ramp_skip(SGS_Ramp *restrict o, uint32_t skip_len);
