/* SAU library: Memory pool module.
 * Copyright (c) 2014, 2018-2024 Joel K. Pettersson
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

struct sauMempool;
typedef struct sauMempool sauMempool;

sauMempool *sau_create_Mempool(size_t start_size) sauMalloclike;
void sau_destroy_Mempool(sauMempool *restrict o);

void *sau_mpalloc(sauMempool *restrict o, size_t size) sauMalloclike;
void *sau_mpmemdup(sauMempool *restrict o,
		const void *restrict src, size_t size) sauMalloclike;
typedef void (*sauDtor_f)(void *o);
bool sau_mpregdtor(sauMempool *restrict o,
		sauDtor_f func, void *restrict arg);
