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

/*
 * For a polynomial with even positive powers, the DC offset constant is
 * the sum of the coefficients used for even powers, with sign reversed.
 * This ensures output for +/- 1.0 input range also has a +/- 1.0 range.
 */

/** 2th-degree polynomial abs() approximation, -1 <= x <= 1. */
static inline float abs_d2(float x) {
	return x*x;
}

/** 4th-degree polynomial abs() approximation, -1 <= x <= 1. */
static inline float abs_d4(float x) {
	float x2 = x*x;
	return 2*x2 - x2*x2;
}

/** 6th-degree polynomial abs() approximation, -1 <= x <= 1. */
static inline float abs_d6(float x) {
	float x2 = x*x;
	return 3*x2 - 4*x2*x2 + 2*x2*x2*x2;
//	return 2.5*x2 - 2*x2*x2 + 0.5*x2*x2*x2;
}

/** Squared 3rd-degree smoothstep
    6th-degree polynomial abs() approximation, -1 <= x <= 1. */
static inline float lowabs(float x) {
//	return abs_d4(x);
	x = 1.5f*x - 0.5f*x*x*x;
	return x*x;
//	float x2 = x*x;
//	return 2.5f*x2 - 2*x2*x2 + 0.5f*x2*x2*x2;
}

void sauClip_apply_fr2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = (abs_d2(x) - 0.5f)*2;
		buf[i] = x * out_gain;
	}
}

void sauClip_apply_fr6(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = (lowabs(x) - 0.5f)*2;
		buf[i] = x * out_gain;
	}
}

void sauClip_apply_hr2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = (0.25f + x + abs_d2(x)) * (0.5f / (1.f+0.25f/2));//- 1.f + 0.25f)*0.5f;
		buf[i] = x * out_gain;
	}
}

void sauClip_apply_hr6(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 0.05452073256422515176f +
		    ((x + lowabs(x)) * 0.47273963371788742412f);
		buf[i] = x * out_gain;
	}
}

void sauClip_apply_ds2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 0.5f + 1.0f*x - 0.5f*x*x; // H 2
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 2*x - 1*x*x; // H 2

void sauClip_apply_ds2b(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 0.75f + 0.75f*x - 0.75f*x*x + 0.25f*x*x*x; // H 2, 3
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 3*x - 3*x*x + 1*x*x*x; // H 2, 3

void sauClip_apply_dpgm(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		float x2 = x*x, tmp = x - x2;
		x = 1.f/6 + x*5.f/6 + (tmp + x2*tmp*tmp*0.25f)*1.f/3;
		buf[i] = x * out_gain;
	}
}

void sauClip_apply_dm3(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 0.25f + 0.75f*x - 0.25f*x*x + 0.25f*x*x*x; // H 2, 3
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 2*x - 2*x*x + 1*x*x*x; // H 2, 3

void sauClip_apply_dm4(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = -0.125f + x - 0.25f*x*x + 0.375f*x*x*x*x; // H 2, 4
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 4*x*x - 6*x*x*x + 3*x*x*x*x; // H 2, 4

void sauClip_apply_dm4_2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 1.25f*x - 0.25f*x*x - 0.25f*x*x*x + 0.25f*x*x*x*x; // H 3, 4 (2, 3, 4 at low vol)
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 4*x*x - 5*x*x*x + 2*x*x*x*x; // H 3, 4 (2, 3, 4 at low vol)

void sauClip_apply_sa3(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = fabsf(gain);
	const float out_gain = 1.f;
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 1.5f*x - 0.5f*x*x*x; // H 3
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 3*x*x - 2*x*x*x; // H 3

void sauClip_apply_sa4(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 0.125f + 1.5f*x - 0.25f*x*x - 0.5f*x*x*x + 0.125f*x*x*x*x; // H 2, 3, 4 (more 3rd)
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 4*x*x - 4*x*x*x + 1*x*x*x*x; // H 2, 3, 4 (more 3rd)

void sauClip_apply_sa4_2(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 0.25f + 1.5f*x - 0.5f*x*x - 0.5f*x*x*x + 0.25f*x*x*x*x; // H 2, 3, 4 (more 2nd, 4th)
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 5*x*x - 6*x*x*x + 2*x*x*x*x; // H 2, 3, 4 (more 2nd, 4th)

void sauClip_apply_sae(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = gain;
	const float out_gain = copysignf(1.f, in_gain);
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = x - 0.25f*x*x + 0.5f*x*x*x*x - 0.25f*x*x*x*x*x*x;
		buf[i] = x * out_gain;
	}
}

void sauClip_apply_sa5(float *restrict buf, size_t buf_len,
		float gain) {
	const float in_gain = fabsf(gain);
	const float out_gain = 1.f;
	for (size_t i = 0; i < buf_len; ++i) {
		float x = buf[i] * in_gain;
		x = sau_fclampf(x, -1.f, 1.f);
		x = 1.875f*x - 1.25f*x*x*x + 0.375f*x*x*x*x*x; // H 3, 5
		buf[i] = x * out_gain;
	}
}
//	0 <= x <= 1:
//		x = 10*x*x*x - 15*x*x*x*x + 6*x*x*x*x*x; // H 3, 5

//		x = 9*x*x*x - 15*x*x*x*x + 7*x*x*x*x*x; // ? 2, 3, 4, 5
