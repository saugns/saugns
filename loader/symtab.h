/* mgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2020 Joel K. Pettersson
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
#include "../mempool.h"

/**
 * Item stored for each unique string associated with the symbol table.
 */
typedef struct MGS_SymStr {
	struct MGS_SymStr *prev;
	void *data;
	size_t key_len;
	char key[];
} MGS_SymStr;

struct MGS_SymTab;
typedef struct MGS_SymTab MGS_SymTab;

MGS_SymTab *MGS_create_SymTab(MGS_MemPool *restrict mempool) mgsMalloclike;
void MGS_destroy_SymTab(MGS_SymTab *restrict o);

MGS_SymStr *MGS_SymTab_get_symstr(MGS_SymTab *restrict o,
		const void *restrict str, size_t len);

/**
 * Get the unique copy of \p str held in the symbol table,
 * adding \p str to the string pool unless already present.
 *
 * \return unique copy of \p str, or NULL on allocation failure
 */
static inline const void *MGS_SymTab_pool_str(MGS_SymTab *restrict o,
		const void *restrict str, size_t len) {
	MGS_SymStr *item = MGS_SymTab_get_symstr(o, str, len);
	return (item != NULL) ? item->key : NULL;
}
const char **MGS_SymTab_pool_stra(MGS_SymTab *restrict o,
		const char *const* restrict stra,
		size_t n);
