/* sgensys: Generic array module.
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
#include "sgensys.h"

/*
 * Generic array meta-type. A given type is used for the elements,
 * macros being used to declare and define the concrete type.
 *
 * Each concrete type uses inline wrappers around generic methods.
 */

struct SGS_Mempool;

/**
 * Declare array type using \p Name, with \p ElementType.
 *
 * Only declares type, not methods. See sgsArrType().
 */
#define sgsArrTypeStruct(Name, ElementType) \
typedef struct Name { \
	ElementType *a; \
	size_t count; \
	size_t asize; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * Only declares methods, not type. See sgsArrType().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define sgsArrTypeMethods(Name, ElementType, MethodPrefix) \
static inline ElementType SGS__maybe_unused \
*MethodPrefix##Name##_add(Name *o, \
		const ElementType *item) { \
	return SGS_ArrType_add(o, item, sizeof(ElementType)); \
} \
static inline void SGS__maybe_unused \
MethodPrefix##Name##_clear(Name *o) { \
	SGS_ArrType_clear(o); \
} \
static inline bool SGS__maybe_unused \
MethodPrefix##Name##_memdup(Name *o, \
		ElementType **dst) { \
	return SGS_ArrType_memdup(o, (void**) dst, sizeof(ElementType)); \
} \
static inline bool SGS__maybe_unused \
MethodPrefix##Name##_mpmemdup(Name *o, \
		ElementType **dst, \
		struct SGS_Mempool *mempool) { \
	return SGS_ArrType_mpmemdup(o, (void**) dst, \
		sizeof(ElementType), mempool); \
}

/**
 * Declare both type and methods for \p Name, with \p ElementType.
 *
 * Combines sgsArrTypeStruct() and sgsArrTypeMethods().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define sgsArrType(Name, ElementType, MethodPrefix) \
sgsArrTypeStruct(Name, ElementType) \
sgsArrTypeMethods(Name, ElementType, MethodPrefix)

void *SGS_ArrType_add(void *o,
		const void *item, size_t item_size);
void SGS_ArrType_clear(void *o);
bool SGS_ArrType_memdup(void *o,
		void **dst, size_t item_size);
bool SGS_ArrType_mpmemdup(void *o,
		void **dst, size_t item_size,
		struct SGS_Mempool *mempool);

/** Byte (uint8_t) array type. */
sgsArrType(SGS_ByteArr, uint8_t, )
