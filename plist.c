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

#include "plist.h"
#include <stdlib.h>
#include <string.h>

/**
 * Add a pointer to the given list.
 *
 * If allocation fails, the list will remain unaltered.
 *
 * \return true unless allocation failed
 */
bool SGS_PList_add(SGS_PList *o, const void *item) {
	if (!o->asize) {
		if (o->count == 0) {
			o->items = (const void**) item;
			o->count = 1;
		} else {
			const size_t asize = 2 * sizeof(const void*);
			const void **a = malloc(asize);
			if (!a) {
				return false;
			}
			a[0] = (const void*) o->items;
			a[1] = item;
			o->items = a;
			o->count = 2;
			o->asize = asize;
		}
		return true;
	}

	size_t isize = o->count * sizeof(const void*);
	size_t asize = o->asize;
	if (o->count == o->old_count) {
		if (asize == isize) asize <<= 1;
		const void **a = malloc(asize);
		if (!a) {
			return false;
		}
		memcpy(a, o->items, isize);
		o->items = a;
		o->asize = asize;
	} else if (asize == isize) {
		asize <<= 1;
		const void **a = realloc(o->items, asize);
		if (!a) {
			return false;
		}
		o->items = a;
		o->asize = asize;
	}
	o->items[o->count] = item;
	++o->count;
	return true;
}

/**
 * Clear the given list.
 */
void SGS_PList_clear(SGS_PList *o) {
	if (o->count > o->old_count && o->asize > 0) {
		free(o->items);
	}
	o->items = 0;
	o->count = 0;
	o->old_count = 0;
	o->asize = 0;
}

/**
 * Memdup function for the contents of the given list.
 *
 * \p dst will be set to point to the new allocation
 * (or to NULL if the list was empty). If the list was
 * non-empty and allocation failed, \p dst will remain
 * unaltered.
 *
 * \return true unless allocation failed
 */
bool SGS_PList_memdup(SGS_PList *o, const void ***dst) {
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * sizeof(const void*);
	const void **src = SGS_PList_ITEMS(o);
	const void **a = SGS_memdup(src, size);
	if (!a) {
		return false;
	}
	*dst = a;
	return true;
}

/**
 * Copy the list src to dst (clearing dst first if needed); to save
 * memory, dst will actually merely reference the data in src
 * unless/until added to.
 *
 * old_count will be set to the count of src, so that iteration
 * beginning at that value will ignore copied entries.
 */
void SGS_PList_copy(SGS_PList *dst, const SGS_PList *src) {
	SGS_PList_clear(dst);
	dst->items = src->items;
	dst->count = src->count;
	dst->old_count = src->count;
	dst->asize = src->asize;
}
