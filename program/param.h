/* saugns: Script parameter module.
 * Copyright (c) 2018-2019 Joel K. Pettersson
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
#include "slope.h"

/**
 * Timed parameter flags.
 */
enum {
	SAU_TPAR_STATE = 1<<0, // v0 set
	SAU_TPAR_STATE_RATIO = 1<<1,
	SAU_TPAR_SLOPE = 1<<2, // vt and time_ms set
	SAU_TPAR_SLOPE_RATIO = 1<<3,
};

/**
 * Timed parameter type.
 *
 * Holds data for parameters with support for gradual change,
 * both during script processing and audio rendering.
 */
typedef struct SAU_TimedParam {
	float v0, vt;
	uint32_t time_ms;
	uint8_t slope;
	uint8_t flags;
} SAU_TimedParam;

/**
 * Get the main flags showing whether state and/or slope are enabled.
 * Zero implies that the instance is unused.
 *
 * \return flag values
 */
#define SAU_TimedParam_ENABLED(o) \
	((o)->flags & (SAU_TPAR_STATE | SAU_TPAR_SLOPE))

void SAU_TimedParam_reset(SAU_TimedParam *restrict o);
void SAU_TimedParam_copy(SAU_TimedParam *restrict o,
		const SAU_TimedParam *restrict src);

bool SAU_TimedParam_run(SAU_TimedParam *restrict o, float *restrict buf,
		uint32_t buf_len, uint32_t srate,
		uint32_t *restrict pos, const float *restrict mulbuf);
