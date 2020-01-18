/* sgensys: Test program for experimental reader code.
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

#include "sgensys.h"
#include "program.h"
#include "arrtype.h"
#if SGS_TEST_SCANNER
# include "scanner.h"
#else
# include "lexer.h"
#endif
#include "file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define NAME "test-scan"

/*
 * Command line options flags.
 */
enum {
	SGS_OPT_MODE_FULL     = 1<<0,
	SGS_OPT_AUDIO_ENABLE  = 1<<1,
	SGS_OPT_AUDIO_DISABLE = 1<<2,
	SGS_OPT_MODE_CHECK    = 1<<3,
	SGS_OPT_PRINT_INFO    = 1<<4,
	SGS_OPT_EVAL_STRING   = 1<<5,
};

struct SGS_ScriptArg {
	const char *str;
};
sgsArrType(SGS_ScriptArgArr, struct SGS_ScriptArg, )
sgsArrType(SGS_ProgramArr, SGS_Program*, )

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
		SGS_ScriptArgArr *restrict script_args) {
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
			struct SGS_ScriptArg entry = {arg};
			SGS_ScriptArgArr_add(script_args, &entry);
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
	SGS_ScriptArgArr_clear(script_args);
	return false;
}

/*
 * Discard the programs in the list, ignoring NULL entries,
 * and clearing the list.
 */
void SGS_discard(SGS_ProgramArr *restrict prg_objs) {
	for (size_t i = 0; i < prg_objs->count; ++i) {
		free(prg_objs->a[i]); // for placeholder
	}
	SGS_ProgramArr_clear(prg_objs);
}

/*
 * Run script through test code.
 *
 * \return SGS_Program or NULL on error
 */
static SGS_Program *build_program(const char *restrict script_arg,
		bool is_path) {
	SGS_Program *o = NULL;
	SGS_Mempool *mempool = SGS_create_Mempool(0);
	SGS_Symtab *symtab = SGS_create_Symtab(mempool);
	if (!symtab)
		return NULL;
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
	SGS_destroy_Symtab(symtab);
	SGS_destroy_Mempool(mempool);
	return o;
}

/*
 * Load the listed scripts and build inner programs for them,
 * adding each result (even if NULL) to the program list.
 *
 * \return number of items successfully processed
 */
size_t SGS_read(const SGS_ScriptArgArr *restrict script_args, uint32_t options,
		SGS_ProgramArr *restrict prg_objs) {
	bool are_paths = !(options & SGS_OPT_EVAL_STRING);
	size_t built = 0;
	for (size_t i = 0; i < script_args->count; ++i) {
		const SGS_Program *prg = build_program(script_args->a[i].str,
				are_paths);
		if (prg != NULL) ++built;
		SGS_ProgramArr_add(prg_objs, &prg);
	}
	return built;
}

/**
 * Main function.
 */
int main(int argc, char **restrict argv) {
	SGS_ScriptArgArr script_args = (SGS_ScriptArgArr){0};
	SGS_ProgramArr prg_objs = (SGS_ProgramArr){0};
	uint32_t options = 0;
	if (!parse_args(argc, argv, &options, &script_args))
		return 0;
	bool error = !SGS_read(&script_args, options, &prg_objs);
	SGS_ScriptArgArr_clear(&script_args);
	if (error)
		return 1;
	if (prg_objs.count > 0) {
		// no audio output
		SGS_discard(&prg_objs);
	}
	return 0;
}
