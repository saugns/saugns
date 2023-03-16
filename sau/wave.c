/* SAU library: Wave module.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
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

#include <sau/wave.h>
#include <sau/math.h>
#include <stdio.h>

#define HALFLEN (sauWave_LEN>>1)
#define QUARTERLEN (sauWave_LEN>>2)
#define DVSCALE (sauWave_LEN * 0.125f)
#define IVSCALE (1.f / DVSCALE)

static float sin_lut[sauWave_LEN];

static float sqr_lut[sauWave_LEN];
static float tri_lut[sauWave_LEN];
static float pitri_lut[sauWave_LEN];

static float eto_lut[sauWave_LEN];
static float ean_lut[sauWave_LEN];
static float piean_lut[sauWave_LEN];

static float saw_lut[sauWave_LEN];
static float par_lut[sauWave_LEN];
static float pipar_lut[sauWave_LEN];

static float srs_lut[sauWave_LEN], pisrs_lut[sauWave_LEN];
static float cat_lut[sauWave_LEN], picat_lut[sauWave_LEN];
static float mto_lut[sauWave_LEN], pimto_lut[sauWave_LEN];
static float hsi_lut[sauWave_LEN], pihsi_lut[sauWave_LEN];
static float spa_lut[sauWave_LEN], pispa_lut[sauWave_LEN];
static float siw_lut[sauWave_LEN], pisiw_lut[sauWave_LEN];
static float shs_lut[sauWave_LEN], pishs_lut[sauWave_LEN];
static float ssr_lut[sauWave_LEN], pissr_lut[sauWave_LEN];

#define SAU_WAVE__X_LUT_NAME(NAME, COEFFS) NAME##_lut,

float *const sauWave_luts[SAU_WAVE_NAMED] = {
	SAU_WAVE__ITEMS(SAU_WAVE__X_LUT_NAME)
};

float *const sauWave_piluts[SAU_WAVE_NAMED] = {
	sin_lut,
	pitri_lut,
	pisrs_lut,
	tri_lut,
	piean_lut,
	picat_lut,
	ean_lut,
	pipar_lut,
	pimto_lut,
	par_lut,
	pihsi_lut,
	pispa_lut,
	pisiw_lut,
	pishs_lut,
	pissr_lut,
};

const struct sauWaveCoeffs sauWave_picoeffs[SAU_WAVE_NAMED] = {
	SAU_WAVE__ITEMS(SAU_WAVE__X_COEFFS)
};

const char *const sauWave_names[SAU_WAVE_NAMED + 1] = {
	SAU_WAVE__ITEMS(SAU_WAVE__X_NAME)
	NULL
};

/*
 * Fill \p lut with integrated version of \p in_lut,
 * adjusted to have a peak amplitude of +/- \p scale.
 */
static void fill_It(float *restrict lut, size_t len, const float scale,
		const float *restrict in_lut) {
	double in_dc = 0.f;
	for (size_t i = 0; i < len; ++i) {
		in_dc += in_lut[i];
	}
	in_dc /= len;
	double in_sum = 0.f;
	float lb = 0.f, ub = 0.f;
	for (size_t i = 0; i < len; ++i) {
		in_sum += in_lut[i] - in_dc;
		float x = in_sum * IVSCALE;
		if (x < lb) lb = x;
		if (x > ub) ub = x;
		lut[i] = x;
	}
	float out_scale = scale / ((ub - lb) * 0.5f);
	float out_dc = -(ub + lb) * 0.5f;
	for (size_t i = 0; i < len; ++i) {
		lut[i] = (lut[i] + out_dc) * out_scale;
	}
}

/**
 * Fill in the look-up tables enumerated by SAU_WAVE_*.
 *
 * If already initialized, return without doing anything.
 */
