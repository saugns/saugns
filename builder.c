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
#include "builder/file.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Open file for script arg.
 *
 * \return instance or NULL on error
 */
static SGS_File *open_file(const char *restrict script_arg, bool is_path) {
	SGS_File *f = SGS_create_File();
	if (!f) return NULL;
	if (!is_path) {
		SGS_File_stropenrb(f, "<string>", script_arg);
		return f;
	}
	if (!SGS_File_fopenrb(f, script_arg)) {
		SGS_error(NULL,
"couldn't open script file \"%s\" for reading", script_arg);
		SGS_destroy_File(f);
		return NULL;
	}
	return f;
}

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SGS_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SGS_File *f = open_file(script_arg, is_path);
	if (!f) return NULL;

	SGS_Program *o = NULL;
	SGS_Script *sd = SGS_load_Script(f);
	if (!sd) goto CLOSE;
	o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
CLOSE:
	SGS_destroy_File(f);
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
