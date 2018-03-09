/* sgensys: Wave LUT module.
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

#define SGS_WaveLut_LENBITS 12
#define SGS_WaveLut_LEN     (1<<SGS_WaveLut_LENBITS) /* 4096 */
#define SGS_WaveLut_LENMASK (SGS_WaveLut_LEN - 1)

#define SGS_WaveLut_MAXVAL ((1<<15) - 1)
#define SGS_WaveLut_MINVAL (-SGS_WaveLut_MAXVAL)

#define SGS_WaveLut_SCALEBITS (32-SGS_WaveLut_LENBITS)
#define SGS_WaveLut_SCALE     (1<<SGS_WaveLut_SCALEBITS)
#define SGS_WaveLut_SCALEMASK (SGS_WaveLut_SCALE - 1)

/**
 * Wave types.
 */
enum {
  SGS_WAVE_SIN = 0,
  SGS_WAVE_SRS,
  SGS_WAVE_TRI,
  SGS_WAVE_SQR,
  SGS_WAVE_SAW,
  SGS_WAVE_TYPES
};

/** Wave LUT type. */
typedef int16_t SGS_WaveLut_t[SGS_WaveLut_LEN];

/** Wave LUT pointer type. */
typedef const int16_t *SGS_WaveLut_p;

/** Wave LUTs, indexed by wave type value. */
extern SGS_WaveLut_t SGS_waveluts[SGS_WAVE_TYPES];

/**
 * Turn 32-bit unsigned phase value into LUT index.
 */
#define SGS_WaveLut_INDEX(phase) \
	(((uint32_t)(phase)) >> SGS_WaveLut_SCALEBITS)

extern void SGS_global_init_WaveLut(void);
