/* mgensys: Generic array module.
 * Copyright (c) 2018-2022 Joel K. Pettersson
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

#pragma once
#include "common.h"

/*
 * Generic array meta-type. A given type is used for the elements,
 * macros being used to declare and define the concrete type.
 *
 * Each concrete type uses inline wrappers around generic methods.
 */

struct mgsMemPool;

/**
 * Declare array type using \p Name, with \p ElementType.
 *
 * Only declares type, not methods. See mgsArrType().
 */
#define mgsArrTypeStruct(Name, ElementType) \
typedef struct Name { \
	ElementType *a; \
	size_t count; \
	size_t asize; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * Only declares methods, not type. See mgsArrType().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define mgsArrTypeMethods(Name, ElementType, MethodPrefix) \
static inline ElementType mgsMaybeUnused \
*MethodPrefix##Name##_add(Name *restrict o, \
		const ElementType *restrict item) { \
	return mgsArrType_add(o, item, sizeof(ElementType)); \
} \
static inline void mgsMaybeUnused \
MethodPrefix##Name##_clear(Name *restrict o) { \
	mgsArrType_clear(o); \
} \
static inline bool mgsMaybeUnused \
MethodPrefix##Name##_memdup(Name *restrict o, \
		ElementType **restrict dst) { \
	return mgsArrType_memdup(o, (void**) dst, sizeof(ElementType)); \
} \
static inline bool mgsMaybeUnused \
MethodPrefix##Name##_mpmemdup(Name *restrict o, \
		ElementType **restrict dst, \
		struct mgsMemPool *restrict mempool) { \
	return mgsArrType_mpmemdup(o, (void**) dst, \
		sizeof(ElementType), mempool); \
}

/**
 * Declare both type and methods for \p Name, with \p ElementType.
 *
 * Combines mgsArrTypeStruct() and mgsArrTypeMethods().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define mgsArrType(Name, ElementType, MethodPrefix) \
mgsArrTypeStruct(Name, ElementType) \
mgsArrTypeMethods(Name, ElementType, MethodPrefix)

void *mgsArrType_add(void *restrict o,
		const void *restrict item, size_t item_size);
void mgsArrType_clear(void *restrict o);
bool mgsArrType_memdup(void *restrict o,
		void **restrict dst, size_t item_size);
bool mgsArrType_mpmemdup(void *restrict o,
		void **restrict dst, size_t item_size,
		struct mgsMemPool *mempool);

/** Byte (uint8_t) array type. */
mgsArrType(mgsByteArr, uint8_t, )
