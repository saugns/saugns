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
	size_t alloc; \
} Name;

/**
 * Declare array methods for \p Name, with \p ElementType.
 *
 * The methods are inline wrappers around generic methods.
 */
#define SGS_GArr_DEF_METHODS(Name, ElementType) \
static inline bool Name##_add(Name *o, ElementType *item) { \
	return SGS_GArr_generic_add(o, item, sizeof(ElementType)); \
} \
static inline void Name##_clear(Name *o) { \
	SGS_GArr_generic_clear(o); \
} \
static inline bool Name##_dupa(Name *o, ElementType **dst) { \
	return SGS_GArr_generic_dupa(o, (void**) dst, sizeof(ElementType)); \
}

#define SGS_GArr_DEF(Name, ElementType) \
SGS_GArr_DEF_TYPE(Name, ElementType) \
SGS_GArr_DEF_METHODS(Name, ElementType)

bool SGS_GArr_generic_add(void *o, const void *item, size_t item_size);
void SGS_GArr_generic_clear(void *o);
bool SGS_GArr_generic_dupa(void *o, void **dst, size_t item_size);

/** uint8_t array type. */
SGS_GArr_DEF(SGS_UInt8Arr, uint8_t);
