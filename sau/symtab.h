/* SAU library: Symbol table module.
 * Copyright (c) 2011-2012, 2014, 2017-2024 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "mempool.h"

/**
 * Node stored for each unique string associated with the symbol table.
 */
typedef struct sauSymstr {
	struct sauSymstr *prev;
	struct sauSymitem *item; // the last item with this string
	uint32_t key_len;
	uint8_t key[];
} sauSymstr;

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
typedef struct sauSymitem {
	uint8_t sym_type;
	uint8_t data_use;
	uint32_t data_id; // can also hold extra ID for != SAU_SYM_DATA_ID
	struct sauSymitem *prev; // the previous item with this string
	sauSymstr *sstr;
	union {
		double num;
		void *obj;
	} data;
} sauSymitem;

struct sauSymtab;
typedef struct sauSymtab sauSymtab;

sauSymtab *sau_create_Symtab(sauMempool *restrict mempool) sauMalloclike;

sauSymstr *sauSymtab_get_symstr(sauSymtab *restrict o,
		const void *restrict str, size_t len);

sauSymitem *sauSymtab_add_item(sauSymtab *restrict o,
		sauSymstr *restrict symstr, uint32_t type_id);
sauSymitem *sauSymtab_find_item(sauSymtab *restrict o,
		sauSymstr *restrict symstr, uint32_t type_id);

bool sauSymtab_add_stra(sauSymtab *restrict o,
		const char *const*restrict stra, size_t n,
		uint32_t sym_type, uint32_t id_from);
