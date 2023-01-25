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

#pragma once
#include "math.h"

/* Macro used to declare and define line type sets of items. */
#define SAU_LINE__ITEMS(X) \
	X(cos) \
	X(lin) \
	X(sah) \
	X(exp) \
	X(log) \
	X(xpe) \
	X(lge) \
	//
#define SAU_LINE__X_ID(NAME) SAU_LINE_N_##NAME,
#define SAU_LINE__X_NAME(NAME) #NAME,
#define SAU_LINE__X_PROTOTYPES(NAME) \
void sauLine_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time, \
		const float *restrict mulbuf); \
void sauLine_map_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, const float *restrict t); \
/**/
#define SAU_LINE__X_FILL_ADDR(NAME) sauLine_fill_##NAME,
#define SAU_LINE__X_MAP_ADDR(NAME) sauLine_map_##NAME,

/**
 * Line fill types.
 */
enum {
	SAU_LINE__ITEMS(SAU_LINE__X_ID)
	SAU_LINE_NAMED
};

SAU_LINE__ITEMS(SAU_LINE__X_PROTOTYPES)

/** Names of line fill types, with an extra NULL pointer at the end. */
extern const char *const sauLine_names[SAU_LINE_NAMED + 1];

typedef void (*sauLine_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

typedef void (*sauLine_map_f)(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t);

/** Fill functions for line types. */
extern const sauLine_fill_f sauLine_fill_funcs[SAU_LINE_NAMED];

/** Map functions for line types. */
extern const sauLine_map_f sauLine_map_funcs[SAU_LINE_NAMED];

/**
 * Line parameter flags.
 */
enum {
	SAU_LINEP_STATE       = 1<<0, // v0 set
	SAU_LINEP_STATE_RATIO = 1<<1,
	SAU_LINEP_GOAL        = 1<<2, // vt set -- and timed fill will be done
	SAU_LINEP_GOAL_RATIO  = 1<<3,
	SAU_LINEP_TYPE        = 1<<4, // type set
	SAU_LINEP_TIME        = 1<<5, // time_ms set -- cleared on time expiry
	SAU_LINEP_TIME_IF_NEW = 1<<6, // time_ms to be kept if currently set
};

/**
 * Line parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct sauLine {
	float v0, vt;
	uint32_t pos, end;
	uint32_t time_ms;
	uint8_t type;
	uint8_t flags;
} sauLine;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define sauLine_ENABLED(o) \
	((o)->flags & (SAU_LINEP_STATE | SAU_LINEP_GOAL))

/** Needed before get, run, or skip when a line is not copy-initialized. */
static inline void sauLine_setup(sauLine *restrict o, uint32_t srate) {
	o->end = sau_ms_in_samples(o->time_ms, srate, NULL);
}
void sauLine_copy(sauLine *restrict o,
		const sauLine *restrict src,
		uint32_t srate);

uint32_t sauLine_get(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool sauLine_run(sauLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool sauLine_skip(sauLine *restrict o, uint32_t skip_len);
