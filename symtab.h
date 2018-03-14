/* sgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2018 Joel K. Pettersson
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
#include "sgensys.h"

struct SGS_SymTab;
typedef struct SGS_SymTab *SGS_SymTab_t;

SGS_SymTab_t SGS_create_symtab(void);
void SGS_destroy_symtab(SGS_SymTab_t o);

const char *SGS_symtab_pool_str(SGS_SymTab_t o, const char *str,
		uint32_t len);

void *SGS_symtab_get(SGS_SymTab_t o, const char *key);
void *SGS_symtab_set(SGS_SymTab_t o, const char *key, void *value);
