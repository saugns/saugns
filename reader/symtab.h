/* sgensys: Symbol table module.
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
typedef struct SGS_SymStr {
	struct SGS_SymStr *prev;
	struct SGS_SymItem *item; // the last item with this string
	uint32_t key_len;
	char key[];
} SGS_SymStr;

/** Data type used in a symbol item. */
enum {
	SGS_SYM_DATA_NONE = 0,
	SGS_SYM_DATA_ID,
	SGS_SYM_DATA_NUM,
	SGS_SYM_DATA_OBJ,
};

/**
 * Item with type, string, and data.
 */
typedef struct SGS_SymItem {
	uint32_t sym_type;
	uint32_t data_use;
	struct SGS_SymItem *prev; // the previous item with this string
	SGS_SymStr *sstr;
	union {
		uint32_t id;
		double num;
		void *obj;
	} data;
} SGS_SymItem;

struct SGS_SymTab;
typedef struct SGS_SymTab SGS_SymTab;

SGS_SymTab *SGS_create_SymTab(SGS_MemPool *restrict mempool) sgsMalloclike;
void SGS_destroy_SymTab(SGS_SymTab *restrict o);

SGS_SymStr *SGS_SymTab_get_symstr(SGS_SymTab *restrict o,
		const void *restrict str, size_t len);

SGS_SymItem *SGS_SymTab_add_item(SGS_SymTab *restrict o,
		SGS_SymStr *restrict symstr, uint32_t type_id);
SGS_SymItem *SGS_SymTab_find_item(SGS_SymTab *restrict o,
		SGS_SymStr *restrict symstr, uint32_t type_id);

bool SGS_SymTab_add_stra(SGS_SymTab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t type_id);
