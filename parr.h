/* sgensys: Pointer array module.
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
 * Pointer array type.
 */
struct SGS_PArr {
	size_t count;
	size_t copy_count;
	const void **items;
	size_t alloc;
};
typedef struct SGS_PArr SGS_PArr;

/**
 * Get array of items.
 *
 * The array pointer is used in place of an array if no more
 * than 1 item has been added.
 */
#define SGS_PArr_ITEMS(ar) \
	((const void**) ((ar)->count > 1 ? \
		(ar)->items : \
		((const void**) &(ar)->items)))

/**
 * Get the item \p i.
 */
#define SGS_PArr_GET(ar, i) \
	((const void*) SGS_PArr_ITEMS(ar)[i])

bool SGS_PArr_add(SGS_PArr *ar, const void *item);
void SGS_PArr_clear(SGS_PArr *ar);
void SGS_PArr_copy(SGS_PArr *dst, const SGS_PArr *src);
