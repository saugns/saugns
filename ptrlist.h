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
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "common.h"

/**
 * Pointer list type using an array with resizing.
 *
 * A soft copy (SGS_PtrList_soft_copy()) references
 * the original underlying array instead of duplicating
 * it, unless/until added to.
 */
typedef struct SGS_PtrList {
	const void **items;
	size_t count;
	size_t old_count;
	size_t asize;
} SGS_PtrList;

/**
 * Get the underlying array holding items.
 *
 * The array pointer is used in place of an array if at most
 * 1 item is held.
 */
#define SGS_PtrList_ITEMS(o) \
	((o)->count > 1 ? \
		(o)->items : \
		((const void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SGS_PtrList_GET(o, i) \
	((const void*) SGS_PtrList_ITEMS(o)[i])

bool SGS_PtrList_add(SGS_PtrList *restrict o, const void *restrict item);
void SGS_PtrList_clear(SGS_PtrList *restrict o);
bool SGS_PtrList_memdup(SGS_PtrList *restrict o, const void ***restrict dst);
void SGS_PtrList_soft_copy(SGS_PtrList *restrict dst,
		const SGS_PtrList *restrict src);
