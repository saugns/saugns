/* SAU library: Envelope generator module.
 * Copyright (c) 2025-2026 Joel K. Pettersson
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

/** Envelope modes a.k.a. functions. */
enum {
	SAU_ENV_FN_OFF = 0,
	SAU_ENV_FN_TRUNC,
	SAU_ENV_FN_DECLICK,
	SAU_ENV_FN_CLONED,
	SAU_ENV_FN_LOOP,
	SAU_ENV_FN_SHRINK,
	SAU_ENV_FUNCTIONS,
	SAU_ENV_FN_DEFAULT = SAU_ENV_FN_DECLICK
};

/** Envelope time parameters. Used as indices for time arrays. */
enum {
	SAU_ENV_TIME_A = 0,
	SAU_ENV_TIME_D,
	SAU_ENV_TIME_S,
	SAU_ENV_TIME_R,
	SAU_ENV_TIMES
};

/** Envelope line parameters. Used as indices for line arrays. */
enum {
	SAU_ENV_LINE_A = 0,
	SAU_ENV_LINE_D,
	SAU_ENV_LINE_R,
	SAU_ENV_LINES
};

/** Envelope time parameter flag; see envelope time enums for \p i range. */
#define SAU_ENVP_TIME(i) (1U<<(i))

/** Envelope other parameter flags. */
enum {
	SAU_ENVP_S         = 1U<<0,
	SAU_ENVP_MODE      = 1U<<1,
	SAU_ENVP_R_STRETCH = 1U<<2,
};

/**
 * Envelope parameter type.
 */
typedef struct sauEnvPar {
	uint32_t time_ms[SAU_ENV_TIMES];
	uint8_t line_p1[SAU_ENV_LINES], line_all_p1; // set +1 the value
	uint8_t flags, time_flags;
	uint8_t mode;
	float s_val;
} sauEnvPar;
