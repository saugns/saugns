/* saugns: Generic array module.
 * Copyright (c) 2018-2019 Joel K. Pettersson
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

#include "arrtype.h"
#include <stdlib.h>
#include <string.h>

/**
 * Add an item to the given array. If \p item is not null,
 * it will be copied (otherwise, the array is simply extended).
 *
 * If allocation fails, the array will remain unaltered.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return true if successful, false if allocation failed
 */
bool SAU_ArrType_add(void *restrict _o,
		const void *restrict item, size_t item_size) {
	SAU_ByteArr *restrict o = _o;
	if (!SAU_ArrType_upsize(o, o->count + 1, item_size)) {
		return false;
	}
	if (item) {
		size_t offs = o->count * item_size;
		memcpy(o->a + offs, item, item_size);
	}
	++o->count;
	return true;
}

/**
 * Resize the given array if \p count is greater than the current
 * allocation.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return true unless allocation failed
 */
bool SAU_ArrType_upsize(void *restrict _o,
		size_t count, size_t item_size) {
	SAU_ByteArr *restrict o = _o;
	size_t asize = o->asize;
	if (!o->a) asize = 0;
	size_t min_asize = count * item_size;
	if (asize < min_asize) {
		size_t old_asize = asize;
		if (!asize) asize = item_size;
		while (asize < min_asize) asize <<= 1;
		void *a = realloc(o->a, asize);
		if (!a) {
			return false;
		}
		o->a = a;
		o->asize = asize;
		memset(a + old_asize, 0, asize - old_asize);
	}
	return true;
}

/**
 * Clear the given array.
 *
 * (Generic version of the function, to be used through wrapper.)
 */
void SAU_ArrType_clear(void *restrict _o) {
	SAU_ByteArr *restrict o = _o;
	if (o->a) {
		free(o->a);
		o->a = NULL;
	}
	o->count = 0;
	o->asize = 0;
}

/**
 * Memdup function for the contents of the given array.
 *
 * \p dst will be set to point to the new allocation
 * (or to NULL if the array was empty). If the array was
 * non-empty and allocation failed, \p will remain
 * unaltered.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return true unless allocation failed
 */
bool SAU_ArrType_memdup(void *restrict _o,
		const void **restrict dst, size_t item_size) {
	SAU_ByteArr *restrict o = _o;
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	void *a = SAU_memdup(o->a, size);
	if (!a) {
		return false;
	}
	*dst = a;
	return true;
}
