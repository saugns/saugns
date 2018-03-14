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

/*
 * Pointer array type.
 */

/** Pointer array item type. */
typedef const void *SGS_Ptr_t;

struct SGS_PtrArr {
	size_t count;
	size_t copy_count;
	SGS_Ptr_t *items;
	size_t alloc;
};

/**
 * Get array of items.
 *
 * The array pointer is used in place of an array if no more
 * than 1 item has been added.
 */
#define SGS_PTRARR_ITEMS(ar) \
	((SGS_Ptr_t*) ((ar)->count > 1 ? \
		(ar)->items : \
		((SGS_Ptr_t*) &(ar)->items)))

/**
 * Get the item \p i.
 */
#define SGS_PTRARR_GET(ar, i) \
	((SGS_Ptr_t) SGS_PTRARR_ITEMS(ar)[i])

bool SGS_ptrarr_add(struct SGS_PtrArr *ar, SGS_Ptr_t item);
void SGS_ptrarr_clear(struct SGS_PtrArr *ar);
void SGS_ptrarr_copy(struct SGS_PtrArr *dst, const struct SGS_PtrArr *src);
