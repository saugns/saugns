/* sgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2020 Joel K. Pettersson
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
#include "../mempool.h"

/**
 * Item stored for each unique string associated with the symbol table.
 */
typedef struct SGS_SymStr {
	struct SGS_SymStr *prev;
	void *data;
	size_t key_len;
	char key[];
} SGS_SymStr;

struct SGS_SymTab;
typedef struct SGS_SymTab SGS_SymTab;

SGS_SymTab *SGS_create_SymTab(SGS_MemPool *restrict mempool) sgsMalloclike;
void SGS_destroy_SymTab(SGS_SymTab *restrict o);

SGS_SymStr *SGS_SymTab_get_symstr(SGS_SymTab *restrict o,
		const void *restrict str, size_t len);

/**
 * Get the unique copy of \p str held in the symbol table,
 * adding \p str to the string pool unless already present.
 *
 * \return unique copy of \p str, or NULL on allocation failure
 */
static inline const void *SGS_SymTab_pool_str(SGS_SymTab *restrict o,
		const void *restrict str, size_t len) {
	SGS_SymStr *item = SGS_SymTab_get_symstr(o, str, len);
	return (item != NULL) ? item->key : NULL;
}
const char **SGS_SymTab_pool_stra(SGS_SymTab *restrict o,
		const char *const* restrict stra,
		size_t n);
