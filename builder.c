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
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "script.h"
#if SGS_TEST_LEXER
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
SGSProgram* SGS_build(const char *fname) {
#if SGS_TEST_LEXER
	SGSSymtab *symtab = SGS_create_symtab();
	SGSLexer *lexer = SGS_create_lexer(fname, symtab);
	if (!lexer) return NULL;
	for (;;) {
		SGSToken *token = SGS_get_token(lexer);
		if (token->type <= 0) break;
	}
	SGS_destroy_lexer(lexer);
	SGS_destroy_symtab(symtab);
	return (SGSProgram*) calloc(1, sizeof(SGSProgram)); //0;
#else // OLD PARSER
	SGSScript *sd = SGS_load_Script(fname);
	if (!sd) return NULL;

	SGSProgram *o = SGS_build_Program(sd);
	SGS_discard_Script(sd);
	if (!o) return NULL;
	return o;
#endif
}
