/* sgensys: Script parameter module.
 * Copyright (c) 2018 Joel K. Pettersson
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
#include "../common.h"

/**
 * Timed parameter flags.
 */
enum {
	SGS_TPAR_SLOPE = 1<<0,
};

/**
 * Timed parameter type.
 */
typedef struct SGS_TimedParam {
	float v0, vt;
	uint32_t time_ms;
	uint8_t slope;
	uint8_t flags;
} SGS_TimedParam;
