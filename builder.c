/* sgensys: Audio program builder module.
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

#include "sgensys.h"
#include "script.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SGS_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SGS_Script *sd = SGS_load_Script(script_arg, is_path);
	if (!sd)
		return NULL;
	SGS_Program *o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
	return o;
}

/**
 * Build the listed scripts, adding each result (even if NULL)
 * to the program list.
 *
 * \return number of programs successfully built
 */
size_t SGS_build(const SGS_PtrList *restrict script_args, bool are_paths,
		SGS_PtrList *restrict prg_objs) {
	size_t built = 0;
	const char **args = (const char**) SGS_PtrList_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SGS_Program *prg = build_program(args[i], are_paths);
		if (prg != NULL) ++built;
		SGS_PtrList_add(prg_objs, prg);
	}
	return built;
}
