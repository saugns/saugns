/* sgensys: oscillator module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "osc.h"

#define HALFLEN (SGSOsc_LUT_LEN>>1)

SGSOscLUT SGSOsc_luts[SGS_WAVE_TYPES];

/**
 * Fill in the look-up tables enumerated by SGS_WAVE_*.
 *
 * If already initialized, return without doing anything.
 */
void SGSOsc_init_luts(void) {
	static bool done = false;
	if (done) return;
	done = true;

	SGSOscLUV *const sin_lut = SGSOsc_luts[SGS_WAVE_SIN];
	SGSOscLUV *const srs_lut = SGSOsc_luts[SGS_WAVE_SRS];
	SGSOscLUV *const tri_lut = SGSOsc_luts[SGS_WAVE_TRI];
	SGSOscLUV *const sqr_lut = SGSOsc_luts[SGS_WAVE_SQR];
	SGSOscLUV *const saw_lut = SGSOsc_luts[SGS_WAVE_SAW];
	int i;
	/* first half */
	for (i = 0; i < HALFLEN; ++i) {
		double sinval = sin(PI * i/HALFLEN);
		sin_lut[i] = SGSOsc_LUT_MAX * sinval;
		srs_lut[i] = SGSOsc_LUT_MAX * sqrtf(sinval);
		if (i < (HALFLEN>>1))
			tri_lut[i] = SGSOsc_LUT_MAX * (2.f * i/HALFLEN);
		else
			tri_lut[i] = SGSOsc_LUT_MAX * (2.f * (HALFLEN-i)/HALFLEN);
		sqr_lut[i] = SGSOsc_LUT_MAX;
		saw_lut[i] = SGSOsc_LUT_MAX * (1.f * (HALFLEN-i)/HALFLEN);
	}
	/* second half */
	for (; i < SGSOsc_LUT_LEN; ++i) {
		sin_lut[i] = -sin_lut[i - HALFLEN];
		srs_lut[i] = -srs_lut[i - HALFLEN];
		tri_lut[i] = -tri_lut[i - HALFLEN];
		sqr_lut[i] = -sqr_lut[i - HALFLEN];
		saw_lut[i] = -saw_lut[(SGSOsc_LUT_LEN-1) - i];
	}
}
