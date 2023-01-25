/* SAU library: Wave module.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

static float saw_lut[sauWave_LEN];
static float par_lut[sauWave_LEN];

static float ahs_lut[sauWave_LEN];
static float piahs_lut[sauWave_LEN];

static float hrs_lut[sauWave_LEN];
static float pihrs_lut[sauWave_LEN];

static float srs_lut[sauWave_LEN];
static float pisrs_lut[sauWave_LEN];
static float ssr_lut[sauWave_LEN];
static float pissr_lut[sauWave_LEN];

#define SAU_WAVE__X_LUT_NAME(NAME) NAME##_lut,

float *const sauWave_luts[SAU_WAVE_NAMED] = {
	SAU_WAVE__ITEMS(SAU_WAVE__X_LUT_NAME)
};

float *const sauWave_piluts[SAU_WAVE_NAMED] = {
	sin_lut,
	tri_lut,
	pitri_lut,
	par_lut,
	piahs_lut,
	pihrs_lut,
	pisrs_lut,
	pissr_lut,
};

const struct sauWaveCoeffs sauWave_picoeffs[SAU_WAVE_NAMED] = {
	{
		.amp_scale = 1.f / 0.78539693356f,
		.amp_dc    = 0.f,
		.phase_adj = INT32_MIN/2,
	},{
		.amp_scale = 1.f / 0.5f,
		.amp_dc    = 0.f,
		.phase_adj = INT32_MIN/2,
	},{
		.amp_scale = 1.f / 0.99902343750f,
		.amp_dc    = 0.f,
		.phase_adj = 0,
	},{
		.amp_scale = 1.f / 1.00048828125f,
		.amp_dc    = 0.f,
		.phase_adj = 0,
	},{
		.amp_scale = 1.f / 0.93224668503f,
		.amp_dc    = 0.27323962859f - (1.00038196601f - 1.f),
		.phase_adj = 0,
	},{
		.amp_scale = 1.f / 0.71259796619f,
		.amp_dc    = -0.36338006155f - (-1.00002840285f + 1.f),
		.phase_adj = 0,
	},{
		.amp_scale = 1.f / 0.65553373098f,
		.amp_dc    = 0.f,
		.phase_adj = 0,
	},{
		.amp_scale = 1.f / 0.79131034491f,
		.amp_dc    = -0.13136863776f - (-1.00000757464 + 1.f),
		.phase_adj = 0,
	},
};

const char *const sauWave_names[SAU_WAVE_NAMED + 1] = {
	SAU_WAVE__ITEMS(SAU_WAVE__X_NAME)
	NULL
};

/*
 * Fill \p lut with integrated version of \p in_lut,
 * adjusted to have a peak amplitude of +/- \p scale.
 * The \in_dc DC offset for \p in_lut must be provided.
 */
static void fill_It(float *restrict lut, size_t len, const float scale,
		const float *restrict in_lut, double in_dc) {
	double in_sum = 0.f;
	float lb = 0.f, ub = 0.f;
	for (size_t i = 0; i < len; ++i) {
		in_sum += in_lut[i] - in_dc;
		float x = in_sum * IVSCALE;
		if (x < lb) lb = x;
		if (x > ub) ub = x;
		lut[i] = x;
	}
	float out_scale = scale * 1.f/((ub - lb) * 0.5f);
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
	 *
	 * First half:
	 *  - sqr, It -cotri
	 *  - tri, It pitri
	 *  - saw, It par
	 *  - srs, It pisrs
	 *  - ssr, It pissr
	 */
	double srs_half_dc = 0.f, ssr_half_dc = 0.f, ssr_dc;
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * (1.f/HALFLEN);
		//const double x_rev = (HALFLEN-i) * (1.f/HALFLEN);

		const float sin_x = sin(SAU_PI * x);
		sin_lut[i] = val_scale * sin_x;
		sin_lut[i + HALFLEN] = -val_scale * sin_x;

		sqr_lut[i] = val_scale;

		srs_half_dc += srs_lut[i] = val_scale * sqrtf(sin_x);

		ssr_half_dc += ssr_lut[i] = val_scale * (sin_x * sin_x);
	}
	srs_half_dc *= (1.f/sauWave_LEN);
	ssr_half_dc *= (1.f/sauWave_LEN);
	ssr_dc = ssr_half_dc - srs_half_dc;
	for (i = 0; i < HALFLEN; ++i) {
		const double x = i * (1.f/(HALFLEN-1));
		const double x_rev = ((HALFLEN-1)-i) * (1.f/(HALFLEN-1));

		saw_lut[i] = val_scale * (x - 1.f);
		par_lut[i] = val_scale * ((x_rev * x_rev) * 2.f - 1.f);
	}
	for (i = 0; i < QUARTERLEN; ++i) {
		const double x = i * (1.f/QUARTERLEN);
		const double x_rev = (QUARTERLEN-i) * (1.f/QUARTERLEN);

		tri_lut[i] = val_scale * x;
		tri_lut[i + QUARTERLEN] = val_scale * x_rev;

		/* pre-integrated triangle shape */
		pitri_lut[i] = val_scale * ((x * x) - 1.f);
		pitri_lut[i + QUARTERLEN] = val_scale * (1.f - (x_rev * x_rev));
	}
	/* Second half:
	 *  - sqr, It -cotri
	 *  - tri, It pitri
	 *  - saw, It par
	 *  - srs, It pisrs
	 *  - ssr, It pissr
	 */
	for (i = HALFLEN; i < sauWave_LEN; ++i) {
		sqr_lut[i] = -sqr_lut[i - HALFLEN];
		tri_lut[i] = -tri_lut[i - HALFLEN];
		pitri_lut[i] = -pitri_lut[i - HALFLEN];

		saw_lut[i] = -saw_lut[(sauWave_LEN-1) - i];
		par_lut[i] = par_lut[(sauWave_LEN-1) - i];

		ssr_lut[i] = srs_lut[i] = -srs_lut[i - HALFLEN];
	}
	/* Full cycle:
	 *  - ahs, It piahs
	 *  - hrs, It pihrs
	 */
	double ahs_dc = 0.f;
	double hrs_dc = 0.f;
	for (i = 0; i < sauWave_LEN; ++i) {
		const double x = i * (1.f/HALFLEN);

		float ahs_x = sin((SAU_PI * x) * 0.5f + SAU_ASIN_1_2);
		ahs_x = fabs(ahs_x) - 0.5f;
		ahs_x += ahs_x;
		ahs_x *= val_scale;
		ahs_lut[i] = ahs_x;
		ahs_dc += ahs_x;

		float hrs_x = sin((SAU_PI * x) + SAU_ASIN_1_2);
		if (hrs_x > 0.f) {
			hrs_x -= 0.5f;
			hrs_x += hrs_x;
			hrs_x *= val_scale;
		} else {
			hrs_x = -val_scale;
		}
		hrs_lut[i] = hrs_x;
		hrs_dc += hrs_x;
	}
	ahs_dc *= (1.f/sauWave_LEN);
	hrs_dc *= (1.f/sauWave_LEN);
	fill_It(piahs_lut, sauWave_LEN, val_scale, ahs_lut, ahs_dc);
	fill_It(pihrs_lut, sauWave_LEN, val_scale, hrs_lut, hrs_dc);
	fill_It(pisrs_lut, sauWave_LEN, val_scale, srs_lut, 0.f);
	fill_It(pissr_lut, sauWave_LEN, val_scale, ssr_lut, ssr_dc);
}

