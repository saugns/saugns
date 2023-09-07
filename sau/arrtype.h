/* SAU library: Generic array module.
 * Copyright (c) 2018-2023 Joel K. Pettersson
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

#pragma once
#include "common.h"

/*
 * Generic array meta-type. A given type is used for the elements,
 * macros being used to declare and define the concrete type.
 *
 * Each concrete type uses inline wrappers around generic methods.
 *
 * Some optional functionality relies on directly setting
 * fields of an instance (of any arrtype type declared):
 *  - Set \a count to zero for a soft clear, to start over
 *    with adding items while keeping the old allocation.
 *  - Set \a asize to a non-zero size in bytes, prior to the
 *    very first allocation, to make that allocation larger.
 *  - Copy \a a from elsewhere and ensure \a asize is zero
 *    and \a count set to the number of items to reuse, to
 *    clone those \a a items upon the first resizing call.
 *    A borrowed allocation will not be freed, a new will.
 *    (To reuse a prior allocation until a new is needed.)
 */

struct sauMempool;

/**
 * Declare array type using \p Name, with \p ElementType.
 *
 * Only declares type, not methods. See sauArrType().
 */
#define sauArrTypeStruct(Name, ElementType) \
typedef struct Name { \
	ElementType *a; \
	size_t count; \
	size_t asize; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * Only declares methods, not type. See sauArrType().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define sauArrTypeMethods(Name, ElementType, MethodPrefix) \
static inline ElementType sauMaybeUnused \
*MethodPrefix##Name##_add(Name *restrict o, \
		const ElementType *restrict item) { \
	return sauArrType_add(o, item, sizeof(ElementType)); \
} \
static inline void sauMaybeUnused \
MethodPrefix##Name##_clear(Name *restrict o) { \
	sauArrType_clear(o); \
} \
static inline bool sauMaybeUnused \
MethodPrefix##Name##_memdup(Name *restrict o, \
		ElementType **restrict dst) { \
	return sauArrType_memdup(o, (void**) dst, sizeof(ElementType)); \
} \
static inline bool sauMaybeUnused \
MethodPrefix##Name##_mpmemdup(Name *restrict o, \
		ElementType **restrict dst, \
		struct sauMempool *restrict mempool) { \
	return sauArrType_mpmemdup(o, (void**) dst, \
		sizeof(ElementType), mempool); \
} \
static inline bool sauMaybeUnused \
MethodPrefix##Name##_upsize(Name *restrict o, size_t count) { \
	return sauArrType_upsize(o, count, sizeof(ElementType)); \
}

/**
 * Declare both type and methods for \p Name, with \p ElementType.
 *
 * Combines sauArrTypeStruct() and sauArrTypeMethods().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define sauArrType(Name, ElementType, MethodPrefix) \
sauArrTypeStruct(Name, ElementType) \
sauArrTypeMethods(Name, ElementType, MethodPrefix)

void *sauArrType_add(void *restrict o,
		const void *restrict item, size_t item_size);
void sauArrType_clear(void *restrict o);
bool sauArrType_memdup(void *restrict o,
		void **restrict dst, size_t item_size);
bool sauArrType_mpmemdup(void *restrict o,
		void **restrict dst, size_t item_size,
		struct sauMempool *mempool);
bool sauArrType_upsize(void *restrict o,
		size_t count, size_t item_size);

/*
 * Arrays of primitive types.
 */
sauArrType(sauByteArr, uint8_t, )
sauArrType(sauUint16Arr, uint16_t, )
sauArrType(sauUint32Arr, uint32_t, )
