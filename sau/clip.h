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

#pragma once
#include "math.h"

/* Macro used to declare and define clip type sets of items. */
#define SAU_CLIP__ITEMS(X) \
	X(off) \
	X(hard) \
	X(sa3) \
	X(sa4) \
	X(sa4_2) \
	X(sa4b) \
	X(sa5) \
	X(ds2) \
	X(ds2b) \
	X(dm3) \
	X(dm4) \
	X(dm4_2) \
	//
#define SAU_CLIP__X_ID(NAME) SAU_CLIP_N_##NAME,
#define SAU_CLIP__X_NAME(NAME) #NAME,
#define SAU_CLIP__X_PROTOTYPES(NAME) \
void sauClip_apply_##NAME(float *restrict buf, size_t len, float gain); \
/**/
#define SAU_CLIP__X_APPLY_ADDR(NAME) sauClip_apply_##NAME,

/**
 * Clip function types.
 */
enum {
	SAU_CLIP__ITEMS(SAU_CLIP__X_ID)
	SAU_CLIP_NAMED
};

SAU_CLIP__ITEMS(SAU_CLIP__X_PROTOTYPES)

/** Names of clip function types, with an extra NULL pointer at the end. */
extern const char *const sauClip_names[SAU_CLIP_NAMED + 1];

typedef void (*sauClip_apply_f)(float *restrict buf, size_t len,
		float gain);

/** In-place clip functions for types. */
extern const sauClip_apply_f sauClip_apply_funcs[SAU_CLIP_NAMED];

/**
 * Clip parameter type.
 */
struct sauClipParam {
	float level;
	uint8_t type;
	bool set_type : 1;
	bool set_level : 1;
};
