/* mgensys: Audio program builder module.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "../mgensys.h"

/*
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
static MGS_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	return MGS_create_Program(script_arg, is_path);
//	MGS_Script *sd = MGS_load_Script(script_arg, is_path);
//	if (!sd)
//		return NULL;
//	MGS_Program *o = MGS_build_Program(sd);
//	MGS_discard_Script(sd);
//	return o;
}

/**
 * Build the listed scripts, adding each result (even if NULL)
 * to the program list. (NULL scripts are ignored.)
 *
 * \return number of failures for non-NULL scripts
 */
size_t MGS_build(const MGS_PtrArr *restrict script_args, uint32_t options,
		MGS_PtrArr *restrict prg_objs) {
	size_t fails = 0;
	const char **args = (const char**) MGS_PtrArr_ITEMS(script_args);
	bool are_paths = !(options & MGS_OPT_EVAL_STRING);
	bool print_info = (options & MGS_OPT_PRINT_INFO) != 0;
	for (size_t i = 0; i < script_args->count; ++i) {
		if (!args[i]) continue;
		MGS_Program *prg = build_program(args[i], are_paths);
		if (!prg)
			++fails;
		else if (print_info)
			(void)0; // MGS_Program_print_info(prg);
		MGS_PtrArr_add(prg_objs, prg);
	}
	return fails;
}

/**
 * Discard the programs in the list, and clear the list.
 * NULL pointer entries are ignored.
 */
void MGS_discard(MGS_PtrArr *restrict prg_objs) {
	MGS_Program **prgs = (MGS_Program**) MGS_PtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		MGS_destroy_Program(prgs[i]);
	}
	MGS_PtrArr_clear(prg_objs);
}
