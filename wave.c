/* sgensys: Wave module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "wave.h"
#include "math.h"
#include <stdio.h>

#define HALFLEN (SGS_Wave_LEN>>1)

int16_t SGS_Wave_luts[SGS_WAVE_TYPES][SGS_Wave_LEN];

const char *const SGS_Wave_names[SGS_WAVE_TYPES + 1] = {
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
 * Fill in the look-up tables enumerated by SGS_WAVE_*.
 *
 * If already initialized, return without doing anything.
 */
void SGS_global_init_Wave(void) {
	static bool done = false;
	if (done) return;
	done = true;

	int16_t *const sin_lut = SGS_Wave_luts[SGS_WAVE_SIN];
	int16_t *const sqr_lut = SGS_Wave_luts[SGS_WAVE_SQR];
	int16_t *const tri_lut = SGS_Wave_luts[SGS_WAVE_TRI];
	int16_t *const saw_lut = SGS_Wave_luts[SGS_WAVE_SAW];
	int16_t *const sha_lut = SGS_Wave_luts[SGS_WAVE_SHA];
	int16_t *const szh_lut = SGS_Wave_luts[SGS_WAVE_SZH];
	int16_t *const shh_lut = SGS_Wave_luts[SGS_WAVE_SHH];
	int16_t *const ssr_lut = SGS_Wave_luts[SGS_WAVE_SSR];
//	int16_t *const szhhr_lut = SGS_Wave_luts[SGS_WAVE_SZHHR];
	int i;
	const double val_scale = SGS_Wave_MAXVAL;
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

		const double sin_x = sin(SGS_PI * x);
		sin_lut[i] = lrint(val_scale * sin_x);

		sqr_lut[i] = SGS_Wave_MAXVAL;

		if (i < (HALFLEN>>1))
			tri_lut[i] = lrint(val_scale * 2.f * x);
		else
			tri_lut[i] = lrint(val_scale * 2.f * x_rev);

		saw_lut[i] = SGS_Wave_MINVAL + lrint(val_scale * x);

		ssr_lut[i] = lrint(val_scale * sqrt(sin_x));
	}
	/* Second half:
	 *  - sin
	 *  - sqr
	 *  - tri
	 *  - saw
	 *  - ssr
	 */
	for (; i < SGS_Wave_LEN; ++i) {
		sin_lut[i] = -sin_lut[i - HALFLEN];

		sqr_lut[i] = -SGS_Wave_MAXVAL;

		tri_lut[i] = -tri_lut[i - HALFLEN];

		saw_lut[i] = -saw_lut[(SGS_Wave_LEN-1) - i];

		ssr_lut[i] = -ssr_lut[i - HALFLEN];
	}
	/* Full cycle:
	 *  - sha
	 *  - szh
	 *  - shh
//	 *  - szhhr
	 */
	for (i = 0; i < SGS_Wave_LEN; ++i) {
		const double x = i * len_scale;

		double sha_x = sin((SGS_PI * x) * 0.5f + SGS_ASIN_1_2);
		sha_x = fabs(sha_x) - 0.5f;
		sha_x += sha_x;
		sha_lut[i] = lrint(val_scale * sha_x);

		double szh_x = sin((SGS_PI * x) + SGS_ASIN_1_2);
		if (szh_x > 0.f) {
			szh_x -= 0.5f;
			szh_x += szh_x;
			szh_lut[i] = lrint(val_scale * szh_x);
//			double szhhr_x = (szh_x > 0.f) ?
//				sqrt(szh_x) :
//				szh_x;
//			szhhr_lut[i] = lrint(val_scale * szhhr_x);
		} else {
			szh_lut[i] = -SGS_Wave_MAXVAL;
//			szhhr_lut[i] = -SGS_Wave_MAXVAL;
		}

		double shh_x = sin((SGS_PI * x) * 0.25f);
		shh_x -= 0.5f;
		shh_x += shh_x;
		shh_lut[i] = lrint(val_scale * shh_x);
	}
}

/**
 * Print an index-value table for a LUT.
 */
void SGS_Wave_print(uint8_t id) {
	if (id >= SGS_WAVE_TYPES) return;

	const int16_t *lut = SGS_Wave_luts[id];
	const char *lut_name = SGS_Wave_names[id];
	fprintf(stderr, "LUT: %s\n", lut_name);
	for (int i = 0; i < SGS_Wave_LEN; ++i) {
		int v = (int) lut[i];
		fprintf(stderr, "[\t%d]: \t%d\n", i, v);
	}
}
