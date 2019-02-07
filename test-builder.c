/* saugns: Test program for experimental builder code.
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
#if SAU_TEST_SCANNER
# include "builder/scanner.h"
#else
# include "builder/lexer.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: test-builder [-c] [-p] scriptfile(s)\n"
"\n"
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
	ARG_ONLY_COMPILE = 1<<3,
	ARG_PRINT_INFO = 1<<4,
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
		SAU_PtrList *restrict script_paths) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_paths->count) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SAU_PtrList_add(script_paths, arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'c':
			if ((*flags & ARG_FULL_RUN) != 0)
				goto INVALID;
			*flags |= ARG_ONLY_COMPILE;
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
	return (script_paths->count != 0);

INVALID:
	print_usage(false);
CLEAR:
	SAU_PtrList_clear(script_paths);
	return false;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
static void discard_programs(SAU_PtrList *restrict prg_list) {
	SAU_Program **prgs = (SAU_Program**) SAU_PtrList_ITEMS(prg_list);
	for (size_t i = 0; i < prg_list->count; ++i) {
		SAU_Program *prg = prgs[i];
		if (prg != NULL) SAU_discard_Program(prg);
	}
	SAU_PtrList_clear(prg_list);
}

/**
 * Run script through test code.
 *
 * \return SAU_Program or NULL on error
 */
static SAU_Program *build_program(const char *restrict fname) {
#if SAU_TEST_SCANNER
	SAU_Scanner *scanner = SAU_create_Scanner();
	if (SAU_Scanner_fopenrb(scanner, fname)) for (;;) {
		uint8_t c = SAU_Scanner_getc(scanner);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
	SAU_destroy_Scanner(scanner);
	// return dummy object
	return (SAU_Program*) calloc(1, sizeof(SAU_Program));
#else
	SAU_SymTab *symtab = SAU_create_SymTab();
	SAU_Lexer *lexer = SAU_create_Lexer(fname, symtab);
	if (!lexer) {
		SAU_destroy_SymTab(symtab);
		return false;
	}
	for (;;) {
		SAU_ScriptToken token;
		if (!SAU_Lexer_get(lexer, &token)) break;
	}
	SAU_destroy_Lexer(lexer);
	SAU_destroy_SymTab(symtab);
	// return dummy object
	return (SAU_Program*) calloc(1, sizeof(SAU_Program));
#endif
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

/*
 * Process the listed script files.
 *
 * \return true if at least one script succesfully built
 */
static bool build(const SAU_PtrList *restrict path_list,
		SAU_PtrList *restrict prg_list,
		uint32_t options) {
	if (!SAU_build(path_list, prg_list))
		return false;
	if ((options & ARG_PRINT_INFO) != 0) {
		const SAU_Program **prgs =
			(const SAU_Program**) SAU_PtrList_ITEMS(prg_list);
		for (size_t i = 0; i < prg_list->count; ++i) {
			const SAU_Program *prg = prgs[i];
			if (prg != NULL) SAU_Program_print_info(prg);
		}
	}
	if ((options & ARG_ONLY_COMPILE) != 0) {
		discard_programs(prg_list);
	}
	return true;
}

/**
 * Main function.
 */
int main(int argc, char **argv) {
	SAU_PtrList script_paths = (SAU_PtrList){0};
	SAU_PtrList prg_list = (SAU_PtrList){0};
	uint32_t options = 0;

	if (!parse_args(argc, argv, &options, &script_paths))
		return 0;

	bool error = !build(&script_paths, &prg_list, options);
	SAU_PtrList_clear(&script_paths);
	if (error)
		return 1;
	if (prg_list.count > 0) {
		// no audio output
		discard_programs(&prg_list);
	}
	return 0;
}
