/* SAU library: Help data and printout code.
 * Copyright (c) 2020-2024 Joel K. Pettersson
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
#include "common.h"
#include <stdio.h>

/* Macro used to declare and define help sets of items. */
#define SAU_HELP__ITEMS(X) \
	X(help, Help) \
	X(math, Math) \
	X(line, Line) \
	X(wave, Wave) \
	X(noise, Noise) \
	//
#define SAU_HELP__X_ID(NAME, ARRAY) SAU_HELP_N_##NAME,
#define SAU_HELP__X_NAME(NAME, ARRAY) #NAME,
#define SAU_HELP__X_ARRAY(NAME, ARRAY) sau##ARRAY##_names

/**
 * Named help types.
 */
enum {
	SAU_HELP__ITEMS(SAU_HELP__X_ID)
	SAU_HELP_NAMED
};

/** Names of help types, with an extra NULL pointer at the end. */
extern const char *const sauHelp_names[SAU_HELP_NAMED + 1];

const char *const *sau_find_help(const char *restrict str);

/*
 * Name array functions of more general use.
 */

bool sau_find_name(const char *const *restrict namearr,
		const char *restrict str, size_t *restrict id);
bool sau_print_names(const char *const *restrict namearr,
		const char *restrict headstr,
		FILE *restrict out);
