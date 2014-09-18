/* sgensys: script lexer module.
 * Copyright (c) 2014, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "symtab.h"

enum {
	SGS_T_ERROR = -1,
	SGS_T_EOF = 0,
	SGS_T_IDENTIFIER,
};

typedef struct SGSToken {
	int type;
	union {
	} data;
} SGSToken;

struct SGSLexer;
typedef struct SGSLexer SGSLexer;

SGSLexer *SGS_create_lexer(const char *filename, SGSSymtab *symtab);
void SGS_destroy_lexer(SGSLexer *o);

SGSToken *SGS_get_token(SGSLexer *o);
