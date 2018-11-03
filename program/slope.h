/* sgensys: Value slope module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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

#include "../common.h"

/**
 * Slope types.
 */
enum {
	SGS_SLOPE_HOLD = 0,
	SGS_SLOPE_LIN,
	SGS_SLOPE_EXP,
	SGS_SLOPE_LOG,
	SGS_SLOPE_TYPES
};

/** Names of slope types, with an extra NULL pointer at the end. */
extern const char *const SGS_Slope_names[SGS_SLOPE_TYPES + 1];

/**
 * Slope, used for gradual value change.
 *
 * The \a pos field keeps track of position in samples;
 * reset to 0 when running for a new duration.
 */
typedef struct SGS_Slope {
	uint32_t time_ms;
	uint32_t pos;
	float goal;
	uint8_t type;
} SGS_Slope;

bool SGS_Slope_run(SGS_Slope *restrict o, uint32_t srate,
		float *restrict buf, uint32_t buf_len, float s0);
