/* saugns: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2022 Joel K. Pettersson
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
 * Node stored for each unique string associated with the symbol table.
 */
typedef struct SAU_SymStr {
	struct SAU_SymStr *prev;
	uint32_t item_i; // the last item with this string
	uint32_t key_len;
	char key[];
} SAU_SymStr;

/**
 * Item with type, string, and data.
 */
typedef struct SAU_SymItem {
	uint32_t type_id;
	uint32_t id;
	uint32_t prev; // the previous item with this string
	void *data;
	SAU_SymStr *sstr;
} SAU_SymItem;

struct SAU_SymTab;
typedef struct SAU_SymTab SAU_SymTab;

SAU_SymTab *SAU_create_SymTab(SAU_MemPool *restrict mempool) sauMalloclike;
void SAU_destroy_SymTab(SAU_SymTab *restrict o);

SAU_SymStr *SAU_SymTab_get_symstr(SAU_SymTab *restrict o,
		const void *restrict str, size_t len);

SAU_SymItem *SAU_SymTab_add_item(SAU_SymTab *restrict o,
		SAU_SymStr *restrict symstr, uint32_t type_id);
SAU_SymItem *SAU_SymTab_find_item(SAU_SymTab *restrict o,
		SAU_SymStr *restrict symstr, uint32_t type_id);

bool SAU_SymTab_add_stra(SAU_SymTab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t type_id);
