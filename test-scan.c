/* saugns: Test program for experimental loader code.
 * Copyright (c) 2017-2021 Joel K. Pettersson
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
# include "loader/scanner.h"
#else
# include "loader/lexer.h"
#endif
#include "loader/file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define NAME "test-scan"

/*
 * Print command line usage instructions.
 */
static void print_usage(void) {
	fputs(
"Usage: "NAME" [-c] [-p] [-e] <script>...\n"
"\n"
"  -e \tEvaluate strings instead of files.\n"
"  -c \tCheck scripts only, reporting any errors or requested info.\n"
"  -p \tPrint info for scripts after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
		stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	fputs(NAME" ("SAU_CLINAME_STR") "SAU_VERSION_STR"\n", stderr);
}

/*
 * Parse command line arguments.
 *
 * Print usage instructions if requested or args invalid.
 *
 * \return true if args valid and script path set
 */
static bool parse_args(int argc, char **restrict argv,
		uint32_t *restrict flags,
		SAU_PtrArr *restrict script_args) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!script_args->count) goto USAGE;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			SAU_PtrArr_add(script_args, (void*) arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'c':
			if ((*flags & SAU_OPT_MODE_FULL) != 0)
				goto USAGE;
			*flags |= SAU_OPT_MODE_CHECK;
			break;
		case 'e':
			*flags |= SAU_OPT_EVAL_STRING;
			break;
		case 'h':
			goto USAGE;
		case 'p':
			*flags |= SAU_OPT_PRINT_INFO;
			break;
		case 'v':
			print_version();
			goto ABORT;
		default:
			goto USAGE;
		}
		goto NEXT_C;
	}
	return true;
USAGE:
	print_usage();
ABORT:
	SAU_PtrArr_clear(script_args);
	return false;
}

/*
 * Discard the scripts in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SAU_discard(SAU_PtrArr *restrict script_objs) {
	SAU_Script **scripts = (SAU_Script**) SAU_PtrArr_ITEMS(script_objs);
	for (size_t i = 0; i < script_objs->count; ++i) {
		free(scripts[i]); // for placeholder
	}
	SAU_PtrArr_clear(script_objs);
}

/*
 * Run script through test code.
 *
 * \return SAU_Script or NULL on error
 */
static SAU_Script *build_program(const char *restrict script_arg,
		bool is_path) {
	SAU_Script *o = NULL;
	SAU_MemPool *mempool = SAU_create_MemPool(0);
	SAU_SymTab *symtab = SAU_create_SymTab(mempool);
	if (!symtab)
		return NULL;
#if SAU_TEST_SCANNER
	SAU_Scanner *scanner = SAU_create_Scanner(symtab);
	if (!scanner) goto CLOSE;
	if (!SAU_Scanner_open(scanner, script_arg, is_path)) goto CLOSE;
	for (;;) {
		uint8_t c = SAU_Scanner_getc(scanner);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
	o = (SAU_Script*) calloc(1, sizeof(SAU_Script)); // placeholder
CLOSE:
	SAU_destroy_Scanner(scanner);
#else
	SAU_Lexer *lexer = SAU_create_Lexer(symtab);
	if (!lexer) goto CLOSE;
	if (!SAU_Lexer_open(lexer, script_arg, is_path)) goto CLOSE;
	for (;;) {
		SAU_ScriptToken token;
		if (!SAU_Lexer_get(lexer, &token)) break;
	}
	o = (SAU_Script*) calloc(1, sizeof(SAU_Script)); // placeholder
CLOSE:
	SAU_destroy_Lexer(lexer);
#endif
	SAU_destroy_SymTab(symtab);
	SAU_destroy_MemPool(mempool);
	return o;
}

/**
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the script list.
 *
 * \return number of items successfully processed
 */
size_t SAU_load(const SAU_PtrArr *restrict script_args, uint32_t options,
		SAU_PtrArr *restrict script_objs) {
	bool are_paths = !(options & SAU_OPT_EVAL_STRING);
	size_t built = 0;
	const char **args = (const char**) SAU_PtrArr_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SAU_Script *script = build_program(args[i], are_paths);
		if (script != NULL) ++built;
		SAU_PtrArr_add(script_objs, script);
	}
	return built;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SAU_PtrArr script_args = (SAU_PtrArr){0};
	SAU_PtrArr script_objs = (SAU_PtrArr){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !SAU_load(&script_args, options, &script_objs);
	SAU_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (script_objs.count > 0) {
		// no audio output
		SAU_discard(&script_objs);
	}
	return 0;
}
