/* ssndgen: Value slope module.
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
	SSG_SLOPE_STATE = 0,
	SSG_SLOPE_LIN,
	SSG_SLOPE_EXP,
	SSG_SLOPE_LOG,
	SSG_SLOPE_TYPES
};

/** Names of slope types, with an extra NULL pointer at the end. */
extern const char *const SSG_Slope_names[SSG_SLOPE_TYPES + 1];

/**
 * Slope, used for gradual value change.
 *
 * The \a pos field keeps track of position in samples;
 * reset to 0 when running for a new duration.
 */
typedef struct SSG_Slope {
	uint32_t time_ms;
	uint32_t pos;
	float goal;
	uint8_t type;
} SSG_Slope;

bool SSG_Slope_run(SSG_Slope *o, uint32_t srate,
		float *buf, uint32_t buf_len, float s0);
