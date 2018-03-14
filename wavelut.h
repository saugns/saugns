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

#define SGS_WAVELUT_LENBITS 12
#define SGS_WAVELUT_LEN     (1<<SGS_WAVELUT_LENBITS) /* 4096 */
#define SGS_WAVELUT_LENMASK (SGS_WAVELUT_LEN - 1)

#define SGS_WAVELUT_MAXVAL ((1<<15) - 1)
#define SGS_WAVELUT_MINVAL (-SGS_WAVELUT_MAXVAL)

#define SGS_WAVELUT_SCALEBITS (32-SGS_WAVELUT_LENBITS)
#define SGS_WAVELUT_SCALE     (1<<SGS_WAVELUT_SCALEBITS)
#define SGS_WAVELUT_SCALEMASK (SGS_WAVELUT_SCALE - 1)

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
typedef int16_t SGS_WaveLUT_t[SGS_WAVELUT_LEN];

/** Wave LUT pointer type. */
typedef const int16_t *SGS_WaveLUTPtr_t;

/** Wave LUTs, indexed by wave type value. */
extern SGS_WaveLUT_t SGS_waveluts[SGS_WAVE_TYPES];

/**
 * Turn 32-bit unsigned phase value into LUT value index.
 */
#define SGS_WAVELUT_INDEX(phase) \
	(((uint32_t)(phase)) >> SGS_WAVELUT_SCALEBITS)

extern void SGS_wavelut_global_init(void);
