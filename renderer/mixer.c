/* sgensys: Audio mixer module.
 * Copyright (c) 2019-2020 Joel K. Pettersson
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

#include "mixer.h"
#include "../math.h"
#include <stdlib.h>
#include <string.h>

static float max, min;

/**
 * Create instance.
 */
SGS_Mixer *SGS_create_Mixer(void) {
	SGS_Mixer *o = calloc(1, sizeof(SGS_Mixer));
	if (!o)
		return NULL;
	o->mix_l = calloc(SGS_MIX_BUFLEN, sizeof(float));
	if (!o->mix_l) goto ERROR;
	o->mix_r = calloc(SGS_MIX_BUFLEN, sizeof(float));
	if (!o->mix_r) goto ERROR;
	o->pan_buf = calloc(SGS_MIX_BUFLEN, sizeof(float));
	if (!o->pan_buf) goto ERROR;
	o->scale = 1.f;
max = 0.f;
min = 0.f;
	return o;

ERROR:
	SGS_destroy_Mixer(o);
	return NULL;
}

/**
 * Destroy instance.
 */
void SGS_destroy_Mixer(SGS_Mixer *restrict o) {
	if (!o)
		return;
printf("mixer: max==%.11f, min==%.11f\n", max, min);
	free(o->mix_l);
	free(o->mix_r);
	free(o->pan_buf);
	free(o);
}

/**
 * Clear the mix buffers.
 */
void SGS_Mixer_clear(SGS_Mixer *restrict o) {
	memset(o->mix_l, 0, sizeof(float) * SGS_MIX_BUFLEN);
	memset(o->mix_r, 0, sizeof(float) * SGS_MIX_BUFLEN);
}

/**
 * Add \p len samples from \p buf into the mix buffers,
 * using \p pan for panning and scaling each sample.
 *
 * Sample rate needs to be set if \p pan has curve enabled.
 */
void SGS_Mixer_add(SGS_Mixer *restrict o,
		float *restrict buf, size_t len,
		SGS_Ramp *restrict pan, uint32_t *restrict pan_pos) {
	if (pan->flags & SGS_RAMPP_GOAL) {
		SGS_Ramp_run(pan, o->pan_buf, len, o->srate, pan_pos, NULL);
		for (size_t i = 0; i < len; ++i) {
			float s = buf[i] * o->scale;
			float s_r = s * o->pan_buf[i];
			float s_l = s - s_r;
			o->mix_l[i] += s_l;
			o->mix_r[i] += s_r;
		}
	} else {
		for (size_t i = 0; i < len; ++i) {
			float s = buf[i] * o->scale;
			float s_r = s * pan->v0;
			float s_l = s - s_r;
			o->mix_l[i] += s_l;
			o->mix_r[i] += s_r;
		}
	}
}

/**
 * Write \p len samples from the mix buffers
 * into a 16-bit stereo (interleaved) buffer
 * pointed to by \p spp. Advances \p spp.
 */
void SGS_Mixer_write(SGS_Mixer *restrict o,
		int16_t **restrict spp, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		float s_l = o->mix_l[i];
		float s_r = o->mix_r[i];
if (max < s_l) max = s_l;
if (max < s_r) max = s_r;
if (min > s_l) min = s_l;
if (min > s_r) min = s_r;
		if (s_l > 1.f) s_l = 1.f;
		else if (s_l < -1.f) s_l = -1.f;
		if (s_r > 1.f) s_r = 1.f;
		else if (s_r < -1.f) s_r = -1.f;
		*(*spp)++ += lrintf(s_l * (float) INT16_MAX);
		*(*spp)++ += lrintf(s_r * (float) INT16_MAX);
	}
}
