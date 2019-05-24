/* sgensys: Generic array module.
 * Copyright (c) 2018-2020 Joel K. Pettersson
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
 * Add an item to the given array. The memory is initialized
 * with a copy of \p item if not NULL, otherwise zero bytes.
 *
 * The address of the item in the array is returned, and can
 * be used to initialize it. (If allocation fails, the array
 * will remain unaltered and NULL be returned.) This address
 * should however be expected to change with array resizing.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return item in array, or NULL if allocation failed
 */
void *SGS_ArrType_add(void *restrict _o,
		const void *restrict item, size_t item_size) {
	SGS_ByteArr *restrict o = _o;
	if (!SGS_ArrType_upsize(o, o->count + 1, item_size))
		return NULL;
	size_t offs = o->count * item_size;
	void *mem = o->a + offs;
	if (item != NULL)
		memcpy(mem, item, item_size);
	else
		memset(mem, 0, item_size);
	++o->count;
	return mem;
}

/**
 * Resize the given array if \p count is greater than the current
 * allocation.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return true unless allocation failed
 */
bool SGS_ArrType_upsize(void *restrict _o,
		size_t count, size_t item_size) {
	SGS_ByteArr *restrict o = _o;
	size_t asize = o->asize;
	if (!o->a) asize = 0;
	size_t min_asize = count * item_size;
	if (asize < min_asize) {
		if (!asize) asize = item_size;
		while (asize < min_asize) asize <<= 1;
		void *a = realloc(o->a, asize);
		if (!a)
			return false;
		o->a = a;
		o->asize = asize;
	}
	return true;
}

/**
 * Clear the given array.
 *
 * (Generic version of the function, to be used through wrapper.)
 */
void SGS_ArrType_clear(void *restrict _o) {
	SGS_ByteArr *restrict o = _o;
	if (o->a != NULL) {
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
bool SGS_ArrType_memdup(void *restrict _o,
		const void **restrict dst, size_t item_size) {
	SGS_ByteArr *restrict o = _o;
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	void *a = SGS_memdup(o->a, size);
	if (!a)
		return false;
	*dst = a;
	return true;
}
