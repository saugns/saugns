/* ssndgen: Generic array module.
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
#include "common.h"

/*
 * Generic array meta-type. A given type is used for the elements,
 * macros being used to declare and define the concrete type.
 *
 * Each concrete type uses inline wrappers around generic methods.
 */

/**
 * Declare array type using \p Name, with \p ElementType.
 *
 * Only declares type, not methods. See SSG_DEF_ArrType().
 */
#define SSG_DEF_ArrType_TYPE(Name, ElementType) \
typedef struct Name { \
	ElementType *a; \
	size_t count; \
	size_t asize; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * Only declares methods, not type. See SSG_DEF_ArrType().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define SSG_DEF_ArrType_METHODS(Name, ElementType, MethodPrefix) \
static inline bool SSG__maybe_unused \
MethodPrefix##Name##_add(Name *o, const ElementType *item) { \
	return SSG_ArrType_add(o, item, sizeof(ElementType)); \
} \
static inline bool SSG__maybe_unused \
MethodPrefix##Name##_upsize(Name *o, size_t count) { \
	return SSG_ArrType_upsize(o, count, sizeof(ElementType)); \
} \
static inline void SSG__maybe_unused \
MethodPrefix##Name##_clear(Name *o) { \
	SSG_ArrType_clear(o); \
} \
static inline bool SSG__maybe_unused \
MethodPrefix##Name##_memdup(Name *o, const ElementType **dst) { \
	return SSG_ArrType_memdup(o, (const void**) dst, sizeof(ElementType)); \
}

/**
 * Declare both type and methods for \p Name, with \p ElementType.
 *
 * Combines SSG_DEF_ArrType_TYPE() and SSG_DEF_ArrType_METHODS().
 *
 * The Name_*() methods defined are inline wrappers around the
 * generic methods. If not blank, \p MethodPrefix will be used
 * to prefix their names.
 */
#define SSG_DEF_ArrType(Name, ElementType, MethodPrefix) \
SSG_DEF_ArrType_TYPE(Name, ElementType) \
SSG_DEF_ArrType_METHODS(Name, ElementType, MethodPrefix)

bool SSG_ArrType_add(void *o, const void *item, size_t item_size);
bool SSG_ArrType_upsize(void *o, size_t count, size_t item_size);
void SSG_ArrType_clear(void *o);
bool SSG_ArrType_memdup(void *o, const void **dst, size_t item_size);

/** uint8_t array type. */
SSG_DEF_ArrType(SSG_UInt8Arr, uint8_t, );
