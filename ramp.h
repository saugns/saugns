/* sgensys: Value ramp module.
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
 * Ramp curves.
 */
enum {
	SGS_RAC_HOLD = 0,
	SGS_RAC_LIN,
	SGS_RAC_EXP,
	SGS_RAC_LOG,
	SGS_RAC_TYPES
};

/** Names of ramp curve types, with an extra NULL pointer at the end. */
extern const char *const SGS_RampCurve_names[SGS_RAC_TYPES + 1];

typedef void (*SGS_RampCurve_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time);

/** Functions for ramp curve types. */
extern const SGS_RampCurve_f SGS_RampCurve_funcs[SGS_RAC_TYPES];

void SGS_RampCurve_hold(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SGS_RampCurve_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SGS_RampCurve_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SGS_RampCurve_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);

/**
 * Ramp parameter flags.
 */
enum {
	SGS_RAMP_STATE       = 1<<0, // v0 set
	SGS_RAMP_STATE_RATIO = 1<<1,
	SGS_RAMP_CURVE       = 1<<2, // vt and time_ms set
	SGS_RAMP_CURVE_RATIO = 1<<3,
	SGS_RAMP_TIME_SET    = 1<<4, // manually used for tracking changes
};

/**
 * Ramp parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct SGS_Ramp {
	float v0, vt;
	uint32_t time_ms;
	uint8_t curve;
	uint8_t flags;
} SGS_Ramp;

/**
 * Get the main flags showing whether state and/or curve are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define SGS_Ramp_ENABLED(o) \
	((o)->flags & (SGS_RAMP_STATE | SGS_RAMP_CURVE))

void SGS_Ramp_reset(SGS_Ramp *restrict o);
void SGS_Ramp_copy(SGS_Ramp *restrict o,
		const SGS_Ramp *restrict src);

bool SGS_Ramp_run(SGS_Ramp *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf);
