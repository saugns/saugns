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
 * <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include "ptrarr.h"

/**
 * Add a pointer to the given array.
 *
 * \return true unless allocation fails
 */
bool SGS_ptrarr_add(struct SGSPtrArr *ar, SGSPtr_t item) {
	if (!ar->alloc) {
		if (ar->count == 0) {
			ar->count = 1;
			ar->items = (SGSPtr_t*) item;
		} else {
			SGSPtr_t old_item = (SGSPtr_t) ar->items;
			ar->count = 2;
			ar->alloc = 2;
			ar->items = malloc(sizeof(SGSPtr_t) * ar->alloc);
			if (!ar->items) {
				return false;
			}
			ar->items[0] = old_item;
			ar->items[1] = item;
		}
		return true;
	}

	if (ar->count == ar->copy_count) {
		SGSPtr_t *old_items = ar->items;
		if (ar->count == ar->alloc) {
			ar->alloc <<= 1;
		}
		ar->items = malloc(sizeof(SGSPtr_t) * ar->alloc);
		if (!ar->items) {
			return false;
		}
		memcpy(ar->items, old_items, sizeof(SGSPtr_t) * ar->count);
	} else if (ar->count == ar->alloc) {
		ar->alloc <<= 1;
		ar->items = realloc(ar->items, sizeof(SGSPtr_t) * ar->alloc);
		if (!ar->items) {
			return false;
		}
	}
	ar->items[ar->count] = item;
	++ar->count;
	return true;
}

/**
 * Clear the given array.
 */
void SGS_ptrarr_clear(struct SGSPtrArr *ar) {
	if (ar->count > ar->copy_count && ar->alloc > 0) {
		free(ar->items);
	}
	ar->count = 0;
	ar->copy_count = 0;
	ar->items = 0;
	ar->alloc = 0;
}

/**
 * Copy the array src to dst (clearing dst first if needed); to save
 * memory, dst will actually merely reference the data in src
 * unless/until added to.
 *
 * copy_count will be set to the count of src, so that iteration
 * beginning at that value will ignore copied entries.
 */
void SGS_ptrarr_copy(struct SGSPtrArr *dst, const struct SGSPtrArr *src) {
	SGS_ptrarr_clear(dst);
	dst->count = src->count;
	dst->copy_count = src->count;
	dst->items = src->items;
	dst->alloc = src->alloc;
}
