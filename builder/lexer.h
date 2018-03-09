/* sgensys: Script lexer module.
 * Copyright (c) 2014, 2017-2018 Joel K. Pettersson
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

#pragma once
#include "symtab.h"

/**
 * For those 1-character (special character) tokens that are defined, passing
 * the character will yield the token number.
 */
#define SGS_T_1CT(c) (0x100 + (c))

/*
 * Token enumerations.
 */
enum {
	SGS_T_ERROR = -1,
	SGS_T_EOF = 0,
	SGS_T_INVALID = 1,
	SGS_T_ID_STR,
	SGS_T_INT_NUM,
	SGS_T_REAL_NUM,
	/* individual special characters */
	SGS_T_BANG           = SGS_T_1CT('!'),
	SGS_T_QUOTATIONMARK  = SGS_T_1CT('"'),
	SGS_T_NUMBERSIGN     = SGS_T_1CT('#'),
	SGS_T_DOLLARSIGN     = SGS_T_1CT('$'),
	SGS_T_PERCENTSIGN    = SGS_T_1CT('%'),
	SGS_T_AMPERSAND      = SGS_T_1CT('&'),
	SGS_T_APOSTROPHE     = SGS_T_1CT('\''),
	SGS_T_LPARENTHESIS   = SGS_T_1CT('('),
	SGS_T_RPARENTHESIS   = SGS_T_1CT(')'),
	SGS_T_ASTERISK       = SGS_T_1CT('*'),
	SGS_T_PLUS           = SGS_T_1CT('+'),
	SGS_T_COMMA          = SGS_T_1CT(','),
	SGS_T_MINUS          = SGS_T_1CT('-'),
	SGS_T_DOT            = SGS_T_1CT('.'),
	SGS_T_SLASH          = SGS_T_1CT('/'),
	SGS_T_COLON          = SGS_T_1CT(':'),
	SGS_T_SEMICOLON      = SGS_T_1CT(';'),
	SGS_T_LESSTHAN       = SGS_T_1CT('<'),
	SGS_T_EQUALSSIGN     = SGS_T_1CT('='),
	SGS_T_GREATERTHAN    = SGS_T_1CT('>'),
	SGS_T_QUESTIONMARK   = SGS_T_1CT('?'),
	SGS_T_ATSIGN         = SGS_T_1CT('@'),
	SGS_T_LSQUAREBRACKET = SGS_T_1CT('['),
	SGS_T_BACKSLASH      = SGS_T_1CT('\\'),
	SGS_T_RSQUAREBRACKET = SGS_T_1CT(']'),
	SGS_T_CARET          = SGS_T_1CT('^'),
	SGS_T_UNDERSCORE     = SGS_T_1CT('_'),
	SGS_T_BACKTICK       = SGS_T_1CT('`'),
	SGS_T_LCURLYBRACKET  = SGS_T_1CT('{'),
	SGS_T_PIPE           = SGS_T_1CT('|'),
	SGS_T_RCURLYBRACKET  = SGS_T_1CT('}'),
	SGS_T_TILDE          = SGS_T_1CT('~'),
};

typedef struct SGS_ScriptToken {
	int32_t type;
	union {
		const char *id;
	} data;
} SGS_ScriptToken;

struct SGS_Lexer;
typedef struct SGS_Lexer SGS_Lexer;

SGS_Lexer *SGS_create_Lexer(const char *fname, SGS_SymTab *symtab);
void SGS_destroy_Lexer(SGS_Lexer *o);

SGS_ScriptToken *SGS_Lexer_get_token(SGS_Lexer *o);

void SGS_Lexer_warning(SGS_Lexer *o, const char *fmt, ...);
void SGS_Lexer_error(SGS_Lexer *o, const char *fmt, ...);
