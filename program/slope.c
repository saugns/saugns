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

#include "slope.h"
#include "../math.h"

const char *const SGS_Slope_names[SGS_SLOPE_TYPES + 1] = {
	"hold",
	"lin",
	"exp",
	"log",
	NULL
};

const SGS_Slope_fill_f SGS_Slope_funcs[SGS_SLOPE_TYPES] = {
	SGS_Slope_fill_hold,
	SGS_Slope_fill_lin,
	SGS_Slope_fill_exp,
	SGS_Slope_fill_log,
};

/**
 * Fill \p buf with \p len values along a straight horizontal line,
 * i.e. \p len copies of \p v0.
 */
void SGS_Slope_fill_hold(float *restrict buf, uint32_t len,
		float v0, float vt SGS__maybe_unused,
		uint32_t pos SGS__maybe_unused, uint32_t time SGS__maybe_unused) {
	uint32_t i;
	for (i = 0; i < len; ++i)
		buf[i] = v0;
}

/**
 * Fill \p buf with \p len values along a linear trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 */
void SGS_Slope_fill_lin(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const double inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		(*buf++) = v0 + (vt - v0) * (i * inv_time);
	}
}

/**
 * Fill \p buf with \p len values along an exponential trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural.
 * (Unlike a real exponential curve, it has a definite beginning
 * and end. It is symmetric to the corresponding logarithmic curve.)
 */
void SGS_Slope_fill_exp(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const double inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		double mod = 1.f - i * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
		      (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		(*buf++) = vt + (v0 - vt) * mod;
	}
}

/**
 * Fill \p buf with \p len values along a logarithmic trajectory
 * from \p v0 (at position 0) to \p vt (at position \p time),
 * beginning at position \p pos.
 *
 * Uses an ear-tuned polynomial, designed to sound natural.
 * (Unlike a real logarithmic curve, it has a definite beginning
 * and end. It is symmetric to the corresponding exponential curve.)
 */
void SGS_Slope_fill_log(float *restrict buf, uint32_t len,
		float v0, float vt,
		uint32_t pos, uint32_t time) {
	const double inv_time = 1.f / time;
	uint32_t i, end;
	for (i = pos, end = i + len; i < end; ++i) {
		double mod = i * inv_time,
			modp2 = mod * mod,
			modp3 = modp2 * mod;
		mod = modp3 + (modp2 * modp3 - modp2) *
		      (mod * (629.f/1792.f) + modp2 * (1163.f/1792.f));
		(*buf++) = v0 + (vt - v0) * mod;
	}
}
