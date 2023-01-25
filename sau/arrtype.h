/* SAU library: Generic array module.
 * Copyright (c) 2018-2022 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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

/** Byte (uint8_t) array type. */
sauArrType(sauByteArr, uint8_t, )
