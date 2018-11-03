/* sgensys: Memory pool module.
 * Copyright (c) 2014, 2018 Joel K. Pettersson
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

struct SGS_MemPool;
typedef struct SGS_MemPool SGS_MemPool;

SGS_MemPool *SGS_create_MemPool(size_t block_size) SGS__malloclike;
void SGS_destroy_MemPool(SGS_MemPool *restrict o);

void *SGS_MemPool_alloc(SGS_MemPool *restrict o, size_t size) SGS__malloclike;
void *SGS_MemPool_memdup(SGS_MemPool *restrict o,
		const void *restrict src, size_t size) SGS__malloclike;
