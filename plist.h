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
#include "sgensys.h"

/**
 * Pointer list type using an array with resizing. A copy
 * (SGS_plist_copy()) references the copied array instead
 * of duplicating it, until added to.
 */
typedef struct SGSPList {
	size_t count;
	size_t copy_count;
	const void **items;
	size_t alloc;
} SGSPList;

/**
 * Get array holding list of items.
 *
 * The array pointer is used in place of an array if no more
 * than 1 item has been added.
 */
#define SGS_PLIST_ITEMS(o) \
	((o)->count > 1 ? \
		(o)->items : \
		((const void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SGS_PLIST_GET(o, i) \
	((const void*) SGS_PLIST_ITEMS(o)[i])

bool SGS_plist_add(SGSPList *o, const void *item);
void SGS_plist_clear(SGSPList *o);
void SGS_plist_copy(SGSPList *dst, const SGSPList *src);
