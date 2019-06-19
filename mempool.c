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

#include "mempool.h"
#include "arrtype.h"
#include <stdlib.h>
#include <string.h>

/* Not important enough to query page size for; use large-enough default. */
#define DEFAULT_BLOCK_SIZE \
	(4096 * 2)

#define ALIGN_BYTES \
	sizeof(void*)

#define ALIGN_SIZE(size) \
	(((size) + (ALIGN_BYTES - 1)) & ~(ALIGN_BYTES - 1))

typedef struct BlockEntry {
	void *mem;
	size_t free;
} BlockEntry;

SAU_DEF_ArrType(BlockArr, BlockEntry, _)

/*
 * Allocate new memory block,
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
static void *BlockArr_add(BlockArr *restrict o,
		size_t size_alloc, size_t size_used) {
	size_t i = o->count;
	if (!_BlockArr_add(o, NULL))
		return NULL;
	void *mem = calloc(1, size_alloc);
	if (!mem)
		return NULL;
	o->a[i].mem = mem;
	o->a[i].free = size_alloc - size_used;
	return mem;
}

/*
 * Free all memory blocks.
 */
static void BlockArr_clear(BlockArr *restrict o) {
	for (size_t i = 0; i < o->count; ++i) {
		free(o->a[i].mem);
	}
	_BlockArr_clear(o);
}

/*
 * Locate the first block with the smallest size into which \p size fits,
 * using binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
static bool BlockArr_first_smallest(const BlockArr *restrict o,
		size_t size, size_t *restrict id) {
	size_t i = 0;
	ptrdiff_t min = 0;
	ptrdiff_t max = o->count - 1;
	for (;;) {
		i = ((size_t)(min + max) >> 1);
		if (o->a[i].free < size) {
			min = i + 1;
			if (max < min) {
				++i;
				break;
			}
		} else {
			max = i - 1;
			if (max < min) {
				break;
			}
		}
	}
	if (i < o->count && o->a[i].free >= size) {
		*id = i;
		return true;
	}
	return false;
}

/*
 * Locate the first block with the smallest size greater than \p size,
 * using binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
#define BlockArr_first_greater(o, size, id) \
	BlockArr_first_smallest((o), (size) + 1, (id))

/*
 * Copy the blocks from \p from to \p to upwards one step.
 *
 * Technically, only the first block of each successive size is overwritten,
 * by the previous such block, until finally the last such block overwrites
 * the block at \p to.
 */
static void BlockArr_copy_up_one(BlockArr *restrict o,
		size_t to, size_t from) {
	if (from + 1 == to ||
		o->a[from].free == o->a[to - 1].free) {
		/*
		 * Either there are no blocks in-between, or they all have
		 * the same free space as the first; simply set the last to
		 * the first.
		 */
		o->a[to] = o->a[from];
	} else {
		/*
		 * Find the first block of the next larger size and recurse.
		 * Afterwards that block is overwritten by the original
		 * first block of this call.
		 */
		size_t higher_from;
		BlockArr_first_greater(o, o->a[from].free, &higher_from);
		BlockArr_copy_up_one(o, to, higher_from);
		o->a[higher_from] = o->a[from];
	}
}

struct SAU_MemPool {
	BlockArr blocks;
	size_t block_size;
};

/**
 * Create instance.
 *
 * \p block_size specifies the requested size of each memory block managed by
 * the memory pool. If 0 is passed, the default is used: 8KB, meant to take
 * up at least one memory page on the system. Performance measurement is
 * advised if a custom size is used.
 *
 * Allocations larger than the requested block size can still be made;
 * these are given individual blocks sized according to need.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_MemPool *SAU_create_MemPool(size_t block_size) {
	SAU_MemPool *o = calloc(1, sizeof(SAU_MemPool));
	if (!o)
		return NULL;
	o->block_size = (block_size > 0) ?
		ALIGN_SIZE(block_size) :
		DEFAULT_BLOCK_SIZE;
	return o;
}

/**
 * Destroy instance.
 */
void SAU_destroy_MemPool(SAU_MemPool *restrict o) {
	if (!o)
		return;
	BlockArr_clear(&o->blocks);
	free(o);
}

/**
 * Allocate block of \p size within the memory pool,
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
void *SAU_MemPool_alloc(SAU_MemPool *restrict o, size_t size) {
	size_t i = o->blocks.count;
	void *ret;
	size = ALIGN_SIZE(size);
	/*
	 * Find suitable block using binary search, or use a new if none.
	 */
	if ((i > 0 && size <= o->blocks.a[i - 1].free) &&
		BlockArr_first_smallest(&o->blocks, size, &i)) {
		size_t offset = o->block_size - o->blocks.a[i].free;
		ret = &((char*)o->blocks.a[i].mem)[offset];
		o->blocks.a[i].free -= size;
	} else {
		size_t alloc_size = (size > o->block_size) ?
			size :
			o->block_size;
		ret = BlockArr_add(&o->blocks, alloc_size, size);
		if (!ret)
			return NULL;
	}
	/*
	 * Sort blocks after allocation.
	 */
	if (i > 0) {
		/*
		 * The free space of the block at i is temporarily
		 * fudged in order for binary search to work reliably.
		 */
		size_t j;
		size_t i_free = o->blocks.a[i].free;
		o->blocks.a[i].free = o->blocks.a[i - 1].free;
		if (BlockArr_first_greater(&o->blocks, i_free, &j) && (j < i)) {
			/*
			 * Copy blocks upwards, then
			 * set the one at j to the one originally at i
			 * (and give it the restored free space value).
			 */
			BlockEntry tmp = o->blocks.a[i];
			tmp.free = i_free;
			BlockArr_copy_up_one(&o->blocks, i, j);
			o->blocks.a[j] = tmp;
		} else {
			o->blocks.a[i].free = i_free;
		}
	}
	return ret;
}

/**
 * Allocate block of \p size within the memory pool,
 * copied from \p src if not NULL, otherwise
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
void *SAU_MemPool_memdup(SAU_MemPool *restrict o,
		const void *restrict src, size_t size) {
	void *ret = SAU_MemPool_alloc(o, size);
	if (!ret)
		return NULL;
	if (src != NULL) {
		memcpy(ret, src, size);
	}
	return ret;
}
