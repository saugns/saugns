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
#include "sgensys.h"

struct SGS_Mempool;
typedef struct SGS_Mempool SGS_Mempool;

SGS_Mempool *SGS_create_Mempool(size_t block_size) SGS__malloclike;
void SGS_destroy_Mempool(SGS_Mempool *o);

void *SGS_Mempool_alloc(SGS_Mempool *o, size_t size) SGS__malloclike;
void *SGS_Mempool_memdup(SGS_Mempool *o,
		const void *src, size_t size) SGS__malloclike;
