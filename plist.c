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
 * \return true if successful, false if allocation failed
 */
bool SGS_PList_add(SGS_PList *o, const void *item) {
	if (!o->alloc) {
		if (o->count == 0) {
			o->count = 1;
			o->items = (const void**) item;
		} else {
			const void **items;
			items = malloc(sizeof(const void*) * 2);
			if (!items) {
				return false;
			}
			items[0] = (const void*) o->items;
			items[1] = item;
			o->count = 2;
			o->alloc = 2;
			o->items = items;
		}
		return true;
	}

	if (o->count == o->copy_count) {
		const void **items;
		size_t alloc = o->alloc;
		if (o->count == alloc) {
			alloc <<= 1;
		}
		items = malloc(sizeof(const void*) * alloc);
		if (!items) {
			return false;
		}
		memcpy(items, o->items, sizeof(const void*) * o->count);
		o->items = items;
		o->alloc = alloc;
	} else if (o->count == o->alloc) {
		const void **items;
		size_t alloc = o->alloc << 1;
		items = realloc(o->items, sizeof(const void*) * alloc);
		if (!items) {
			return false;
		}
		o->items = items;
		o->alloc = alloc;
	}
	o->items[o->count] = item;
	++o->count;
	return true;
}

/**
 * Clear the given list.
 */
void SGS_PList_clear(SGS_PList *o) {
	if (o->count > o->copy_count && o->alloc > 0) {
		free(o->items);
	}
	o->count = 0;
	o->copy_count = 0;
	o->items = 0;
	o->alloc = 0;
}

/**
 * Copy the list src to dst (clearing dst first if needed); to save
 * memory, dst will actually merely reference the data in src
 * unless/until added to.
 *
 * copy_count will be set to the count of src, so that iteration
 * beginning at that value will ignore copied entries.
 */
void SGS_PList_copy(SGS_PList *dst, const SGS_PList *src) {
	SGS_PList_clear(dst);
	dst->count = src->count;
	dst->copy_count = src->count;
	dst->items = src->items;
	dst->alloc = src->alloc;
}
