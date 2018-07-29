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
 * <http://www.gnu.org/licenses/>.
 */

#include "garr.h"
#include <stdlib.h>
#include <string.h>

/**
 * Add an item to the given array.
 *
 * If allocation fails, the array will remain unaltered.
 *
 * \return true if successful, false if allocation failed
 */
bool SGS_GArr_generic_add(void *_o, const void *item, size_t item_size) {
	SGS_UInt8Arr *o = _o;
	uint8_t *a = o->a;
	size_t i = o->count;
	size_t alloc = o->alloc;
	size_t offs = i * item_size;
	if (!o->a || i == alloc) {
		size_t size;
		alloc = (!alloc) ? 1 : alloc << 1;
		size = alloc * item_size;
		a = realloc(o->a, size);
		if (!a) return false;
		o->a = a;
		o->alloc = alloc;
		memset(a + offs, 0, size - offs);
	}
	memcpy(a + offs, item, item_size);
	++o->count;
	return true;
}

/**
 * Clear the given array.
 */
void SGS_GArr_generic_clear(void *_o) {
	SGS_UInt8Arr *o = _o;
	if (o->a) {
		free(o->a);
		o->a = NULL;
	}
	o->count = 0;
	o->alloc = 0;
}

/**
 * Memdup function for the allocation of the given array.
 * \p dst will be set to point to the new allocation on success,
 * but remain unaltered on failure.
 *
 * \return true if successful, false on failure or no allocation
 */
bool SGS_GArr_generic_dupa(void *_o, void **dst, size_t item_size) {
	SGS_UInt8Arr *o = _o;
	size_t size = o->count * item_size;
	if (!o->a || !size) return false;
	void *a = malloc(size);
	if (!a) return false;
	memcpy(a, o->a, size);
	*dst = a;
	return true;
}

/*
 * Simple test.
 */
#if 0
typedef struct Test {
	uint32_t i;
} Test;

SGS_GArr_DEF(SGS_TestArr, Test);
#endif
