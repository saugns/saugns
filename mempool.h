/* ssndgen: Memory pool module.
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

struct SSG_MemPool;
typedef struct SSG_MemPool SSG_MemPool;

SSG_MemPool *SSG_create_MemPool(size_t block_size);
void SSG_destroy_MemPool(SSG_MemPool *o);

void *SSG_MemPool_alloc(SSG_MemPool *o, const void *src, size_t size);
