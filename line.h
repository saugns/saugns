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

/* Macro used to declare and define line fill sets of items. */
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
#define MGS_LINE__X_PROTOTYPE(NAME) \
void MGS_Line_fill_##NAME(float *restrict buf, uint32_t len, \
		float v0, float vt, uint32_t pos, uint32_t time, \
		const float *restrict mulbuf);
#define MGS_LINE__X_ADDRESS(NAME) MGS_Line_fill_##NAME,

/**
 * Line fill types.
 */
enum {
	MGS_LINE__ITEMS(MGS_LINE__X_ID)
	MGS_LINE_NAMED
};

MGS_LINE__ITEMS(MGS_LINE__X_PROTOTYPE)

/** Names of line fill types, with an extra NULL pointer at the end. */
extern const char *const MGS_Line_names[MGS_LINE_NAMED + 1];

typedef void (*MGS_Line_fill_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time,
		const float *restrict mulbuf);

/** Fill functions for line fill types. */
extern const MGS_Line_fill_f MGS_Line_fill_funcs[MGS_LINE_NAMED];

/**
 * Line parameter flags.
 */
enum {
	MGS_LINEP_STATE       = 1<<0, // v0 set
	MGS_LINEP_STATE_RATIO = 1<<1,
	MGS_LINEP_GOAL        = 1<<2, // vt set -- and timed fill will be done
	MGS_LINEP_GOAL_RATIO  = 1<<3,
	MGS_LINEP_FILL_TYPE   = 1<<4, // fill_type set
	MGS_LINEP_TIME        = 1<<5, // time_ms set -- cleared on time expiry
	MGS_LINEP_TIME_IF_NEW = 1<<6, // time_ms to be kept if currently set
};

/**
 * Line parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct MGS_Line {
	float v0, vt;
	uint32_t pos, end;
	uint32_t time_ms;
	uint8_t fill_type;
	uint8_t flags;
} MGS_Line;

/**
 * Get the main flags showing whether state and/or goal are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define MGS_Line_ENABLED(o) \
	((o)->flags & (MGS_LINEP_STATE | MGS_LINEP_GOAL))

/** Needed before get, run, or skip when a line is not copy-initialized. */
static inline void MGS_Line_setup(MGS_Line *restrict o, uint32_t srate) {
	o->end = MGS_ms_in_samples(o->time_ms, srate);
}
void MGS_Line_copy(MGS_Line *restrict o,
		const MGS_Line *restrict src,
		uint32_t srate);

uint32_t MGS_Line_get(MGS_Line *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool MGS_Line_run(MGS_Line *restrict o,
		float *restrict buf, uint32_t buf_len,
		const float *restrict mulbuf);
bool MGS_Line_skip(MGS_Line *restrict o, uint32_t skip_len);
