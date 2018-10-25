/* saugns: Wave module.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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

#include "wave.h"
#include "../math.h"
#include <stdio.h>

#define HALFLEN (SAU_Wave_LEN>>1)

float SAU_Wave_luts[SAU_WAVE_TYPES][SAU_Wave_LEN];

const char *const SAU_Wave_names[SAU_WAVE_TYPES + 1] = {
	"sin",
	"sqr",
	"tri",
	"saw",
	"sha",
	"szh",
	"shh",
	"ssr",
//	"szhhr",
	NULL
};

/**
 * Fill in the look-up tables enumerated by SAU_WAVE_*.
 *
 * If already initialized, return without doing anything.
 */
void SAU_global_init_Wave(void) {
	static bool done = false;
	if (done) return;
	done = true;

	float *const sin_lut = SAU_Wave_luts[SAU_WAVE_SIN];
	float *const sqr_lut = SAU_Wave_luts[SAU_WAVE_SQR];
	float *const tri_lut = SAU_Wave_luts[SAU_WAVE_TRI];
	float *const saw_lut = SAU_Wave_luts[SAU_WAVE_SAW];
	float *const sha_lut = SAU_Wave_luts[SAU_WAVE_SHA];
	float *const szh_lut = SAU_Wave_luts[SAU_WAVE_SZH];
	float *const shh_lut = SAU_Wave_luts[SAU_WAVE_SHH];
	float *const ssr_lut = SAU_Wave_luts[SAU_WAVE_SSR];
//	float *const szhhr_lut = SAU_Wave_luts[SAU_WAVE_SZHHR];
	int i;
	const double val_scale = SAU_Wave_MAXVAL;
	const double len_scale = 1.f / HALFLEN;
	/*
	 * First half:
	 *  - sin
	 *  - sqr
	 *  - tri
	 *  - saw
	 *  - ssr
	 */
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * len_scale;
		const double x_rev = (HALFLEN-i) * len_scale;

		const double sin_x = sin(SAU_PI * x);
		sin_lut[i] = val_scale * sin_x;

		sqr_lut[i] = SAU_Wave_MAXVAL;

		if (i < (HALFLEN>>1))
			tri_lut[i] = val_scale * 2.f * x;
		else
			tri_lut[i] = val_scale * 2.f * x_rev;

		saw_lut[i] = SAU_Wave_MINVAL + val_scale * x;

		ssr_lut[i] = val_scale * sqrt(sin_x);
	}
	/* Second half:
	 *  - sin
	 *  - sqr
	 *  - tri
	 *  - saw
	 *  - ssr
	 */
	for (; i < SAU_Wave_LEN; ++i) {
		sin_lut[i] = -sin_lut[i - HALFLEN];

		sqr_lut[i] = -SAU_Wave_MAXVAL;

		tri_lut[i] = -tri_lut[i - HALFLEN];

		saw_lut[i] = -saw_lut[(SAU_Wave_LEN-1) - i];

		ssr_lut[i] = -ssr_lut[i - HALFLEN];
	}
	/* Full cycle:
	 *  - sha
	 *  - szh
	 *  - shh
//	 *  - szhhr
	 */
	for (i = 0; i < SAU_Wave_LEN; ++i) {
		const double x = i * len_scale;

		double sha_x = sin((SAU_PI * x) * 0.5f + SAU_ASIN_1_2);
		sha_x = fabs(sha_x) - 0.5f;
		sha_x += sha_x;
		sha_lut[i] = val_scale * sha_x;

		double szh_x = sin((SAU_PI * x) + SAU_ASIN_1_2);
		if (szh_x > 0.f) {
			szh_x -= 0.5f;
			szh_x += szh_x;
			szh_lut[i] = val_scale * szh_x;
//			double szhhr_x = (szh_x > 0.f) ?
//				sqrt(szh_x) :
//				szh_x;
//			szhhr_lut[i] = val_scale * szhhr_x;
		} else {
			szh_lut[i] = -SAU_Wave_MAXVAL;
//			szhhr_lut[i] = -SAU_Wave_MAXVAL;
		}

		double shh_x = sin((SAU_PI * x) * 0.25f);
		shh_x -= 0.5f;
		shh_x += shh_x;
		shh_lut[i] = val_scale * shh_x;
	}
}

/**
 * Print an index-value table for a LUT.
 */
void SAU_Wave_print(uint8_t id) {
	if (id >= SAU_WAVE_TYPES) return;

	const float *lut = SAU_Wave_luts[id];
	const char *lut_name = SAU_Wave_names[id];
	fprintf(stderr, "LUT: %s\n", lut_name);
	for (int i = 0; i < SAU_Wave_LEN; ++i) {
		float v = lut[i];
		fprintf(stderr, "[\t%d]: \t%.11f\n", i, v);
	}
}
