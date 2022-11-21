/* mgensys: Audio program builder module.
 * Copyright (c) 2011-2013, 2017-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
static mgsProgram *build_program(const char *restrict script_arg,
		bool is_path) {
	return mgs_create_Program(script_arg, is_path);
//	mgsScript *sd = mgs_load_Script(script_arg, is_path);
//	if (!sd)
//		return NULL;
//	mgsProgram *o = mgs_build_Program(sd);
//	mgs_discard_Script(sd);
//	return o;
}

/**
 * Build the listed scripts, adding each result (even if NULL)
 * to the program list. (NULL scripts are ignored.)
 *
 * \return number of failures for non-NULL scripts
 */
size_t mgs_build(const mgsPtrArr *restrict script_args, uint32_t options,
		mgsPtrArr *restrict prg_objs) {
	size_t fails = 0;
	const char **args = (const char**) mgsPtrArr_ITEMS(script_args);
	bool are_paths = !(options & MGS_OPT_EVAL_STRING);
	bool print_info = (options & MGS_OPT_PRINT_INFO) != 0;
	for (size_t i = 0; i < script_args->count; ++i) {
		if (!args[i]) continue;
		mgsProgram *prg = build_program(args[i], are_paths);
		if (!prg)
			++fails;
		else if (print_info)
			(void)0; // mgsProgram_print_info(prg);
		mgsPtrArr_add(prg_objs, prg);
	}
	return fails;
}

/**
 * Discard the programs in the list, and clear the list.
 * NULL pointer entries are ignored.
 */
void mgs_discard(mgsPtrArr *restrict prg_objs) {
	mgsProgram **prgs = (mgsProgram**) mgsPtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		mgs_destroy_Program(prgs[i]);
	}
	mgsPtrArr_clear(prg_objs);
}
