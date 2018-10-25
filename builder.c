/* ssndgen: Audio program builder module.
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

#include "ssndgen.h"
#include "script.h"
#if SSG_TEST_LEXER
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
SSG_Program* SSG_build(const char *fname) {
#if SSG_TEST_LEXER
	SSG_SymTab *symtab = SSG_create_SymTab();
	SSG_Lexer *lexer = SSG_create_Lexer(fname, symtab);
	if (!lexer) {
		SSG_destroy_SymTab(symtab);
		return false;
	}
	for (;;) {
		SSG_ScriptToken token;
		if (!SSG_Lexer_get(lexer, &token)) break;
	}
	SSG_destroy_Lexer(lexer);
	SSG_destroy_SymTab(symtab);
	return (SSG_Program*) calloc(1, sizeof(SSG_Program)); //0;
#else // OLD PARSER
	SSG_Script *sd = SSG_load_Script(fname);
	if (!sd) return NULL;

	SSG_Program *o = SSG_build_Program(sd);
	SSG_discard_Script(sd);
	if (!o) return NULL;
	return o;
#endif
}
