/* saugns: Test program for script interpreter code.
 * Copyright (c) 2017-2019 Joel K. Pettersson
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
#include "interp/interp.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: test-interp [-c] [-p] <script>...\n"
"       test-interp [-c] [-p] -e <string>...\n"
"\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
	(by_arg) ? stdout : stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(SAU_VERSION_STR);
}

/*
 * Command line argument flags.
 */
enum {
	ARG_FULL_RUN = 1<<0, /* identifies any non-compile-only flags */
	ARG_ENABLE_AUDIO_DEV = 1<<1,
	ARG_DISABLE_AUDIO_DEV = 1<<2,
	ARG_ONLY_CHECK = 1<<3,
	ARG_PRINT_INFO = 1<<4,
	ARG_EVAL_STRING = 1<<5,
};

/*
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags,
		SAU_PtrList *restrict script_args) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_args->count) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SAU_PtrList_add(script_args, arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'c':
			if ((*flags & ARG_FULL_RUN) != 0)
				goto INVALID;
			*flags |= ARG_ONLY_CHECK;
			break;
		case 'e':
			*flags |= ARG_EVAL_STRING;
			break;
		case 'h':
			if (*flags != 0) goto INVALID;
			print_usage(true);
			goto CLEAR;
		case 'p':
			*flags |= ARG_PRINT_INFO;
			break;
		case 'v':
			print_version();
			goto CLEAR;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (script_args->count != 0);
INVALID:
	print_usage(false);
CLEAR:
	SAU_PtrList_clear(script_args);
	return false;
}

/*
 * Run the programs in the list through test code, ignoring NULL entries.
 */
static void run_interp(const SAU_PtrList *restrict prg_objs,
		uint32_t options) {
	SAU_PtrList res_objs = (SAU_PtrList){0};
	SAU_interpret(prg_objs, &res_objs);
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
static void discard_programs(SAU_PtrList *restrict prg_objs) {
	SAU_Program **prgs = (SAU_Program**) SAU_PtrList_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		SAU_discard_Program(prgs[i]);
	}
	SAU_PtrList_clear(prg_objs);
}

/*
 * Process the listed scripts.
 *
 * \return true if at least one script succesfully built
 */
static bool build(const SAU_PtrList *restrict script_args,
		SAU_PtrList *restrict prg_objs,
		uint32_t options) {
	bool are_paths = !(options & ARG_EVAL_STRING);
	SAU_build(script_args, are_paths, prg_objs);
	const SAU_Program **prgs =
		(const SAU_Program**) SAU_PtrList_ITEMS(prg_objs);
	size_t built = 0;
	for (size_t i = 0; i < prg_objs->count; ++i) {
		const SAU_Program *prg = prgs[i];
		if (prg != NULL) {
			++built;
			if ((options & ARG_PRINT_INFO) != 0)
				SAU_Program_print_info(prg);
		}
	}
	if (!built)
		return false;
	if ((options & ARG_ONLY_CHECK) != 0) {
		discard_programs(prg_objs);
	}
	return true;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SAU_PtrList script_args = (SAU_PtrList){0};
	SAU_PtrList prg_objs = (SAU_PtrList){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !build(&script_args, &prg_objs, options);
	SAU_PtrList_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		run_interp(&prg_objs, options);
		// no audio output
		discard_programs(&prg_objs);
	}
	return 0;
}