void sau_global_init_Wave(void) {
	static bool done = false;
	if (done)
		return;
	done = true;

	int i;
	const float val_scale = sauWave_MAXVAL;
	/*
	 * Fully fill:
	 *  - sin, It -cosin
	 *  - par, It pipar
	 *  - spa, It pispa
	 *
	 * First half:
	 *  - tri, It pitri
	 *  - srs, It pisrs
	 *  - sqr, It -cotri
	 *  - mto, It pimto
	 *  - saw, -It copar
	 *  - hsi, It pihsi
	 */
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * (1.f/HALFLEN);
		//const double x_rev = (HALFLEN-i) * (1.f/HALFLEN);

		const float sin_x = sin(SAU_PI * x);
		sin_lut[i] = val_scale * sin_x;
		sin_lut[i + HALFLEN] = -val_scale * sin_x;

		sqr_lut[i] = val_scale;

		const float srs_x = sqrtf(sin_x);
		srs_lut[i] = val_scale * srs_x;
		hsi_lut[i] = val_scale * (sin_x*2 - 1.f);
		mto_lut[i] = val_scale * (srs_x*2 - 1.f);

		const float spa_x = sin(SAU_PI * 0.5f * (1 + x));
		spa_lut[i + QUARTERLEN] = val_scale * (spa_x*2 - 1.f);

		siw_lut[i] = sau_sintilt_r1(-1.f + x);
		siw_lut[i + HALFLEN] = sau_sintilt_r1(x);

		shs_lut[i] = sin_x*sin_x*2 - 1.f;
		shs_lut[i + HALFLEN] = -sauWave_MAXVAL;

		ssr_lut[i] = sin_x*sin_x;
		ssr_lut[i + HALFLEN] = -srs_x;
	}
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * (1.f/(HALFLEN-1));
		const double x_rev = (HALFLEN-i) * (1.f/HALFLEN);

		par_lut[i + QUARTERLEN] =
			val_scale * ((x_rev * x_rev) * 2.f - 1.f);
		saw_lut[i] = val_scale * (1.f - x);
	}
	par_lut[HALFLEN+QUARTERLEN] = -val_scale;
	spa_lut[HALFLEN+QUARTERLEN] = -val_scale;
	for (i = 0; i < QUARTERLEN; ++i) {
		const double x = i * (1.f/QUARTERLEN);
		const double x_rev = (QUARTERLEN-i) * (1.f/QUARTERLEN);

		pitri_lut[i] = val_scale * ((x * x) - 1.f);
		pitri_lut[i + QUARTERLEN] = val_scale * (1.f - (x_rev * x_rev));

		tri_lut[i] = val_scale * x;
		tri_lut[i + QUARTERLEN] = val_scale * x_rev;

		par_lut[i] = par_lut[HALFLEN - i];
		par_lut[i + HALFLEN+QUARTERLEN] =
			par_lut[HALFLEN+QUARTERLEN - i];
		spa_lut[i] = spa_lut[HALFLEN - i];
		spa_lut[i + HALFLEN+QUARTERLEN] =
			spa_lut[HALFLEN+QUARTERLEN - i];
	}
	/* Second half:
	 *  - tri, It pitri
	 *  - srs, It pisrs
	 *  - sqr, It -cotri
	 *  - mto, It pimto
	 *  - saw, -It copar
	 *  - hsi, It pihsi
	 */
	for (i = HALFLEN; i < sauWave_LEN; ++i) {
		pitri_lut[i] = -pitri_lut[i - HALFLEN];
		tri_lut[i] = -tri_lut[i - HALFLEN];
		sqr_lut[i] = -val_scale;

		saw_lut[i] = -saw_lut[(sauWave_LEN-1) - i];

		hsi_lut[i] = -val_scale;
		mto_lut[i] = -val_scale;
		srs_lut[i] = -srs_lut[i - HALFLEN];
	}
	/* Full cycle:
	 *  - ean, It piean
	 *  - cat, It picat
	 *  - eto, -It coean
	 */
	const float ean_dc_adj = (1.14603185654 - 1.f) / 2.f;
	const float ean_scale_adj = val_scale / 1.07301592827;
	const float eto_scale_adj = val_scale / 1.21094322205;
	for (i = 0; i < sauWave_LEN; ++i) {
		int j = (i*2) < sauWave_LEN ? (i*2) : (i*2) - sauWave_LEN;
		ean_lut[i] =
			(sin_lut[i] + par_lut[i] - tri_lut[i] + ean_dc_adj) *
			ean_scale_adj;
		cat_lut[i] = sin_lut[i] + mto_lut[i] - srs_lut[i];
		eto_lut[i] = (sin_lut[i] + saw_lut[j]) * eto_scale_adj;
	}
	/*fill_It(ean_lut, sauWave_LEN, -val_scale, eto_lut);*/
	fill_It(piean_lut, sauWave_LEN, val_scale, ean_lut);
	fill_It(picat_lut, sauWave_LEN, val_scale, cat_lut);
	fill_It(pipar_lut, sauWave_LEN, val_scale, par_lut);
	fill_It(pisrs_lut, sauWave_LEN, val_scale, srs_lut);
	fill_It(pimto_lut, sauWave_LEN, val_scale, mto_lut);
	fill_It(pihsi_lut, sauWave_LEN, val_scale, hsi_lut);
	fill_It(pispa_lut, sauWave_LEN, val_scale, spa_lut);
	fill_It(pisiw_lut, sauWave_LEN, val_scale, siw_lut);
	fill_It(pishs_lut, sauWave_LEN, val_scale, shs_lut);
	fill_It(pissr_lut, sauWave_LEN, val_scale, ssr_lut);

