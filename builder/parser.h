/* saugns: Script file parser.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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
#include "script.h"
#include "scanner.h"
#include "symtab.h"

#define SAU_PD_STRBUF_LEN 256

/**
 * Data used for the duration of a parse.
 */
typedef struct SAU_ParseData {
	SAU_ScriptOptions sopt;
	SAU_SymTab *st;
	const char *const*wave_names;
	const char *const*slope_names;
	uint8_t strbuf[SAU_PD_STRBUF_LEN];
} SAU_ParseData;

SAU_ParseData *SAU_create_ParseData(void) SAU__malloclike;
void SAU_destroy_ParseData(SAU_ParseData *o);
