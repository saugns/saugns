/* sgensys: Common definitions.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
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

/*
 * Common types.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned int uint;

/*
 * Debugging options.
 */

/** Disable old sound generator, use interpreter & renderer instead. */
#define TEST_INTERPRETER 0