#if 1
	for (int i = 0; i < SAU_WAVE_NAMED; ++i)
		sauWave_print(i, false);
//	sauWave_print(SAU_WAVE_N_spa, true);
#endif
}

/* Write data meant for conversion to image? */
#define PLOT_DATA 0
#define PLOT_TWICE 1

/**
 * Print an index-value table for a LUT.
 */
void sauWave_print(uint8_t id, bool verbose) {
	if (id >= SAU_WAVE_NAMED)
		return;
	const float *lut = sauWave_luts[id];
	const float *pilut = sauWave_piluts[id];
#if !PLOT_DATA
	const char *lut_name = sauWave_names[id];
	sau_printf("LUT: %s\n", lut_name);
#endif
	double sum = 0.f, sum2 = 0.f, mag_sum = 0.f, mag_sum2 = 0.f;
	float prev_s = lut[sauWave_LEN - 1], prev_s2 = pilut[sauWave_LEN - 1];
	float peak_max = 0.f, peak_max2 = 0.f;
	float slope_min = 0.f, slope_min2 = 0.f;
	float slope_max = 0.f, slope_max2 = 0.f;
	for (int i = 0; i < sauWave_LEN; ++i) {
		float s = lut[i], s2 = pilut[i];
		float abs_s = fabsf(s), abs_s2 = fabsf(s2);
		double slope_s = (s - prev_s), slope_s2 = (s2 - prev_s2);
		sum += s; sum2 += s2;
		mag_sum += abs_s; mag_sum2 += abs_s2;
		if (peak_max < abs_s) peak_max = abs_s;
		if (peak_max2 < abs_s2) peak_max2 = abs_s2;
		if (slope_max < slope_s) slope_max = slope_s;
		if (slope_max2 < slope_s2) slope_max2 = slope_s2;
		if (slope_min > slope_s) slope_min = slope_s;
		if (slope_min2 > slope_s2) slope_min2 = slope_s2;
		prev_s = s; prev_s2 = s2;
#if PLOT_DATA
		sau_printf("%.11f\t%.11f\n", i/(float)sauWave_LEN, s);
#else
		if (verbose)
			sau_printf("[\t%d]: \t%.11f\tIv %.11f\n", i, s, s2);
#endif
	}
#if PLOT_DATA
# if PLOT_TWICE
	for (int i = 0; i < sauWave_LEN; ++i) {
		float s = lut[i];
		sau_printf("%.11f\t%.11f\n",
				(i+sauWave_LEN)/(float)sauWave_LEN, s);
	}
# endif
#else
	double len_scale = sauWave_LEN;
	double diff_scale = sauWave_picoeffs[id].amp_scale;
	double diff_offset = sauWave_picoeffs[id].amp_dc;
	double diff_min = slope_min2 * DVSCALE;
	double diff_min_adj = diff_min * diff_scale + diff_offset;
	double diff_max = slope_max2 * DVSCALE;
	double diff_max_adj = diff_max * diff_scale + diff_offset;
	double tweak_dc = -(diff_min + diff_max) / 2.f;
	double tweak_scale = 2.f / (diff_max - diff_min);
	sau_printf("\tp.m.avg %.11f\tIt %.11f\n"
			"\tp.m.max %.11f\tIt %.11f\n"
			"\tdc.offs %.11f\tIt %.11f\n"
			"\t+slope  %.11f\tIt %.11f\n"
			"\t-slope  %.11f\tIt %.11f\n"
			"It\tdiff.min %.11f\t(adj. to %.11f)\n"
			"It\tdiff.max %.11f\t(adj. to %.11f)\n"
			"tweak\tdc.offs %.11f\n"
			"tweak\tscale %.11f\n",
			mag_sum / len_scale, mag_sum2 / len_scale,
			peak_max, peak_max2,
			sum / len_scale, sum2 / len_scale,
			slope_max, slope_max2,
			slope_min, slope_min2,
			diff_min, (float) diff_min_adj,
			diff_max, (float) diff_max_adj,
			tweak_dc * tweak_scale,
			tweak_scale);
#endif
}
