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
		float gain) {
	(void)buf, (void)buf_len, (void)gain;
}

void sauClip_apply_hard(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = fabsf(gain);
	for (size_t i = 0; i < buf_len; ++i) {
		buf[i] = sau_fclampf(buf[i] * in_gain, -1.f, 1.f);
	}
}

void sauClip_apply_ds2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 2*x - 1*x*x; // H 2
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_ds2b(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 3*x - 3*x*x + 1*x*x*x; // H 2, 3
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_dm3(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 2*x - 2*x*x + 1*x*x*x; // H 2, 3
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_dm4(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_dm4_2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4 (2, 3, 4 at low vol)
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa3(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * fabsf(gain);
	const float out_gain = 2.f;
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 3*x*x - 2*x*x*x; // H 3
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa4(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4 (more 3rd)
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa4_2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * gain;
	const float out_gain = copysignf(2.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 5*x*x - 6*x*x*x + 2*x*x*x*x; // H 2, 3, 4 (more 2nd, 4th)
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

void sauClip_apply_sa5(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = 0.5f * fabsf(gain);
	const float out_gain = 2.f;
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain + 0.5f;
		x = sau_fclampf(x, 0.f, 1.f);
		x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5
		x = (x - 0.5f) * out_gain;
		buf[i] = x;
	}
}

// Distortion effects, strong
//		x = 2*x - 1*x*x; // H 2
//		x = 3*x - 3*x*x + 1*x*x*x; // H 2, 3

// Distortion effects, mellow
//		x = 2*x - 2*x*x + 1*x*x*x; // H 2, 3
//		x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4
//		x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4 (2, 3, 4 at low vol)

// Soft-saturate
//		x = 3*x*x - 2*x*x*x; // H 3
//		x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4 (more 3rd)
//		x = 5*x*x - 6*x*x*x + 2*x*x*x*x; // H 2, 3, 4 (more 2nd, 4th)
//		x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5

//		x = 9*x*x*x - 15*x*x*x*x + 7*x*x*x*x*x; // ? 2, 3, 4, 5
