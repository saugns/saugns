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

#pragma once
#include "sgensys.h"

/*
 * Generic array meta-type. A given type is used for the elements,
 * macros being used to declare and define the concrete type.
 *
 * Each concrete type uses inline wrappers around generic methods.
 */

/**
 * Declare array type using \p Name, with \p ElementType.
 */
#define SGS_GArr_DEF_TYPE(Name, ElementType) \
typedef struct Name { \
	ElementType *a; \
	size_t count; \
	size_t asize; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define SGS_GArr_DEF_METHODS(Name, ElementType, MethodPrefix) \
static inline bool MethodPrefix##Name##_add(Name *o, \
		const ElementType *item) { \
	return SGS_GArr_gadd(o, item, sizeof(ElementType)); \
} \
static inline bool MethodPrefix##Name##_upsize(Name *o, size_t count) { \
	return SGS_GArr_gupsize(o, count, sizeof(ElementType)); \
} \
static inline void MethodPrefix##Name##_clear(Name *o) { \
	SGS_GArr_gclear(o); \
} \
static inline bool MethodPrefix##Name##_dupa(Name *o, \
		const ElementType **dst) { \
	return SGS_GArr_gdupa(o, (const void**) dst, sizeof(ElementType)); \
}

/**
 * Declare both type and methods for \p Name, with \p ElementType.
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define SGS_GArr_DEF(Name, ElementType, MethodPrefix) \
SGS_GArr_DEF_TYPE(Name, ElementType) \
SGS_GArr_DEF_METHODS(Name, ElementType, MethodPrefix)

bool SGS_GArr_gadd(void *o, const void *item, size_t item_size);
bool SGS_GArr_gupsize(void *o, size_t count, size_t item_size);
void SGS_GArr_gclear(void *o);
bool SGS_GArr_gdupa(void *o, const void **dst, size_t item_size);

/** uint8_t array type. */
SGS_GArr_DEF(SGS_UInt8Arr, uint8_t, );
