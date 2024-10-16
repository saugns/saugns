/* SAU library: Generic array module.
 * Copyright (c) 2018-2024 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sau/arrtype.h>
#include <sau/mempool.h>
#include <stdlib.h>
#include <string.h>

/**
 * Add an item to the given array. Its memory is initialized
 * to zero bytes only if allocating a new portion of memory.
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
void *sauArrType_add(void *restrict _o, size_t item_size) {
	sauByteArr *restrict o = _o;
	if (!sauArrType_upsize(o, o->count + 1, item_size))
		return NULL;
	size_t offs = o->count * item_size;
	void *mem = o->a + offs;
	++o->count;
	return mem;
}

/**
 * Add an item to the given array. It is always initialized,
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
void *sauArrType_push(void *restrict _o,
		const void *restrict item, size_t item_size) {
	sauByteArr *restrict o = _o;
	if (!sauArrType_upsize(o, o->count + 1, item_size))
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
 * allocation. Initializes a new part of the array to zero bytes.
 *
 * (Generic version of the function, to be used through wrapper.)
 *
 * \return true unless allocation failed
 */
bool sauArrType_upsize(void *restrict _o,
		size_t count, size_t item_size) {
	sauByteArr *restrict o = _o;
	size_t asize = o->asize;
	size_t min_asize = count * item_size;
	if (!o->a || asize < min_asize) {
		if (!asize) asize = item_size;
		while (asize < min_asize) asize <<= 1;
		void *a = realloc(o->a, asize);
		if (!a)
			return false;
		if (!o->a)
			memset(a, 0, asize);
		else
			memset(a + o->asize, 0, asize - o->asize);
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
void sauArrType_clear(void *restrict _o) {
	sauByteArr *restrict o = _o;
	free(o->a);
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
bool sauArrType_memdup(void *restrict _o,
		void **restrict dst, size_t item_size) {
	sauByteArr *restrict o = _o;
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	void *a = malloc(size);
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
bool sauArrType_mpmemdup(void *restrict _o,
		void **restrict dst, size_t item_size,
		sauMempool *restrict mempool) {
	sauByteArr *restrict o = _o;
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	void *a = sau_mpmemdup(mempool, o->a, size);
	if (!a)
		return false;
	*dst = a;
	return true;
}
