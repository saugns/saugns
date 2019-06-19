/* saugns: Value ramp module.
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
	SAU_RAC_HOLD = 0,
	SAU_RAC_LIN,
	SAU_RAC_EXP,
	SAU_RAC_LOG,
	SAU_RAC_ESD,
	SAU_RAC_LSD,
	SAU_RAC_TYPES
};

/** Names of ramp curve types, with an extra NULL pointer at the end. */
extern const char *const SAU_RampCurve_names[SAU_RAC_TYPES + 1];

typedef void (*SAU_RampCurve_f)(float *restrict buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time);

/** Functions for ramp curve types. */
extern const SAU_RampCurve_f SAU_RampCurve_funcs[SAU_RAC_TYPES];

void SAU_RampCurve_hold(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_RampCurve_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_RampCurve_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_RampCurve_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_RampCurve_esd(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_RampCurve_lsd(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);

/**
 * Ramp parameter flags.
 */
enum {
	SAU_RAMP_STATE = 1<<0, // v0 set
	SAU_RAMP_STATE_RATIO = 1<<1,
	SAU_RAMP_CURVE = 1<<2, // vt and time_ms set
	SAU_RAMP_CURVE_RATIO = 1<<3,
};

/**
 * Ramp parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct SAU_Ramp {
	float v0, vt;
	uint32_t time_ms;
	uint8_t curve;
	uint8_t flags;
} SAU_Ramp;

/**
 * Get the main flags showing whether state and/or curve are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define SAU_Ramp_ENABLED(o) \
	((o)->flags & (SAU_RAMP_STATE | SAU_RAMP_CURVE))

void SAU_Ramp_reset(SAU_Ramp *restrict o);
void SAU_Ramp_copy(SAU_Ramp *restrict o,
		const SAU_Ramp *restrict src);

bool SAU_Ramp_run(SAU_Ramp *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf);
bool SAU_Ramp_skip(SAU_Ramp *restrict o,
		uint32_t skip_len, uint32_t srate, uint32_t *restrict pos);
