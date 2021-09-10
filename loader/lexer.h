/* sgensys: Script lexer module.
 * Copyright (c) 2014, 2017-2021 Joel K. Pettersson
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

#pragma once
#include "symtab.h"

/*
 * Token enumerations.
 */
enum {
	SGS_T_INVALID = 0,
	SGS_T_ID_STR,
	SGS_T_VAL_INT,
	SGS_T_VAL_REAL,
	SGS_T_SPECIAL,
};

typedef struct SGS_ScriptToken {
	uint32_t type;
	union {
		const char *id;
		int32_t i;
		float f;
		uint8_t b;
		char c;
	} data;
} SGS_ScriptToken;

struct SGS_Lexer;
typedef struct SGS_Lexer SGS_Lexer;

SGS_Lexer *SGS_create_Lexer(SGS_SymTab *restrict symtab) sgsMalloclike;
void SGS_destroy_Lexer(SGS_Lexer *restrict o);

bool SGS_Lexer_open(SGS_Lexer *restrict o,
		const char *restrict script, bool is_path);
void SGS_Lexer_close(SGS_Lexer *restrict o);

bool SGS_Lexer_get(SGS_Lexer *restrict o, SGS_ScriptToken *restrict t);
bool SGS_Lexer_get_special(SGS_Lexer *restrict o, SGS_ScriptToken *restrict t);
