/* sgensys: Add-only allocator.
 * Copyright (c) 2014, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "sgensys.h"

struct SGS_AOAlloc;
typedef struct SGS_AOAlloc *SGS_AOAlloc_t;

SGS_AOAlloc_t SGS_create_aoalloc(size_t block_size);
void SGS_destroy_aoalloc(SGS_AOAlloc_t o);

void *SGS_aoalloc_alloc(SGS_AOAlloc_t o, size_t size);
void *SGS_aoalloc_dup(SGS_AOAlloc_t o, const void *mem, size_t size);
