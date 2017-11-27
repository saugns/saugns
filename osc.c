/* sgensys: Oscillator module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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

#include "osc.h"
#include <stdio.h>

#define HALFLEN (SGSOsc_LUT_LEN>>1)

SGSOscLUT SGSOsc_luts[SGS_WAVE_TYPES];

const char *const SGSOsc_lut_names[SGS_WAVE_TYPES + 1] = {
	"sin",
	"tri",
	"sqr",
	"saw",
	"sab",
	"shw",
	"ssr",
	"shr",
	NULL
};

/**
 * Fill in the look-up tables enumerated by SGS_WAVE_*.
 *
 * If already initialized, return without doing anything.
 */
void SGSOsc_global_init(void) {
	static bool done = false;
	if (done) return;
	done = true;

	int16_t *const sin_lut = SGSOsc_luts[SGS_WAVE_SIN];
	int16_t *const tri_lut = SGSOsc_luts[SGS_WAVE_TRI];
	int16_t *const sqr_lut = SGSOsc_luts[SGS_WAVE_SQR];
	int16_t *const saw_lut = SGSOsc_luts[SGS_WAVE_SAW];
	int16_t *const sab_lut = SGSOsc_luts[SGS_WAVE_SAB];
	int16_t *const shw_lut = SGSOsc_luts[SGS_WAVE_SHW];
	int16_t *const ssr_lut = SGSOsc_luts[SGS_WAVE_SSR];
	int16_t *const shr_lut = SGSOsc_luts[SGS_WAVE_SHR];
	int i;
	const double val_scale = SGSOsc_LUT_MAXVAL;
	const double len_scale = 1.f / HALFLEN;
	const double asin_0_5 = asin(0.5f);
	/* first half */
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * len_scale;
		const double x_rev = (HALFLEN-i) * len_scale;

		const double sin_x = sin(PI * x);
		sin_lut[i] = lrint(val_scale * sin_x);

		if (i < (HALFLEN>>1))
			tri_lut[i] = lrint(val_scale * 2.f * x);
		else
			tri_lut[i] = lrint(val_scale * 2.f * x_rev);

		sqr_lut[i] = SGSOsc_LUT_MAXVAL;

		saw_lut[i] = lrint(val_scale * x_rev);

		double sab_x = sin((PI * x) * 0.5f + asin_0_5);
		sab_x = fabs(sab_x) - 0.5f;
		sab_x += sab_x;
		sab_lut[i] = lrint(val_scale * sab_x);

		double shw_x = sin((PI * x) + asin_0_5);
		if (shw_x > 0.f) {
			shw_x -= 0.5f;
			shw_x += shw_x;
			shw_lut[i] = lrint(val_scale * shw_x);
			double shr_x = (shw_x > 0.f) ?
				sqrt(shw_x) :
				shw_x;
			shr_lut[i] = lrint(val_scale * shr_x);
		} else {
			shw_lut[i] = -SGSOsc_LUT_MAXVAL;
			shr_lut[i] = -SGSOsc_LUT_MAXVAL;
		}

		ssr_lut[i] = lrint(val_scale * sqrt(sin_x));
	}
	/* second half */
	for (; i < SGSOsc_LUT_LEN; ++i) {
		const double x = i * len_scale;
		//const double x_rev = (SGSOsc_LUT_LEN-i) * len_scale;

		sin_lut[i] = -sin_lut[i - HALFLEN];

		tri_lut[i] = -tri_lut[i - HALFLEN];

		sqr_lut[i] = -SGSOsc_LUT_MAXVAL;

		saw_lut[i] = -saw_lut[(SGSOsc_LUT_LEN-1) - i];

		double sab_x = sin((PI * x) * 0.5f + asin_0_5);
		sab_x = fabs(sab_x) - 0.5f;
		sab_x += sab_x;
		sab_lut[i] = lrint(val_scale * sab_x);

		double shw_x = sin((PI * x) + asin_0_5);
		if (shw_x > 0.f) {
			shw_x -= 0.5f;
			shw_x += shw_x;
			shw_lut[i] = lrint(val_scale * shw_x);
			double shr_x = (shw_x > 0.f) ?
				sqrt(shw_x) :
				-(sqrt(-shw_x));
			shr_lut[i] = lrint(val_scale * shr_x);
		} else {
			shw_lut[i] = -SGSOsc_LUT_MAXVAL;
			shr_lut[i] = -SGSOsc_LUT_MAXVAL;
		}

		ssr_lut[i] = -ssr_lut[i - HALFLEN];
	}
}

/**
 * Print an index-value table for a LUT.
 */
void SGSOsc_print_lut(SGS_wave_t id) {
	if (id >= SGS_WAVE_TYPES) return;

	SGSOscLUT_t lut = SGSOsc_luts[id];
	const char *lut_name = SGSOsc_lut_names[id];
	printf("LUT: %s\n", lut_name);
	for (int i = 0; i < SGSOsc_LUT_LEN; ++i) {
		int v = (int) lut[i];
		printf("[\t%d]: \t%d\n", i, v);
	}
}
