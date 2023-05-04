/* SAU library: Simple (soft-)clipping functionality.
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

#include <sau/clip.h>

const char *const sauClip_names[SAU_CLIP_NAMED + 1] = {
	SAU_CLIP__ITEMS(SAU_CLIP__X_NAME)
	NULL
};

const sauClip_apply_f sauClip_apply_funcs[SAU_CLIP_NAMED] = {
	SAU_CLIP__ITEMS(SAU_CLIP__X_APPLY_ADDR)
};

void sauClip_apply_off(float *restrict buf, size_t buf_len,
		float threshold) {
	(void)buf, (void)buf_len, (void)threshold;
}

void sauClip_apply_hard(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 1.f / fabsf(threshold);
	for (size_t i = 0; i < buf_len; ++i) {
		buf[i] = sau_fclampf(buf[i] * in_gain, -1.f, 1.f);
	}
}

void sauClip_apply_sa2(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / threshold;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 2*x - 1*x*x; // H 2
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa23(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / threshold;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 2*x*x - 1*x*x*x; // H 2, 3
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa3(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / fabsf(threshold);
	const float out_gain = 2.f;
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 3*x*x - 2*x*x*x; // H 3
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa24(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / threshold;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa234(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / threshold;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa34(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / threshold;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4 (2, 3, 4 at low vol)
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa35(float *restrict buf, size_t buf_len,
		float threshold) {
	const float in_gain = 0.5f / fabsf(threshold);
	const float out_gain = 2.f;
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

// Distortion effects
//		x = 2*x - 1*x*x; // H 2
//		x = 2*x*x - 1*x*x*x; // H 2, 3

// Prominent odd harmonics (the middle option sounds fuller and softer)
//		x = 3*x*x - 2*x*x*x; // H 3
//		x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4
//		x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5

// Prominent even harmonics
//		x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4
//		x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4 (2, 3, 4 at low vol)

//		x = 9*x*x*x - 15*x*x*x*x + 7*x*x*x*x*x; // ? 2, 3, 4, 5
