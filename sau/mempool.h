/* SAU library: Memory pool module.
 * Copyright (c) 2014, 2018-2024 Joel K. Pettersson
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

struct SAU_Mempool;
typedef struct SAU_Mempool SAU_Mempool;

SAU_Mempool *SAU_create_Mempool(size_t start_size) sauMalloclike;
void SAU_destroy_Mempool(SAU_Mempool *restrict o);

void *SAU_mpalloc(SAU_Mempool *restrict o, size_t size) sauMalloclike;
void *SAU_mpmemdup(SAU_Mempool *restrict o,
		const void *restrict src, size_t size) sauMalloclike;
typedef void (*SAU_Dtor_f)(void *o);
bool SAU_mpregdtor(SAU_Mempool *restrict o,
		SAU_Dtor_f func, void *restrict arg);
