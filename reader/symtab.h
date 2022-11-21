/* mgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2022 Joel K. Pettersson
 * <joelkp@tuta.io>.
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
typedef struct mgsSymStr {
	struct mgsSymStr *prev;
	struct mgsSymItem *item; // the last item with this string
	uint32_t key_len;
	char key[];
} mgsSymStr;

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
typedef struct mgsSymItem {
	uint32_t sym_type;
	uint32_t data_use;
	struct mgsSymItem *prev; // the previous item with this string
	mgsSymStr *sstr;
	union {
		uint32_t id;
		double num;
		void *obj;
	} data;
} mgsSymItem;

struct mgsSymTab;
typedef struct mgsSymTab mgsSymTab;

mgsSymTab *mgs_create_SymTab(mgsMemPool *restrict mempool) mgsMalloclike;

mgsSymStr *mgsSymTab_get_symstr(mgsSymTab *restrict o,
		const void *restrict str, size_t len);

mgsSymItem *mgsSymTab_add_item(mgsSymTab *restrict o,
		mgsSymStr *restrict symstr, uint32_t type_id);
mgsSymItem *mgsSymTab_find_item(mgsSymTab *restrict o,
		mgsSymStr *restrict symstr, uint32_t type_id);

bool mgsSymTab_add_stra(mgsSymTab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t type_id);
