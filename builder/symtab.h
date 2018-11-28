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
#include "../common.h"

struct SGS_SymTab;
typedef struct SGS_SymTab SGS_SymTab;

SGS_SymTab *SGS_create_SymTab(void) SGS__malloclike;
void SGS_destroy_SymTab(SGS_SymTab *restrict o);

const void *SGS_SymTab_pool_str(SGS_SymTab *restrict o,
		const void *restrict str, size_t len);
const char **SGS_SymTab_pool_stra(SGS_SymTab *restrict o,
		const char *const* restrict stra, size_t n);

void *SGS_SymTab_get(SGS_SymTab *restrict o,
		const void *restrict key, size_t len);
void *SGS_SymTab_set(SGS_SymTab *restrict o,
		const void *restrict key, size_t len, void *restrict value);
