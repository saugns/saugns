/* sgensys: Math definitions.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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
#include "sgensys.h"
#include <math.h>

#define PI 3.141592653589

#define DC_OFFSET 1.0E-25

#define RC_TIME(sXsr) \
  exp(-1.0 / (sXsr))
#define RC_CALC(coeff, in, state) \
  ((in) + ((state) - (in)) * (coeff))
#define RC_OFFSET 0.632121f
