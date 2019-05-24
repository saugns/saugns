/* ssndgen: Audio program builder module.
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

#include "ssndgen.h"
#include "script.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SSG_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SSG_Script *sd = SSG_load_Script(script_arg, is_path);
	if (!sd)
		return NULL;
	SSG_Program *o = SSG_build_Program(sd);
	SSG_discard_Script(sd);
	return o;
}

/**
 * Build the listed scripts, adding each result (even if NULL)
 * to the program list.
 *
 * \return number of programs successfully built
 */
size_t SSG_build(const SSG_PtrList *restrict script_args, bool are_paths,
		SSG_PtrList *restrict prg_objs) {
	size_t built = 0;
	const char **args = (const char**) SSG_PtrList_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SSG_Program *prg = build_program(args[i], are_paths);
		if (prg != NULL) ++built;
		SSG_PtrList_add(prg_objs, prg);
	}
	return built;
}
