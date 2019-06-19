/* saugns: Math definitions.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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
#include "common.h"
#include <math.h>

#define SAU_PI       3.14159265358979323846
#define SAU_ASIN_1_2 0.52359877559829887308 // asin(0.5)
#define SAU_SQRT_1_2 0.70710678118654752440 // sqrt(0.5), 1/sqrt(2)

/**
 * Convert time in ms to time in samples for a sample rate.
 */
#define SAU_MS_IN_SAMPLES(ms, srate) \
	lrintf(((ms) * .001f) * (srate))
