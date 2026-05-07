/* SAU library: Generic array module.
 * Copyright (c) 2018-2026 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#define sauAnyArr sauByteArr // for implementation
#include "arrtype.h"
#include "mempool.h"
#include "math.h"
#include <stdlib.h>
#include <string.h>

/**
 * Append an item to the given array. Its memory is initialized
 * to zero bytes only if allocating a new portion of memory.
 *
 * The address of the item in the array is returned. (If allocation
 * fails, the array will remain unaltered and NULL be returned.)
 * This address should be expected to change with array resizing.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return item in array, or NULL if allocation failed
 */
void *sauArrType_add(sauAnyArr *restrict o, size_t item_size) {
	if (!sauArrType_upsize(o, o->count + 1, item_size))
		return NULL;
	size_t offs = o->count * item_size;
	uint8_t *mem = o->a + offs;
	++o->count;
	return mem;
}

/**
 * Append an item to the given array. It is always initialized,
 * with a copy of \p item if not NULL, otherwise zero bytes.
 *
 * The address of the item in the array is returned. (If allocation
 * fails, the array will remain unaltered and NULL be returned.)
 * This address should be expected to change with array resizing.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return item in array, or NULL if allocation failed
 */
void *sauArrType_push(sauAnyArr *restrict o,
		const void *restrict item, size_t item_size) {
	if (!sauArrType_upsize(o, o->count + 1, item_size))
		return NULL;
	size_t offs = o->count * item_size;
	uint8_t *mem = o->a + offs;
	if (item != NULL)
		memcpy(mem, item, item_size);
	else
		memset(mem, 0, item_size);
	++o->count;
	return mem;
}

/**
 * Insert item at position within the array, moving contents as needed.
 * The item is always initialized, with a copy of \p item if not NULL,
 * otherwise zero bytes. (If \p i is beyond array item count, enlarges
 * the array to fit.)
 *
 * The address of the item in the array is returned. (If allocation
 * fails, the array will remain unaltered and NULL be returned.)
 * This address should be expected to change with array resizing.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return item in array, or NULL if allocation failed
 */
void *sauArrType_ins(sauAnyArr *restrict o, size_t i,
		const void *restrict item, size_t item_size) {
	size_t count = (i > o->count) ? (i + 1) : (o->count + 1);
	if (!sauArrType_upsize(o, count, item_size))
		return NULL;
	size_t offs = i * item_size;
	uint8_t *mem = o->a + offs;
	if (i < o->count)
		memmove(mem + item_size, mem, o->asize - (offs + item_size));
	if (item != NULL)
		memcpy(mem, item, item_size);
	else
		memset(mem, 0, item_size);
	o->count = count;
	return mem;
}

/**
 * Remove an item within the given array, moving contents as needed.
 * (Does nothing if \p i is out of bounds.)
 *
 * (Generic version of the function, to be used through wrapper.)
 */
void sauArrType_rem(sauAnyArr *restrict o, size_t i, size_t item_size) {
	if (i >= o->count)
		return; // out of bounds
	if (i == o->count - 1) {
		sauByteArr_pop(o);
		return;
	}
	size_t offs = i * item_size;
	uint8_t *mem = o->a + offs;
	memmove(mem, mem + item_size, o->asize - (offs + item_size));
	--o->count;
}

/**
 * Resize the given array if \p count is greater than the current
 * allocation. Picks next power of two multiple of the item size.
 * Can clone, can initialize new part of the array to zero bytes.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return true unless allocation failed
 */
bool sauArrType_upsize(sauAnyArr *restrict o, size_t count, size_t item_size) {
	size_t asize = o->asize;
	size_t min_asize = count * item_size;
	if (count > 0 && (!o->a || asize < min_asize)) {
		uint8_t *a;
		if (asize < min_asize)
			asize = sau_zirupo2(count) * item_size;
		if (!o->asize && o->a) { // clone borrowed allocation
			size_t copy_asize = o->count * item_size;
			if (!(a = malloc(asize)))
				return false;
			memcpy(a, o->a, copy_asize);
			memset(a + copy_asize, 0, asize - copy_asize);
		} else {
			if (!(a = realloc(o->a, asize)))
				return false;
			if (!o->a)
				memset(a, 0, asize);
			else
				memset(a + o->asize, 0, asize - o->asize);
		}
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
void sauArrType_clear(sauAnyArr *restrict o) {
	if (o->asize) free(o->a); // don't free on borrowed allocation
	o->a = NULL;
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
bool sauArrType_memdup(sauAnyArr *restrict o,
		void **restrict dst, size_t item_size) {
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	uint8_t *a = malloc(size);
	if (!a)
		return false;
	memcpy(a, o->a, size);
	*dst = a;
	return true;
}

/**
 * Mempool-using variant of the
 * memdup function for the contents of the given array.
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
bool sauArrType_mpmemdup(sauAnyArr *restrict o,
		void **restrict dst, size_t item_size,
		sauMempool *restrict mempool) {
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	uint8_t *a = sau_mpmemdup(mempool, o->a, size);
	if (!a)
		return false;
	*dst = a;
	return true;
}
