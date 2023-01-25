/* SAU library: Script lexer module.
 * Copyright (c) 2014, 2017-2022 Joel K. Pettersson
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
	SAU_T_INVALID = 0,
	SAU_T_ID_STR,
	SAU_T_VAL_INT,
	SAU_T_VAL_REAL,
	SAU_T_SPECIAL,
};

typedef struct sauScriptToken {
	uint32_t type;
	union {
		const char *id;
		int32_t i;
		float f;
		uint8_t b;
		char c;
	} data;
} sauScriptToken;

struct sauLexer;
typedef struct sauLexer sauLexer;

sauLexer *sau_create_Lexer(sauSymtab *restrict symtab) sauMalloclike;
void sau_destroy_Lexer(sauLexer *restrict o);

bool sauLexer_open(sauLexer *restrict o,
		const char *restrict script, bool is_path);
void sauLexer_close(sauLexer *restrict o);

bool sauLexer_get(sauLexer *restrict o, sauScriptToken *restrict t);
bool sauLexer_get_special(sauLexer *restrict o, sauScriptToken *restrict t);
