/* mgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2022 Joel K. Pettersson
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
 * Node stored for each unique string associated with the symbol table.
 */
typedef struct MGS_SymStr {
	struct MGS_SymStr *prev;
	struct MGS_SymItem *item; // the last item with this string
	uint32_t key_len;
	char key[];
} MGS_SymStr;

/** Data type used in a symbol item. */
enum {
	MGS_SYM_DATA_NONE = 0,
	MGS_SYM_DATA_ID,
	MGS_SYM_DATA_NUM,
	MGS_SYM_DATA_OBJ,
};

/**
 * Item with type, string, and data.
 */
typedef struct MGS_SymItem {
	uint32_t sym_type;
	uint32_t data_use;
	struct MGS_SymItem *prev; // the previous item with this string
	MGS_SymStr *sstr;
	union {
		uint32_t id;
		double num;
		void *obj;
	} data;
} MGS_SymItem;

struct MGS_SymTab;
typedef struct MGS_SymTab MGS_SymTab;

MGS_SymTab *MGS_create_SymTab(MGS_MemPool *restrict mempool) mgsMalloclike;

MGS_SymStr *MGS_SymTab_get_symstr(MGS_SymTab *restrict o,
		const void *restrict str, size_t len);

MGS_SymItem *MGS_SymTab_add_item(MGS_SymTab *restrict o,
		MGS_SymStr *restrict symstr, uint32_t type_id);
MGS_SymItem *MGS_SymTab_find_item(MGS_SymTab *restrict o,
		MGS_SymStr *restrict symstr, uint32_t type_id);

bool MGS_SymTab_add_stra(MGS_SymTab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t type_id);
