/* sgensys: Memory pool module.
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

#include "mempool.h"
#include <stdlib.h>
#if defined(__WIN32) || defined(__WIN64)
# define ssize_t ptrdiff_t
# define GET_PAGESIZE() 4096
#else
# include <unistd.h>
# define GET_PAGESIZE() sysconf(_SC_PAGESIZE)
#endif

#define DEFAULT_BLOCK_SIZE() \
	((size_t)(GET_PAGESIZE() * 2))

#define ALIGN_BYTES \
	(sizeof(void*) * 2)

#define ALIGN_SIZE(size) \
	(((size) + (ALIGN_BYTES - 1)) & ~(ALIGN_BYTES - 1))

typedef struct BlockEntry {
	void *mem;
	size_t free;
} BlockEntry;

struct SGSMemPool {
	BlockEntry *blocks;
	size_t block_size;
	size_t block_count;
	size_t block_alloc;
};

/**
 * Create and return pointer to SGSMemPool instance.
 *
 * \p block_size specifies the requested size of each memory block managed by
 * the memory pool. If 0 is passed, the default is used: twice the size of a
 * memory page on the system. Performance measurement is advised if a custom
 * size is used.
 *
 * Allocations larger than the requested block size can still be made; these
 * are given individual blocks sized according to need.
 *
 * \return SGSMemPool instance, or NULL if allocation fails
 */
SGSMemPool *SGS_create_mempool(size_t block_size) {
	SGSMemPool *o = calloc(1, sizeof(SGSMemPool));
	if (o == NULL) return NULL;
	o->block_size = (block_size > 0) ?
		ALIGN_SIZE(block_size) :
		DEFAULT_BLOCK_SIZE();
	return o;
}

void SGS_destroy_mempool(SGSMemPool *o) {
	size_t i;
	for (i = 0; i < o->block_count; ++i) {
		free(o->blocks[i].mem);
	}
	free(o->blocks);
	free(o);
}

/**
 * Locate the first block with the smallest size into which \p size fits,
 * using binary search.
 * \return id if found, -1 if not found
 */
static ssize_t first_smallest_block(SGSMemPool *o, size_t size) {
	size_t i = 0;
	ssize_t min = 0;
	ssize_t max = o->block_count - 1;
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
	return (i < o->block_count &&
		o->blocks[i].free >= size) ?
		(ssize_t)i :
		-1;
}

/**
 * Locate the first block with the smallest size greater than \p size, using
 * binary search.
 * \return id if found, -1 if not found
 */
#define first_block_greater_than(o, size) \
	first_smallest_block((o), (size) + 1)

/**
 * Copy the blocks from \p from to \p to upwards one step.
 *
 * Technically, only the first block of each successive size is overwritten,
 * by the previous such block, until finally the last such block overwrites
 * the block at \p to.
 */
static void copy_blocks_up_one(SGSMemPool *o, size_t to, size_t from) {
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
		higher_from = first_block_greater_than(o, o->blocks[from].free);
		copy_blocks_up_one(o, to, higher_from);
		o->blocks[higher_from] = o->blocks[from];
	}
}

/**
 * Add blocks to the memory pool. One if none, otherwise double the number.
 * \return the new number of blocks, or -1 upon failure
 */
static ssize_t extend_mempool(SGSMemPool *o) {
	size_t new_block_alloc = (o->block_alloc > 0) ?
		(o->block_alloc << 1) :
		1;
	o->blocks = realloc(o->blocks, sizeof(BlockEntry) * new_block_alloc);
	if (o->blocks == NULL)
		return -1;
	o->block_alloc = new_block_alloc;
	return o->block_alloc;
}

/**
 * Allocate object of \p size within the memory pool. If \p data is not NULL,
 * \p size bytes will be copied from it into the new object.
 *
 * \return the allocated object, or NULL if allocation fails
 */
void *SGS_mempool_alloc(SGSMemPool *o, size_t size) {
	ssize_t i;
	void *ret;
	size = ALIGN_SIZE(size);
	/*
	 * Check if suitable, pre-existing block can be found using binary
	 * search, and if so use it. Otherwise, use a new block.
	 */
	if (o->block_count > 0 &&
		size <= o->blocks[o->block_count - 1].free &&
		(i = first_smallest_block(o, size)) >= 0) {
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
			if (extend_mempool(o) < 0) return NULL;
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
		j = first_block_greater_than(o, i_free);
		o->blocks[i].free = i_free;
		if (j < (size_t)i) {
			/*
			 * Copy blocks upwards, then set the one at j to the
			 * one originally at i.
			 *
			 * Here, too, the free space at i needs to be fudged,
			 * since copy_blocks_up_one() relies on binary search.
			 */
			BlockEntry tmp = o->blocks[i];
			o->blocks[i].free = o->blocks[i - 1].free;
			copy_blocks_up_one(o, i, j);
			o->blocks[j] = tmp;
		}
	}
	return ret;
}

/* EOF */
