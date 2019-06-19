/* saugns: Symbol table module.
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

struct SAU_SymTab;
typedef struct SAU_SymTab SAU_SymTab;

SAU_SymTab *SAU_create_SymTab(void) SAU__malloclike;
void SAU_destroy_SymTab(SAU_SymTab *restrict o);

const void *SAU_SymTab_pool_str(SAU_SymTab *restrict o,
		const void *restrict str, size_t len);
const char **SAU_SymTab_pool_stra(SAU_SymTab *restrict o,
		const char *const* restrict stra,
		size_t n);

void *SAU_SymTab_get(SAU_SymTab *restrict o,
		const void *restrict key, size_t len);
void *SAU_SymTab_set(SAU_SymTab *restrict o,
		const void *restrict key, size_t len, void *restrict value);
