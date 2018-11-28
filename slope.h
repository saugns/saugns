/* sgensys: Value slope module.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "common.h"

/**
 * Slope types.
 */
enum {
	SGS_SLOPE_HOLD = 0,
	SGS_SLOPE_LIN,
	SGS_SLOPE_EXP,
	SGS_SLOPE_LOG,
	SGS_SLOPE_TYPES
};

/** Names of slope types, with an extra NULL pointer at the end. */
extern const char *const SGS_Slope_names[SGS_SLOPE_TYPES + 1];

typedef void (*SGS_SlopeFill_f)(float *buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time);

/** Functions for slope types. */
extern const SGS_SlopeFill_f SGS_Slope_fills[SGS_SLOPE_TYPES];

void SGS_Slope_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SGS_Slope_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SGS_Slope_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SGS_Slope_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);

/**
 * Slope parameter flags.
 */
enum {
	SGS_SLP_STATE = 1<<0, // v0 set
	SGS_SLP_STATE_RATIO = 1<<1,
	SGS_SLP_SLOPE = 1<<2, // vt and time_ms set
	SGS_SLP_SLOPE_RATIO = 1<<3,
};

/**
 * Slope parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct SGS_Slope {
	float v0, vt;
	uint32_t time_ms;
	uint8_t slope;
	uint8_t flags;
} SGS_Slope;

/**
 * Get the main flags showing whether state and/or slope are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define SGS_Slope_ENABLED(o) \
	((o)->flags & (SGS_SLP_STATE | SGS_SLP_SLOPE))

void SGS_Slope_reset(SGS_Slope *restrict o);
void SGS_Slope_copy(SGS_Slope *restrict o,
		const SGS_Slope *restrict src);

bool SGS_Slope_run(SGS_Slope *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf);
