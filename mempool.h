/* saugns: Memory pool module.
 * Copyright (c) 2014, 2018-2019 Joel K. Pettersson
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

struct SAU_MemPool;
typedef struct SAU_MemPool SAU_MemPool;

SAU_MemPool *SAU_create_MemPool(size_t block_size) SAU__malloclike;
void SAU_destroy_MemPool(SAU_MemPool *restrict o);

void *SAU_MemPool_alloc(SAU_MemPool *restrict o, size_t size) SAU__malloclike;
void *SAU_MemPool_memdup(SAU_MemPool *restrict o,
		const void *restrict src, size_t size) SAU__malloclike;
