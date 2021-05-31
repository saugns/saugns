/* sgensys: Test program for experimental builder code.
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

#include "sgensys.h"
#if SGS_TEST_SCANNER
# include "builder/scanner.h"
#else
# include "builder/lexer.h"
#endif
#include "builder/file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Print command line usage instructions.
 */
static void print_usage(bool by_arg) {
	fputs(
"Usage: test-builder [-c] [-p] script\n"
"       test-builder [-c] [-p] -e string\n"
"\n"
"  -e \tEvaluate string instead of file.\n"
"  -c \tCheck script only, reporting any errors or requested info.\n"
"  -p \tPrint info for script after loading.\n"
"  -h \tPrint this message.\n"
"  -v \tPrint version.\n",
	(by_arg) ? stdout : stderr);
}

/*
 * Print version.
 */
static void print_version(void) {
	puts(SGS_VERSION_STR);
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
		const char **restrict script_arg) {
	for (;;) {
		const char *arg;
		--argc;
		++argv;
		if (argc < 1) {
			if (!*script_arg) goto INVALID;
			break;
		}
		arg = *argv;
		if (*arg != '-') {
			if (*script_arg) goto INVALID;
			*script_arg = arg;
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
		case 'e':
			*flags |= ARG_EVAL_STRING;
			break;
		case 'h':
			if (*flags != 0) goto INVALID;
			print_usage(true);
			return false;
		case 'p':
			*flags |= ARG_PRINT_INFO;
			break;
		case 'v':
			print_version();
			return false;
		default:
			goto INVALID;
		}
		goto NEXT_C;
	}
	return (*script_arg != NULL);

INVALID:
	print_usage(false);
	return false;
}

/**
 * Run script through test code.
 *
 * \return SGS_Program or NULL on error
 */
SGS_Program* SGS_build(const char *restrict script_arg, bool is_path) {
	SGS_Program *o = NULL;
	SGS_SymTab *symtab = SGS_create_SymTab();
	if (!symtab) return NULL;
#if SGS_TEST_SCANNER
	SGS_Scanner *scanner = SGS_create_Scanner(symtab);
	if (!scanner) goto CLOSE;
	if (!SGS_Scanner_open(scanner, script_arg, is_path)) goto CLOSE;
	for (;;) {
		uint8_t c = SGS_Scanner_getc(scanner);
		if (!c) {
			putchar('\n');
			break;
		}
		putchar(c);
	}
	o = (SGS_Program*) calloc(1, sizeof(SGS_Program)); // placeholder
CLOSE:
	if (scanner) SGS_destroy_Scanner(scanner);
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
	if (lexer) SGS_destroy_Lexer(lexer);
#endif
	SGS_destroy_SymTab(symtab);
	return o;
}

/*
 * Process the given script file.
 *
 * \return true unless error occurred
 */
static bool build(const char *restrict script_arg,
		SGS_Program **restrict prg_out,
		uint32_t options) {
	SGS_Program *prg;
	bool is_path = !(options & ARG_EVAL_STRING);
	if (!(prg = SGS_build(script_arg, is_path)))
		return false;
	if ((options & ARG_PRINT_INFO) != 0)
		SGS_Program_print_info(prg);
	if ((options & ARG_ONLY_COMPILE) != 0) {
		SGS_discard_Program(prg);
		*prg_out = NULL;
		return true;
	}

	*prg_out = prg;
	return true;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	const char *script_arg = NULL;
	uint32_t options = 0;
	SGS_Program *prg;

	if (!parse_args(argc, argv, &options, &script_arg))
		return 0;
	if (!build(script_arg, &prg, options))
		return 1;
	if (prg != NULL) {
		// no audio output
		SGS_discard_Program(prg);
	}

	return 0;
}
