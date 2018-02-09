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

#pragma once
#include "sgensys.h"

#define SGSWave_LENBITS 11
#define SGSWave_LEN     (1<<SGSWave_LENBITS) /* 2048 */
#define SGSWave_LENMASK (SGSWave_LEN - 1)

#define SGSWave_MAXVAL ((1<<15) - 1)
#define SGSWave_MINVAL (-SGSWave_MAXVAL)

#define SGSWave_SCALEBITS (32-SGSWave_LENBITS)
#define SGSWave_SCALE     (1<<SGSWave_SCALEBITS)
#define SGSWave_SCALEMASK (SGSWave_SCALE - 1)

/**
 * Wave types.
 */
enum {
	SGS_WAVE_SIN = 0,
	SGS_WAVE_SQR,
	SGS_WAVE_TRI,
	SGS_WAVE_SAW,
	SGS_WAVE_SHA,
	SGS_WAVE_SZH,
	SGS_WAVE_SHH,
	SGS_WAVE_SSR,
//	SGS_WAVE_SZHHR,
	SGS_WAVE_TYPES
};

/** LUTs for wave types. */
extern int16_t SGSWave_luts[SGS_WAVE_TYPES][SGSWave_LEN];

/** Names of wave types, with an extra NULL pointer at the end. */
extern const char *const SGSWave_names[SGS_WAVE_TYPES + 1];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define SGSWave_INDEX(phase) \
	(((uint32_t)(phase)) >> SGSWave_SCALEBITS)

extern void SGS_global_init_Wave(void);

extern void SGSWave_print(uint8_t id);
