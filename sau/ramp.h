/* SAU library: Value ramp module.
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

/* Macro used to declare and define ramp fill sets of items. */
#define SAU_RAMP__ITEMS(X) \
	X(sah) \
	X(lin) \
	X(cos) \
	X(exp) \
	X(log) \
	X(xpe) \
	X(lge) \
	//
#define SAU_RAMP__X_ID(NAME) SAU_RAMP_N_##NAME,
#define SAU_RAMP__X_NAME(NAME) #NAME,
#define SAU_RAMP__X_PROTOTYPE(NAME) \
void sauRamp_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time, \
		const float *restrict mulbuf);
#define SAU_RAMP__X_ADDRESS(NAME) sauRamp_fill_##NAME,

/**
 * Ramp fill types.
 */
enum {
	SAU_RAMP__ITEMS(SAU_RAMP__X_ID)
	SAU_RAMP_NAMED
};

SAU_RAMP__ITEMS(SAU_RAMP__X_PROTOTYPE)

/** Names of ramp fill types, with an extra NULL pointer at the end. */
extern const char *const sauRamp_names[SAU_RAMP_NAMED + 1];

typedef void (*sauRamp_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

/** Fill functions for ramp fill types. */
extern const sauRamp_fill_f sauRamp_fill_funcs[SAU_RAMP_NAMED];

/**
 * Ramp parameter flags.
 */
enum {
	SAU_RAMPP_STATE       = 1<<0, // v0 set
	SAU_RAMPP_STATE_RATIO = 1<<1,
	SAU_RAMPP_GOAL        = 1<<2, // vt set -- and timed fill will be done
	SAU_RAMPP_GOAL_RATIO  = 1<<3,
	SAU_RAMPP_FILL_TYPE   = 1<<4, // fill_type set
	SAU_RAMPP_TIME        = 1<<5, // time_ms set -- cleared on time expiry
	SAU_RAMPP_TIME_IF_NEW = 1<<6, // time_ms to be kept if currently set
};

/**
 * Ramp parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct sauRamp {
	float v0, vt;
	uint32_t pos, end;
	uint32_t time_ms;
	uint8_t fill_type;
	uint8_t flags;
} sauRamp;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define sauRamp_ENABLED(o) \
	((o)->flags & (SAU_RAMPP_STATE | SAU_RAMPP_GOAL))

/** Needed before get, run, or skip when a ramp is not copy-initialized. */
static inline void sauRamp_setup(sauRamp *restrict o, uint32_t srate) {
	o->end = sau_ms_in_samples(o->time_ms, srate, NULL);
}
void sauRamp_copy(sauRamp *restrict o,
		const sauRamp *restrict src,
		uint32_t srate);

uint32_t sauRamp_get(sauRamp *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool sauRamp_run(sauRamp *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool sauRamp_skip(sauRamp *restrict o, uint32_t skip_len);
