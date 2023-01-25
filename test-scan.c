/* saugns: Test program for experimental reader code.
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

#include "saugns.h"
#include <sau/program.h>
#include <sau/arrtype.h>
#if TEST_SCANNER
# include <sau/scanner.h>
#else
# include <sau/lexer.h>
#endif
#include <sau/file.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define NAME "test-scan"

/*
 * Command line options flags.
 */
enum {
	OPT_MODE_FULL     = 1<<0,
	OPT_SYSAU_ENABLE  = 1<<1,
	OPT_SYSAU_DISABLE = 1<<2,
	OPT_MODE_CHECK    = 1<<3,
	OPT_PRINT_INFO    = 1<<4,
	OPT_EVAL_STRING   = 1<<5,
};

struct sauScriptArg {
	const char *str;
};
sauArrType(sauScriptArgArr, struct sauScriptArg, )
sauArrType(sauProgramArr, sauProgram*, )

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
"  -V \tPrint version.\n",
		stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	fputs(NAME" ("CLINAME_STR") "VERSION_STR, stderr);
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
		sauScriptArgArr *restrict script_args) {
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
			struct sauScriptArg entry = {arg};
			sauScriptArgArr_add(script_args, &entry);
			continue;
		}
NEXT_C:
		if (!*++arg) continue;
		switch (*arg) {
		case 'V':
			print_version();
			goto ABORT;
		case 'c':
			if ((*flags & OPT_MODE_FULL) != 0)
				goto USAGE;
			*flags |= OPT_MODE_CHECK;
			break;
		case 'e':
			*flags |= OPT_EVAL_STRING;
			break;
		case 'h':
			goto USAGE;
		case 'p':
			*flags |= OPT_PRINT_INFO;
			break;
		default:
			goto USAGE;
		}
		goto NEXT_C;
	}
	return true;
USAGE:
	print_usage();
ABORT:
	sauScriptArgArr_clear(script_args);
	return false;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
static void discard(sauProgramArr *restrict prg_objs) {
	for (size_t i = 0; i < prg_objs->count; ++i) {
		free(prg_objs->a[i]); // for placeholder
	}
	sauProgramArr_clear(prg_objs);
}

#if TEST_SCANNER
/*
 * Functions for scanning a file and printing the
 * contents with whitespace and comment filtering
 * as is done by default.
 */

static inline void scan_simple(sauScanner *o) {
	for (;;) {
		uint8_t c = sauScanner_getc(o);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
}

static inline void scan_with_undo(sauScanner *o) {
	for (;;) {
		uint32_t i = 0, max = SAU_SCAN_UNGET_MAX;
		uint8_t c;
		bool end = false;
		for (i = 0; ++i <= max; ) {
			c = sauScanner_retc(o);
			c = sauScanner_getc(o);
			if (!c) {
				end = true;
				++i;
				break;
			}
		}
		max = i - 1;
		for (i = 0; ++i <= max; ) {
			sauScanner_ungetc(o);
		}
		for (i = 0; ++i <= max; ) {
			c = sauScanner_retc(o);
			c = sauScanner_getc(o);
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
 * \return sauProgram or NULL on error
 */
static sauProgram *build_program(const char *restrict script_arg,
		bool is_path) {
	sauProgram *o = NULL;
	sauMempool *mempool = sau_create_Mempool(0);
	sauSymtab *symtab = sau_create_Symtab(mempool);
	if (!symtab)
		return NULL;
#if TEST_SCANNER
	sauScanner *scanner = sau_create_Scanner(symtab);
	if (!scanner) goto CLOSE;
	if (!sauScanner_open(scanner, script_arg, is_path)) goto CLOSE;
	/* print file contents with whitespace and comment filtering */
	//scan_simple(scanner);
	scan_with_undo(scanner);
	o = (sauProgram*) calloc(1, sizeof(sauProgram)); // placeholder
CLOSE:
	sau_destroy_Scanner(scanner);
#else
	sauLexer *lexer = sau_create_Lexer(symtab);
	if (!lexer) goto CLOSE;
	if (!sauLexer_open(lexer, script_arg, is_path)) goto CLOSE;
	for (;;) {
		sauScriptToken token;
		if (!sauLexer_get(lexer, &token)) break;
	}
	o = (sauProgram*) calloc(1, sizeof(sauProgram)); // placeholder
CLOSE:
	sau_destroy_Lexer(lexer);
#endif
	sau_destroy_Mempool(mempool);
	return o;
}

/*
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the program list.
 *
 * \return number of items successfully processed
 */
static size_t read_scripts(const sauScriptArgArr *restrict script_args,
		uint32_t options, sauProgramArr *restrict prg_objs) {
	bool are_paths = !(options & OPT_EVAL_STRING);
	size_t built = 0;
	for (size_t i = 0; i < script_args->count; ++i) {
		const sauProgram *prg = build_program(script_args->a[i].str,
				are_paths);
		if (prg != NULL) ++built;
		sauProgramArr_add(prg_objs, &prg);
	}
	return built;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	sauScriptArgArr script_args = (sauScriptArgArr){0};
	sauProgramArr prg_objs = (sauProgramArr){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !read_scripts(&script_args, options, &prg_objs);
	sauScriptArgArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		// no audio output
		discard(&prg_objs);
	}
	return 0;
}
