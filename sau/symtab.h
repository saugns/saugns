/* SAU library: Symbol table module.
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
#include "mempool.h"

/**
 * Node stored for each unique string associated with the symbol table.
 */
typedef struct SAU_Symstr {
	struct SAU_Symstr *prev;
	struct SAU_Symitem *item; // the last item with this string
	uint32_t key_len;
	uint8_t key[];
} SAU_Symstr;

/** Data type used in a symbol item. */
enum {
	SAU_SYM_DATA_NONE = 0,
	SAU_SYM_DATA_ID,
	SAU_SYM_DATA_NUM,
	SAU_SYM_DATA_OBJ,
};

/**
 * Item with type, string, and data.
 */
typedef struct SAU_Symitem {
	uint32_t sym_type;
	uint32_t data_use;
	struct SAU_Symitem *prev; // the previous item with this string
	SAU_Symstr *sstr;
	union {
		uint32_t id;
		double num;
		void *obj;
	} data;
} SAU_Symitem;

struct SAU_Symtab;
typedef struct SAU_Symtab SAU_Symtab;

SAU_Symtab *SAU_create_Symtab(SAU_Mempool *restrict mempool) sauMalloclike;

SAU_Symstr *SAU_Symtab_get_symstr(SAU_Symtab *restrict o,
		const void *restrict str, size_t len);

SAU_Symitem *SAU_Symtab_add_item(SAU_Symtab *restrict o,
		SAU_Symstr *restrict symstr, uint32_t type_id);
SAU_Symitem *SAU_Symtab_find_item(SAU_Symtab *restrict o,
		SAU_Symstr *restrict symstr, uint32_t type_id);

bool SAU_Symtab_add_stra(SAU_Symtab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t type_id);
