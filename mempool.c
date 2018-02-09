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
 * <http://www.gnu.org/licenses/>.
 */

#include "mempool.h"
#include <stdlib.h>
#include <string.h>

/* Not important enough to query page size for; use large-enough default. */
#define DEFAULT_BLOCK_SIZE (4096 * 2)

#define ALIGN_BYTES \
	(sizeof(void*) * 2)

#define ALIGN_SIZE(size) \
	(((size) + (ALIGN_BYTES - 1)) & ~(ALIGN_BYTES - 1))

typedef struct BlockEntry {
	void *mem;
	size_t free;
} BlockEntry;

struct SGS_MemPool {
	BlockEntry *blocks;
	size_t block_size;
	size_t block_count;
	size_t block_alloc;
};

/**
 * Create instance.
 *
 * \p block_size specifies the requested size of each memory block managed by
 * the memory pool. If 0 is passed, the default is used: 8KB, meant to take
 * up at least one memory page on the system. Performance measurement is
 * advised if a custom size is used.
 *
 * Allocations larger than the requested block size can still be made; these
 * are given individual blocks sized according to need.
 *
 * \return instance, or NULL if allocation fails
 */
SGS_MemPool *SGS_create_MemPool(size_t block_size) {
	SGS_MemPool *o = calloc(1, sizeof(SGS_MemPool));
	if (o == NULL) return NULL;
	o->block_size = (block_size > 0) ?
		ALIGN_SIZE(block_size) :
		DEFAULT_BLOCK_SIZE;
	return o;
}

void SGS_destroy_MemPool(SGS_MemPool *o) {
	size_t i;
	for (i = 0; i < o->block_count; ++i) {
		free(o->blocks[i].mem);
	}
	free(o->blocks);
	free(o);
}

/**
 * Locate the first block with the smallest size into which \p size fits,
 * using binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
static bool first_smallest_block(const SGS_MemPool *o,
		size_t size, size_t *id) {
	size_t i = 0;
	ptrdiff_t min = 0;
	ptrdiff_t max = o->block_count - 1;
	for (;;) {
		i = ((size_t)(min + max) >> 1);
		if (o->blocks[i].free < size) {
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
	if (i < o->block_count && o->blocks[i].free >= size) {
		*id = i;
		return true;
	}
	return false;
}

/**
 * Locate the first block with the smallest size greater than \p size, using
 * binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
#define first_greater_block(o, size, id) \
	first_smallest_block((o), (size) + 1, (id))

/**
 * Copy the blocks from \p from to \p to upwards one step.
 *
 * Technically, only the first block of each successive size is overwritten,
 * by the previous such block, until finally the last such block overwrites
 * the block at \p to.
 */
static void copy_blocks_up_one(SGS_MemPool *o, size_t to, size_t from) {
	if (from + 1 == to ||
		o->blocks[from].free == o->blocks[to - 1].free) {
		/*
		 * Either there are no blocks in-between, or they all have
		 * the same free space as the first; simply set the last to
		 * the first.
		 */
		o->blocks[to] = o->blocks[from];
	} else {
		/*
		 * Find the first block of the next larger size and recurse.
		 * Afterwards that block is overwritten by the original
		 * first block of this call.
		 */
		size_t higher_from;
		first_greater_block(o, o->blocks[from].free, &higher_from);
		copy_blocks_up_one(o, to, higher_from);
		o->blocks[higher_from] = o->blocks[from];
	}
}

/**
 * Add blocks to the memory pool. One if none, otherwise double the number.
 *
 * \return true, or false if allocation fails
 */
static bool extend_mempool(SGS_MemPool *o) {
	size_t new_block_alloc = (o->block_alloc > 0) ?
		(o->block_alloc << 1) :
		1;
	o->blocks = realloc(o->blocks, sizeof(BlockEntry) * new_block_alloc);
	if (o->blocks == NULL)
		return false;
	o->block_alloc = new_block_alloc;
	return true;
}

/**
 * Allocate block of \p size within the memory pool. If \p src is not
 * NULL, it will be copied to the new allocation.
 *
 * \return the allocated block, or NULL if allocation fails
 */
void *SGS_MemPool_alloc(SGS_MemPool *o, const void *src, size_t size) {
	size_t i;
	void *ret;
	size = ALIGN_SIZE(size);
	/*
	 * Check if suitable, pre-existing block can be found using binary
	 * search, and if so use it. Otherwise, use a new block.
	 */
	if (o->block_count > 0 &&
		size <= o->blocks[o->block_count - 1].free &&
		first_smallest_block(o, size, &i)) {
		/*
		 * Re-use old block for new allocation.
		 */
		size_t offset = o->block_size - o->blocks[i].free;
		ret = &((char*)o->blocks[i].mem)[offset];
		o->blocks[i].free -= size;
	} else {
		size_t alloc_size;
		/*
		 * Expand block entry array if needed.
		 */
		if (o->block_count == o->block_alloc) {
			if (!extend_mempool(o))
				return NULL;
		}
		/*
		 * Allocate new block.
		 */
		i = o->block_count;
		alloc_size = (size > o->block_size) ? size : o->block_size;
		ret = malloc(alloc_size);
		if (ret == NULL)
			return NULL;
		o->blocks[i].mem = ret;
		o->blocks[i].free = alloc_size - size;
		++o->block_count;
	}
	/*
	 * Sort blocks after the allocation; if several blocks exist, the
	 * one created or re-used may need to be moved in order for binary
	 * search to work.
	 */
	if (i > 0) {
		/*
		 * The free space of the block at i is temporarily
		 * fudged in order for binary search to work reliably.
		 */
		size_t j;
		size_t i_free = o->blocks[i].free;
		o->blocks[i].free = o->blocks[i - 1].free;
		if (first_greater_block(o, i_free, &j) && (j < i)) {
			/*
			 * Copy blocks upwards, then set the one at j to the
			 * one originally at i.
			 *
			 * The free space at i/j needs to remain fudged until
			 * after copy_blocks_up_one(), since it relies on
			 * binary search.
			 */
			BlockEntry tmp = o->blocks[i];
			tmp.free = i_free;
			copy_blocks_up_one(o, i, j);
			o->blocks[j] = tmp;
		} else {
			o->blocks[i].free = i_free;
		}
	}
	if (src != NULL) memcpy(ret, src, size);
	return ret;
}
