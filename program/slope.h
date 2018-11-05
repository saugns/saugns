/* sgensys: Value slope module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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
	SGS_SLOPE_HOLD = 0,
	SGS_SLOPE_LIN,
	SGS_SLOPE_EXP,
	SGS_SLOPE_LOG,
	SGS_SLOPE_TYPES
};

/** Names of slope types, with an extra NULL pointer at the end. */
extern const char *const SGS_Slope_names[SGS_SLOPE_TYPES + 1];

typedef void (*SGS_Slope_fill_f)(float *buf, uint32_t len,
		float v0, float vt, uint32_t pos, uint32_t time);

/** Functions for slope types. */
extern const SGS_Slope_fill_f SGS_Slope_funcs[SGS_SLOPE_TYPES];

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
