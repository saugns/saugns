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

#pragma once
#include "common.h"
#ifndef sauAnyArr
# define sauAnyArr void // for use outside implementation
#endif

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
 * The Name_*() methods defined are inline functions including
 * wrappers around the generic arrtype functions, and more. If
 * not blank, \p MethodPrefix will be used to prefix their names.
 */
#define sauArrTypeMethods(Name, ElementType, MethodPrefix) \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_add(Name *restrict o) { \
	return sauArrType_add(o, sizeof(ElementType)); \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_push(Name *restrict o, \
		const ElementType *restrict item) { \
	return sauArrType_push(o, item, sizeof(ElementType)); \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_pop(Name *restrict o) { \
	return (o->count > 0) ? &o->a[--o->count] : NULL; \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_ins(Name *restrict o, size_t i, \
		const ElementType *restrict item) { \
	return sauArrType_ins(o, i, item, sizeof(ElementType)); \
} \
static inline void sauMaybeUnused \
MethodPrefix##Name##_rem(Name *restrict o, size_t i) { \
	sauArrType_rem(o, i, sizeof(ElementType)); \
} \
static inline bool sauMaybeUnused \
MethodPrefix##Name##_upsize(Name *restrict o, size_t count) { \
	return sauArrType_upsize(o, count, sizeof(ElementType)); \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_get(Name *restrict o, size_t i) { \
	return (o->count > i) ? &o->a[i] : NULL; \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_getrev(Name *restrict o, size_t i) { \
	return (o->count > i) ? &o->a[o->count - (i+1)] : NULL; \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_tip(Name *restrict o) { \
	return (o->count > 0) ? &o->a[o->count - 1] : NULL; \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_clone(Name *restrict o, const Name *restrict src_o) { \
	return sauArrType_copy(o, src_o->a, src_o->count, \
			sizeof(ElementType)); \
} \
static inline ElementType* sauMaybeUnused \
MethodPrefix##Name##_copy(Name *restrict o, const void *restrict a, \
		size_t count) { \
	return sauArrType_copy(o, a, count, sizeof(ElementType)); \
} \
static inline void sauMaybeUnused \
MethodPrefix##Name##_clear(Name *restrict o) { \
	sauArrType_clear(o); \
} \
static inline bool sauMaybeUnused \
MethodPrefix##Name##_memdup(Name *restrict o, ElementType **restrict dst) { \
	return sauArrType_memdup(o, (void**) dst, sizeof(ElementType)); \
} \
static inline bool sauMaybeUnused \
MethodPrefix##Name##_mpmemdup(Name *restrict o, ElementType **restrict dst, \
		struct sauMempool *restrict mempool) { \
	return sauArrType_mpmemdup(o, (void**) dst, sizeof(ElementType), \
			mempool); \
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

/*
 * Arrays of primitive types.
 */
sauArrTypeStruct(sauByteArr, uint8_t) // methods after; this may be sauAnyArr

/*
 * Generic methods.
 */
void *sauArrType_add(sauAnyArr *restrict o, size_t item_size);
void *sauArrType_push(sauAnyArr *restrict o,
		const void *restrict item, size_t item_size);
void *sauArrType_ins(sauAnyArr *restrict o, size_t i,
		const void *restrict item, size_t item_size);
void sauArrType_rem(sauAnyArr *restrict o, size_t i, size_t item_size);
bool sauArrType_upsize(sauAnyArr *restrict o, size_t count, size_t item_size);
void *sauArrType_copy(sauAnyArr *restrict o, const void *restrict a,
		size_t count, size_t item_size);
void sauArrType_clear(sauAnyArr *restrict o);
bool sauArrType_memdup(sauAnyArr *restrict o,
		void **restrict dst, size_t item_size);
bool sauArrType_mpmemdup(sauAnyArr *restrict o,
		void **restrict dst, size_t item_size,
		struct sauMempool *restrict mempool);

sauArrTypeMethods(sauByteArr, uint8_t, )
