/* sgensys: Generic array module.
 * Copyright (c) 2018 Joel K. Pettersson
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

#include "garr.h"
#include <stdlib.h>
#include <string.h>

/**
 * Add an item to the given array. If \p item is not null,
 * it will be copied (otherwise, the array is simply extended).
 *
 * If allocation fails, the array will remain unaltered.
 *
 * \return true if successful, false if allocation failed
 */
bool SGS_GArr_gadd(void *_o, const void *item, size_t item_size) {
	SGS_UInt8Arr *o = _o;
	if (!SGS_GArr_gupsize(o, o->count + 1, item_size)) {
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
 * \return true unless allocation failed
 */
bool SGS_GArr_gupsize(void *_o, size_t count, size_t item_size) {
	SGS_UInt8Arr *o = _o;
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
 */
void SGS_GArr_gclear(void *_o) {
	SGS_UInt8Arr *o = _o;
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
 * \return true unless allocation failed
 */
bool SGS_GArr_gdupa(void *_o, const void **dst, size_t item_size) {
	SGS_UInt8Arr *o = _o;
	if (!o->count) {
		*dst = NULL;
		return true;
	}
	size_t size = o->count * item_size;
	void *a = malloc(size);
	if (!a) {
		return false;
	}
	memcpy(a, o->a, size);
	*dst = a;
	return true;
}
