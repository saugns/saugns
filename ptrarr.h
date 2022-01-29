/* sgensys: Pointer array module.
 * Copyright (c) 2011-2012, 2018-2021 Joel K. Pettersson
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

/**
 * Dynamically sized pointer array. Only supports appending.
 *
 * A soft copy (SGS_PtrArr_soft_copy()) references
 * the original items instead of duplicating them,
 * unless/until the array is added to.
 */
typedef struct SGS_PtrArr {
	void **items;
	size_t count;
	size_t old_count;
	size_t asize;
} SGS_PtrArr;

/**
 * Get the underlying array holding items.
 *
 * The array pointer is used in place of an allocation
 * if only 1 item is held.
 */
#define SGS_PtrArr_ITEMS(o) \
	((o)->asize > 1 ? \
		((void**) (o)->items) : \
		((void**) &(o)->items))

/**
 * Get the item \p i.
 */
#define SGS_PtrArr_GET(o, i) \
	((void*) SGS_PtrArr_ITEMS(o)[i])

struct SGS_MemPool;

bool SGS_PtrArr_add(SGS_PtrArr *restrict o, void *restrict item);
void SGS_PtrArr_clear(SGS_PtrArr *restrict o);
bool SGS_PtrArr_memdup(SGS_PtrArr *restrict o, void ***restrict dst);
bool SGS_PtrArr_mpmemdup(SGS_PtrArr *restrict o, void ***restrict dst,
		struct SGS_MemPool *restrict mempool);
void SGS_PtrArr_soft_copy(SGS_PtrArr *restrict dst,
		const SGS_PtrArr *restrict src);
