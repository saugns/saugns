/* sgensys: Audio program builder module.
 * Copyright (c) 2011-2013, 2017-2018 Joel K. Pettersson
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
#include "builder/script.h"
#if SGS_TEST_SCANNER
# include "builder/file.h"
# include "builder/scanner.h"
#elif SGS_TEST_LEXER
# include "builder/lexer.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Create program for the given script file. Invokes the parser.
 *
 * \return instance or NULL on error
 */
SGS_Program* SGS_build(const char *fname) {
#if SGS_TEST_SCANNER
	SGS_File f;
	SGS_init_File(&f);
	if (!SGS_File_fopenrb(&f, fname)) {
		return false;
	}
	SGS_Scanner *scanner = SGS_create_Scanner(&f);
	for (;;) {
		char c = SGS_Scanner_getc(scanner);
		putchar(c);
		if (!c) {
			putchar('\n');
			break;
		}
	}
	SGS_destroy_Scanner(scanner);
	SGS_fini_File(&f);
	return (SGS_Program*) calloc(1, sizeof(SGS_Program)); //0;
#elif SGS_TEST_LEXER
	SGS_SymTab *symtab = SGS_create_SymTab();
	SGS_Lexer *lexer = SGS_create_Lexer(fname, symtab);
	if (!lexer) {
		SGS_destroy_SymTab(symtab);
		return false;
	}
	for (;;) {
		SGS_ScriptToken token;
		if (!SGS_Lexer_get(lexer, &token)) break;
	}
	SGS_destroy_Lexer(lexer);
	SGS_destroy_SymTab(symtab);
	return (SGS_Program*) calloc(1, sizeof(SGS_Program)); //0;
#else // OLD PARSER
	SGS_Script *sd = SGS_load_Script(fname);
	if (!sd) return NULL;

	SGS_Program *o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
	if (!o) return NULL;
	return o;
#endif
}
