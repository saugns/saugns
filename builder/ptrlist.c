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

#include "ptrlist.h"
#include <stdlib.h>
#include <string.h>

/**
 * Add a pointer to the end of the given list.
 *
 * If allocation fails, the list will remain unaltered.
 *
 * \return true unless allocation failed
 */
bool SAU_PtrList_add(SAU_PtrList *restrict o, const void *restrict item) {
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
void SAU_PtrList_clear(SAU_PtrList *restrict o) {
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
bool SAU_PtrList_memdup(SAU_PtrList *restrict o, const void ***restrict dst) {
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * sizeof(const void*);
	const void **src = SAU_PtrList_ITEMS(o);
	const void **a = SAU_memdup(src, size);
	if (!a) {
		return false;
	}
	*dst = a;
	return true;
}

/**
 * Copy the list \p src to \p dst (clearing dst first if needed);
 * to save memory, dst will actually merely reference the data in
 * src unless/until added to. It is assumed that after copying,
 * src will no longer be added to (unless dst is first cleared
 * or further added to).
 *
 * \a old_count will be set to the count of src, so that iteration
 * beginning at that value will ignore copied entries.
 *
 * Regardless of the order of list clearing, once clearing is
 * taking place, by only accessing items (pointer dereferencing)
 * through iteration between \a old_count and \a count, all
 * accessing of freed memory is avoided.
 */
void SAU_PtrList_soft_copy(SAU_PtrList *restrict dst,
		const SAU_PtrList *restrict src) {
	SAU_PtrList_clear(dst);
	dst->items = src->items;
	dst->count = src->count;
	dst->old_count = src->count;
	dst->asize = src->asize;
}
