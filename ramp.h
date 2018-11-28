/* sgensys: Value ramp module.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
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

/**
 * Ramp types.
 */
enum {
	SGS_RAMP_STATE = 0,
	SGS_RAMP_LIN,
	SGS_RAMP_EXP,
	SGS_RAMP_LOG,
	SGS_RAMP_TYPES
};

/** Names of ramp types, with an extra NULL pointer at the end. */
extern const char *const SGS_Ramp_names[SGS_RAMP_TYPES + 1];

/**
 * Ramp, used for gradual value change.
 *
 * The \a pos field keeps track of position in samples;
 * reset to 0 when running for a new duration.
 */
typedef struct SGS_Ramp {
	uint32_t time_ms;
	uint32_t pos;
	float goal;
	uint8_t type;
} SGS_Ramp;

bool SGS_Ramp_run(SGS_Ramp *restrict o, uint32_t srate,
		float *restrict buf, uint32_t buf_len, float s0);
