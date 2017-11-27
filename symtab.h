/* sgensys: symbol table module.
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

#ifndef __SGS_symtab_h
#define __SGS_symtab_h

#include <stdint.h>

struct SGSSymtab;
typedef struct SGSSymtab SGSSymtab;

SGSSymtab* SGS_create_symtab(void);
void SGS_destroy_symtab(SGSSymtab *o);

const char *SGS_symtab_pool_str(SGSSymtab *o, const char *str, uint32_t len);

void* SGS_symtab_get(SGSSymtab *o, const char *key);
void* SGS_symtab_set(SGSSymtab *o, const char *key, void *value);

#endif /* EOF */
