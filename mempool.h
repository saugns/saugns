/* sgensys: Memory pool module.
 * Copyright (c) 2014, 2018-2020 Joel K. Pettersson
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

struct SGS_MemPool;
typedef struct SGS_MemPool SGS_MemPool;

SGS_MemPool *SGS_create_MemPool(size_t block_size) SGS__malloclike;
void SGS_destroy_MemPool(SGS_MemPool *restrict o);

void *SGS_MemPool_alloc(SGS_MemPool *restrict o, size_t size) SGS__malloclike;
void *SGS_MemPool_memdup(SGS_MemPool *restrict o,
		const void *restrict src, size_t size) SGS__malloclike;
