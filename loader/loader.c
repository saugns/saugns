/* saugns: Audio script loader / program builder module.
 * Copyright (c) 2011-2013, 2017-2021 Joel K. Pettersson
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

#include "../saugns.h"
#include "../script.h"

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static SAU_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SAU_Script *sd = SAU_load_Script(script_arg, is_path);
	if (!sd)
		return NULL;
	SAU_Program *o = SAU_build_Program(sd);
	SAU_discard_Script(sd);
	return o;
}

/**
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the program list.
 *
 * \return number of items successfully processed
 */
size_t SAU_load(const SAU_PtrArr *restrict script_args, uint32_t options,
		SAU_PtrArr *restrict prg_objs) {
	bool are_paths = !(options & SAU_OPT_EVAL_STRING);
	size_t built = 0;
	const char **args = (const char**) SAU_PtrArr_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SAU_Program *prg = build_program(args[i], are_paths);
		if (prg != NULL) ++built;
		SAU_PtrArr_add(prg_objs, prg);
	}
	return built;
}
