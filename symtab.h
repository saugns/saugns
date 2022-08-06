/* sgensys: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2023 Joel K. Pettersson
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
#include "sgensys.h"
#include "mempool.h"

/**
 * Node stored for each unique string associated with the symbol table.
 */
typedef struct SGS_Symstr {
	struct SGS_Symstr *prev;
	struct SGS_Symitem *item; // the last item with this string
	uint32_t key_len;
	uint8_t key[];
} SGS_Symstr;

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
typedef struct SGS_Symitem {
	uint32_t sym_type;
	uint32_t data_use;
	struct SGS_Symitem *prev; // the previous item with this string
	SGS_Symstr *sstr;
	union {
		uint32_t id;
		double num;
		void *obj;
	} data;
} SGS_Symitem;

struct SGS_Symtab;
typedef struct SGS_Symtab SGS_Symtab;

SGS_Symtab *SGS_create_Symtab(SGS_Mempool *restrict mempool) sgsMalloclike;

SGS_Symstr *SGS_Symtab_get_symstr(SGS_Symtab *restrict o,
		const void *restrict str, size_t len);

SGS_Symitem *SGS_Symtab_add_item(SGS_Symtab *restrict o,
		SGS_Symstr *restrict symstr, uint32_t type_id);
SGS_Symitem *SGS_Symtab_find_item(SGS_Symtab *restrict o,
		SGS_Symstr *restrict symstr, uint32_t type_id);

bool SGS_Symtab_add_stra(SGS_Symtab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t type_id);
