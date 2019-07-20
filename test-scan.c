/* sgensys: Test program for experimental reader code.
 * Copyright (c) 2017-2022 Joel K. Pettersson
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
#if SGS_TEST_SCANNER
# include "reader/scanner.h"
#else
# include "reader/lexer.h"
#endif
#include "reader/file.h"
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
	puts(NAME" ("SGS_CLINAME_STR") "SGS_VERSION_STR);
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
		SGS_PtrArr *restrict script_args) {
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
			SGS_PtrArr_add(script_args, (void*) arg);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'c':
			if ((*flags & SGS_OPT_MODE_FULL) != 0)
				goto USAGE;
			*flags |= SGS_OPT_MODE_CHECK;
			break;
		case 'e':
			*flags |= SGS_OPT_EVAL_STRING;
			break;
		case 'h':
			goto USAGE;
		case 'p':
			*flags |= SGS_OPT_PRINT_INFO;
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
	SGS_PtrArr_clear(script_args);
	return false;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SGS_discard(SGS_PtrArr *restrict prg_objs) {
	SGS_Program **prgs = (SGS_Program**) SGS_PtrArr_ITEMS(prg_objs);
	for (size_t i = 0; i < prg_objs->count; ++i) {
		free(prgs[i]); // for placeholder
	}
	SGS_PtrArr_clear(prg_objs);
}

#if SGS_TEST_SCANNER
/*
 * Functions for scanning a file and printing the
 * contents with whitespace and comment filtering
 * as is done by default.
 */

static inline void scan_simple(SGS_Scanner *o) {
	for (;;) {
		uint8_t c = SGS_Scanner_getc(o);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
}

static inline void scan_with_undo(SGS_Scanner *o) {
	for (;;) {
		uint32_t i = 0, max = SGS_SCAN_UNGET_MAX;
		uint8_t c;
		bool end = false;
		for (i = 0; ++i <= max; ) {
			c = SGS_Scanner_getc(o);
			if (!c) {
				end = true;
				++i;
				break;
			}
		}
		max = i - 1;
		for (i = 0; ++i <= max; ) {
			SGS_Scanner_ungetc(o);
		}
		for (i = 0; ++i <= max; ) {
			c = SGS_Scanner_getc(o);
			putchar(c);
//			putchar('\n'); // for scanner.c test/debug printouts
		}
//		putchar('\n'); // for scanner.c test/debug printouts
		if (end) {
			putchar('\n');
			break;
		}
	}
}
#endif

/*
 * Run script through test code.
 *
 * \return SGS_Program or NULL on error
 */
static SGS_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SGS_Program *o = NULL;
	SGS_MemPool *mempool = SGS_create_MemPool(0);
	SGS_SymTab *symtab = SGS_create_SymTab(mempool);
	if (!symtab)
		return NULL;
#if SGS_TEST_SCANNER
	SGS_Scanner *scanner = SGS_create_Scanner(symtab);
	if (!scanner) goto CLOSE;
	if (!SGS_Scanner_open(scanner, script_arg, is_path)) goto CLOSE;
	/* print file contents with whitespace and comment filtering */
	//scan_simple(scanner);
	scan_with_undo(scanner);
	o = (SGS_Program*) calloc(1, sizeof(SGS_Program)); // placeholder
CLOSE:
	SGS_destroy_Scanner(scanner);
#else
	SGS_Lexer *lexer = SGS_create_Lexer(symtab);
	if (!lexer) goto CLOSE;
	if (!SGS_Lexer_open(lexer, script_arg, is_path)) goto CLOSE;
	for (;;) {
		SGS_ScriptToken token;
		if (!SGS_Lexer_get(lexer, &token)) break;
	}
	o = (SGS_Program*) calloc(1, sizeof(SGS_Program)); // placeholder
CLOSE:
	SGS_destroy_Lexer(lexer);
#endif
	SGS_destroy_SymTab(symtab);
	SGS_destroy_MemPool(mempool);
	return o;
}

/**
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the program list.
 *
 * \return number of items successfully processed
 */
size_t SGS_read(const SGS_PtrArr *restrict script_args, uint32_t options,
		SGS_PtrArr *restrict prg_objs) {
	bool are_paths = !(options & SGS_OPT_EVAL_STRING);
	size_t built = 0;
	const char **args = (const char**) SGS_PtrArr_ITEMS(script_args);
	for (size_t i = 0; i < script_args->count; ++i) {
		SGS_Program *prg = build_program(args[i], are_paths);
		if (prg != NULL) ++built;
		SGS_PtrArr_add(prg_objs, prg);
	}
	return built;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SGS_PtrArr script_args = (SGS_PtrArr){0};
	SGS_PtrArr prg_objs = (SGS_PtrArr){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !SGS_read(&script_args, options, &prg_objs);
	SGS_PtrArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		// no audio output
		SGS_discard(&prg_objs);
	}
	return 0;
}
