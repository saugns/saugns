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
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "sgensys.h"

struct SGSSymtab;
typedef struct SGSSymtab SGSSymtab;

SGSSymtab *SGS_create_Symtab(void) SGS__malloclike;
void SGS_destroy_Symtab(SGSSymtab *o);

const void *SGSSymtab_pool_str(SGSSymtab *o,
		const void *str, size_t len);

void *SGSSymtab_get(SGSSymtab *o,
		const void *key, size_t len);
void *SGSSymtab_set(SGSSymtab *o,
		const void *key, size_t len, void *value);
