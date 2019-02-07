/* saugns: Audio program builder module.
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

#include "saugns.h"
#include "builder/script.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SAU_Program *build_program(const char *restrict path) {
	SAU_Script *sd = SAU_load_Script(path);
	if (!sd) return NULL;

	SAU_Program *o = SAU_build_Program(sd);
	SAU_discard_Script(sd);
	if (!o) return NULL;
	return o;
}

/**
 * Build the listed script files, adding the result to
 * the program list for each script (even when the result is NULL).
 *
 * \return number of programs successfully built
 */
size_t SAU_build(const SAU_PtrList *restrict path_list,
		SAU_PtrList *restrict prg_list) {
	size_t built = 0;
	const char **paths = (const char**) SAU_PtrList_ITEMS(path_list);
	for (size_t i = 0; i < path_list->count; ++i) {
		SAU_Program *prg = build_program(paths[i]);
		if (prg != NULL) ++built;
		SAU_PtrList_add(prg_list, prg);
	}
	return built;
}
