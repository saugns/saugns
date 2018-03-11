/* sgensys: Pointer list module.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
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
#include "sgensys.h"

/**
 * Pointer list type using an array with resizing.
 *
 * A soft copy (SGSPtrList_soft_copy()) references
 * the original underlying array instead of duplicating
 * it, unless/until added to.
 */
typedef struct SGSPtrList {
	const void **items;
	size_t count;
	size_t old_count;
	size_t asize;
} SGSPtrList;

/**
 * Get the underlying array holding items.
 *
 * The array pointer is used in place of an array if at most
 * 1 item is held.
 */
#define SGSPtrList_ITEMS(o) \
	((o)->count > 1 ? \
		(o)->items : \
		((const void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SGSPtrList_GET(o, i) \
	((const void*) SGSPtrList_ITEMS(o)[i])

bool SGSPtrList_add(SGSPtrList *o, const void *item);
void SGSPtrList_clear(SGSPtrList *o);
bool SGSPtrList_memdup(SGSPtrList *o, const void ***dst);
void SGSPtrList_soft_copy(SGSPtrList *dst, const SGSPtrList *src);
