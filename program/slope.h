/* saugns: Value slope module.
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
#include "../common.h"

/**
 * Slope types.
 */
enum {
	SAU_SLOPE_HOLD = 0,
	SAU_SLOPE_LIN,
	SAU_SLOPE_EXP,
	SAU_SLOPE_LOG,
	SAU_SLOPE_TYPES
};

/** Names of slope types, with an extra NULL pointer at the end. */
extern const char *const SAU_Slope_names[SAU_SLOPE_TYPES + 1];

typedef void (*SAU_Slope_fill_f)(float *buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time);

/** Functions for slope types. */
extern const SAU_Slope_fill_f SAU_Slope_funcs[SAU_SLOPE_TYPES];

void SAU_Slope_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Slope_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Slope_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
void SAU_Slope_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time);
