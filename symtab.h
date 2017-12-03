/* sgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2017 Joel K. Pettersson
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

struct SGSSymtab;
typedef struct SGSSymtab SGSSymtab;

SGSSymtab* SGS_symtab_create(void);
void SGS_symtab_destroy(SGSSymtab *o);

void* SGS_symtab_get(SGSSymtab *o, const char *key);
void* SGS_symtab_set(SGSSymtab *o, const char *key, void *value);
