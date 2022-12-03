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

#pragma once
#include "math.h"

/* Macro used to declare and define line sets of items. */
#define MGS_LINE__ITEMS(X) \
	X(hor) \
	X(lin) \
	X(sin) \
	X(exp) \
	X(log) \
	X(xpe) \
	X(lge) \
	//
#define MGS_LINE__X_ID(NAME) MGS_LINE_N_##NAME,
#define MGS_LINE__X_NAME(NAME) #NAME,
#define MGS_LINE__X_PROTOTYPES(NAME) \
void mgsLine_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time, \
		const float *restrict mulbuf); \
void mgsLine_map_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, const float *restrict t); \
/**/
#define MGS_LINE__X_FILL_ADDR(NAME) mgsLine_fill_##NAME,
#define MGS_LINE__X_MAP_ADDR(NAME) mgsLine_map_##NAME,

/**
 * Line fill types.
 */
enum {
	MGS_LINE__ITEMS(MGS_LINE__X_ID)
	MGS_LINE_NAMED
};

MGS_LINE__ITEMS(MGS_LINE__X_PROTOTYPES)

/** Names of line fill types, with an extra NULL pointer at the end. */
extern const char *const mgsLine_names[MGS_LINE_NAMED + 1];

typedef void (*mgsLine_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

typedef void (*mgsLine_map_f)(float *restrict buf, uint32_t len,
		float v0, float vt, const float *restrict t);

/** Fill functions for line types. */
extern const mgsLine_fill_f mgsLine_fill_funcs[MGS_LINE_NAMED];

/** Map functions for line types. */
extern const mgsLine_map_f mgsLine_map_funcs[MGS_LINE_NAMED];

/**
 * Line parameter flags.
 */
enum {
	MGS_LINEP_STATE       = 1<<0, // v0 set
	MGS_LINEP_STATE_RATIO = 1<<1,
	MGS_LINEP_GOAL        = 1<<2, // vt set -- and timed fill will be done
	MGS_LINEP_GOAL_RATIO  = 1<<3,
	MGS_LINEP_TYPE        = 1<<4, // type set
	MGS_LINEP_TIME        = 1<<5, // time_ms set -- cleared on time expiry
	MGS_LINEP_TIME_IF_NEW = 1<<6, // time_ms to be kept if currently set
};

/**
 * Line parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct mgsLine {
	float v0, vt;
	uint32_t pos, end;
	uint32_t time_ms;
	uint8_t type;
	uint8_t flags;
} mgsLine;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define mgsLine_ENABLED(o) \
	((o)->flags & (MGS_LINEP_STATE | MGS_LINEP_GOAL))

/** Needed before get, run, or skip when a line is not copy-initialized. */
static inline void mgsLine_setup(mgsLine *restrict o, uint32_t srate) {
	o->end = mgs_ms_in_samples(o->time_ms, srate);
}
void mgsLine_copy(mgsLine *restrict o,
		const mgsLine *restrict src,
		uint32_t srate);

uint32_t mgsLine_get(mgsLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool mgsLine_run(mgsLine *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool mgsLine_skip(mgsLine *restrict o, uint32_t skip_len);
