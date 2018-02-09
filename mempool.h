/* sgensys memory pool module.
 * Copyright (c) 2014 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __SGS_mempool_h
#define __SGS_mempool_h

#include <stddef.h>

struct SGSMemPool;
typedef struct SGSMemPool SGSMemPool;

SGSMemPool *SGS_create_mempool(size_t block_size);
void SGS_destroy_mempool(SGSMemPool *o);

void *SGS_mempool_alloc(SGSMemPool *o, size_t size);

#endif /* EOF */
