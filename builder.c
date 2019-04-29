/* sgensys: Audio program builder module.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "script.h"
#if SGS_TEST_LEXER
# include "builder/lexer.h"
#endif
#include "builder/file.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Open file for script arg.
 *
 * \return instance or NULL on error
 */
static SGS_File *open_file(const char *script_arg, bool is_path) {
	SGS_File *f = SGS_create_File();
	if (!f) return NULL;
	if (!is_path) {
		SGS_File_stropenrb(f, "-e ...", script_arg);
		return f;
	}
	if (!SGS_File_fopenrb(f, script_arg)) {
		SGS_error(NULL,
"couldn't open script file \"%s\" for reading", script_arg);
		SGS_destroy_File(f);
		return NULL;
	}
	return f;
}

/**
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SGS_Program* SGS_build(const char *script_arg, bool is_path) {
	SGS_File *f = open_file(script_arg, is_path);
	if (!f) return NULL;

	SGS_Program *o = NULL;
#if SGS_TEST_LEXER
	SGS_SymTab *symtab = SGS_create_SymTab();
	SGS_Lexer *lexer = SGS_create_Lexer(f, symtab);
	if (!lexer) {
		SGS_destroy_SymTab(symtab);
		goto CLOSE;
	}
	for (;;) {
		SGS_ScriptToken token;
		if (!SGS_Lexer_get(lexer, &token)) break;
	}
	SGS_destroy_Lexer(lexer);
	SGS_destroy_SymTab(symtab);
	o = (SGS_Program*) calloc(1, sizeof(SGS_Program)); // placeholder
#else // OLD PARSER
	SGS_Script *sd = SGS_load_Script(f);
	if (!sd) goto CLOSE;

	o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
#endif
CLOSE:
	SGS_destroy_File(f);
	return o;
}
