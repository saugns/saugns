/* saugns: Pointer list module.
 * Copyright (c) 2011-2012, 2018-2019 Joel K. Pettersson
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
#include "common.h"

/**
 * Pointer list type using an array with resizing.
 *
 * A soft copy (SAU_PtrList_soft_copy()) references
 * the original underlying array instead of duplicating
 * it, unless/until added to.
 */
typedef struct SAU_PtrList {
	const void **items;
	size_t count;
	size_t old_count;
	size_t asize;
} SAU_PtrList;

/**
 * Get the underlying array holding items.
 *
 * The array pointer is used in place of an array if at most
 * 1 item is held.
 */
#define SAU_PtrList_ITEMS(o) \
	((o)->count > 1 ? \
		(o)->items : \
		((const void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SAU_PtrList_GET(o, i) \
	((const void*) SAU_PtrList_ITEMS(o)[i])

bool SAU_PtrList_add(SAU_PtrList *restrict o, const void *restrict item);
void SAU_PtrList_clear(SAU_PtrList *restrict o);
bool SAU_PtrList_memdup(SAU_PtrList *restrict o, const void ***restrict dst);
void SAU_PtrList_soft_copy(SAU_PtrList *restrict dst,
		const SAU_PtrList *restrict src);
