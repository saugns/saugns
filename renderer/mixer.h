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

#pragma once
#include "../ramp.h"

#define SGS_MIX_BUFLEN 1024

typedef struct SGS_Mixer {
	float *mix_l, *mix_r;
	float *pan_buf;
	uint32_t srate;
	float scale;
} SGS_Mixer;

SGS_Mixer *SGS_create_Mixer(void);
void SGS_destroy_Mixer(SGS_Mixer *restrict o);

/**
 * Set sample rate used for panning.
 */
static inline void SGS_Mixer_set_srate(SGS_Mixer *restrict o, uint32_t srate) {
	o->srate = srate;
}

/**
 * Set amplitude scaling.
 */
static inline void SGS_Mixer_set_scale(SGS_Mixer *restrict o, float scale) {
	o->scale = scale * 0.5f; // half for panning sum
}

void SGS_Mixer_clear(SGS_Mixer *restrict o);
void SGS_Mixer_add(SGS_Mixer *restrict o,
		float *restrict buf, size_t len,
		SGS_Ramp *restrict pan, uint32_t *restrict pan_pos);
void SGS_Mixer_write(SGS_Mixer *restrict o,
		int16_t **restrict spp, size_t len);
