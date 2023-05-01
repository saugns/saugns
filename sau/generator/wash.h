/* SAU library: Waveshaping routines for extra functionality.
 * Copyright (c) 2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <sau/math.h>

enum {
	CLIP_HARD,
	CLIP_N_sa2,
	CLIP_N_sa23,
	CLIP_N_sa234,
	CLIP_N_sa24,
	CLIP_N_sa3,
	CLIP_N_sa34,
	CLIP_N_sa35,
};

struct WaveshapeOptions {
	float clip_threshold;
	uint8_t clip_type;
};

static sauMaybeUnused void softclip_sa2(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 2*x - 1*x*x; // H 2
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

static sauMaybeUnused void softclip_sa23(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 2*x*x - 1*x*x*x; // H 2, 3
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

static sauMaybeUnused void softclip_sa3(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 3*x*x - 2*x*x*x; // H 3
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

static sauMaybeUnused void softclip_sa234(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

static sauMaybeUnused void softclip_sa24(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

static sauMaybeUnused void softclip_sa34(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

static sauMaybeUnused void softclip_sa35(float *restrict buf, size_t buf_len) {
	for (size_t i = 0; i < buf_len; ++i) {
		float x = (buf[i] + 1.f) * 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5
		x = (x - 0.5f) * 2.f;
		buf[i] = x;
	}
}

		//x = 2*x*x - 1*x*x*x; // H 2, 3
		//x = 3*x*x - 2*x*x*x; // H 3
		//x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4
		//x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4
		//x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4
		//x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5
		//x = 9*x*x*x - 15*x*x*x*x + 7*x*x*x*x*x; // ? 2, 3, 4, 5