/**
 * Print an index-value table for a LUT.
 */
void sauWave_print(uint8_t id) {
	if (id >= SAU_WAVE_NAMED)
		return;
	const float *lut = sauWave_luts[id];
	const float *pilut = sauWave_piluts[id];
	const char *lut_name = sauWave_names[id];
	sau_printf("LUT: %s\n", lut_name);
	double sum = 0.f, sum2 = 0.f, mag_sum = 0.f, mag_sum2 = 0.f;
	float prev_s = lut[sauWave_LEN - 1], prev_s2 = pilut[sauWave_LEN - 1];
	float peak_max = 0.f, peak_max2 = 0.f;
	float slope_min = 0.f, slope_min2 = 0.f;
	float slope_max = 0.f, slope_max2 = 0.f;
	for (int i = 0; i < sauWave_LEN; ++i) {
		float s = lut[i], s2 = pilut[i];
		float abs_s = fabsf(s), abs_s2 = fabsf(s2);
		float slope_s = (s - prev_s), slope_s2 = (s2 - prev_s2);
		sum += s; sum2 += s2;
		mag_sum += abs_s; mag_sum2 += abs_s2;
		if (peak_max < abs_s) peak_max = abs_s;
		if (peak_max2 < abs_s2) peak_max2 = abs_s2;
		if (slope_max < slope_s) slope_max = slope_s;
		if (slope_max2 < slope_s2) slope_max2 = slope_s2;
		if (slope_min > slope_s) slope_min = slope_s;
		if (slope_min2 > slope_s2) slope_min2 = slope_s2;
		prev_s = s; prev_s2 = s2;
		sau_printf("[\t%d]: \t%.11f\tIv %.11f\n", i, s, s2);
	}
	float len_scale = (float) sauWave_LEN;
	float diff_min = slope_min2 * DVSCALE;
	float diff_max = slope_max2 * DVSCALE;
	float diff_scale = sauWave_picoeffs[id].amp_scale;
	float diff_offset = sauWave_picoeffs[id].amp_dc;
	sau_printf("\tp.m.avg %.11f\tIt %.11f\n"
			"\tp.m.max %.11f\tIt %.11f\n"
			"\tdc.offs %.11f\tIt %.11f\n"
			"\t+slope  %.11f\tIt %.11f\n"
			"\t-slope  %.11f\tIt %.11f\n"
			"It\tdiff.min %.11f\t(adj. to %.11f)\n"
			"It\tdiff.max %.11f\t(adj. to %.11f)\n",
			mag_sum / len_scale,
			mag_sum2 / len_scale,
			peak_max,
			peak_max2,
			sum / len_scale,
			sum2 / len_scale,
			slope_max,
			slope_max2,
			slope_min,
			slope_min2,
			diff_min, diff_min * diff_scale + diff_offset,
			diff_max, diff_max * diff_scale + diff_offset);
}
