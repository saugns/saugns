/* sgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2019 Joel K. Pettersson
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
#include "sgensys.h"

struct SGS_Symtab;
typedef struct SGS_Symtab SGS_Symtab;

SGS_Symtab *SGS_create_Symtab(void) SGS__malloclike;
void SGS_destroy_Symtab(SGS_Symtab *restrict o);

const void *SGS_Symtab_pool_str(SGS_Symtab *restrict o,
		const void *restrict str, size_t len);

void *SGS_Symtab_get(SGS_Symtab *restrict o,
		const void *restrict key, size_t len);
void *SGS_Symtab_set(SGS_Symtab *restrict o,
		const void *restrict key, size_t len, void *restrict value);
