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

#include <sau/mempool.h>
#ifndef SAU_MEM_DEBUG
/*
 * Debug-friendly memory handling? (Slower.)
 *
 * Enable to simply calloc every allocation.
 */
# define SAU_MEM_DEBUG 0
#endif
#include <stdlib.h>
#include <string.h>

#define DEFAULT_START_SIZE 512

#define ALIGN_BYTES      sizeof(void*)
#define ALIGN_SIZE(size) (((size) + (ALIGN_BYTES - 1)) & ~(ALIGN_BYTES - 1))

typedef struct MemBlock {
	size_t free;
	char *mem;
} MemBlock;

typedef struct DtorItem {
	sauDtor_f func;
	void *arg;
	struct DtorItem *prev;
} DtorItem;

struct sauMempool {
	MemBlock *a;
	size_t count, first_i, a_len;
	size_t block_size, skip_size;
	DtorItem *last_dtor;
};

/*
 * Extend memory block array.
 *
 * \return true, or false on allocation failure
 */
static bool upsize(sauMempool *restrict o) {
	size_t new_a_len = (o->a_len > 0) ?  (o->a_len << 1) : 1;
	MemBlock *new_a = realloc(o->a, sizeof(MemBlock) * new_a_len);
	if (!new_a)
		return false;
	o->a = new_a;
#if !SAU_MEM_DEBUG
	o->block_size <<= 1;
	if (o->first_i < (o->a_len * 2) / 3) {
		/*
		 * If less than 2/3 of blocks are fully used, then
		 * allocation sizes may be too awkward for the old
		 * full-usage threshold.
		 */
		o->skip_size = (o->skip_size > 0) ?
			(o->skip_size << 1) :
			ALIGN_BYTES;
	}
#endif
	o->a_len = new_a_len;
	return true;
}

#if !SAU_MEM_DEBUG
/*
 * Allocate new memory block, initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
static void *add(sauMempool *restrict o, size_t size_used) {
	if (o->count == o->a_len && !upsize(o))
		return NULL;
	size_t block_size = o->block_size;
	if (block_size < size_used) block_size = size_used;
	char *mem = calloc(1, block_size);
	if (!mem)
		return NULL;
	size_t i = o->count++;
	o->a[i].free = block_size - size_used;
	o->a[i].mem = mem;
	/*
	 * Skip fully used blocks in binary searches.
	 */
	while (o->first_i < o->count && o->a[o->first_i].free <= o->skip_size)
		++o->first_i;
	return mem + o->a[i].free;
}

/*
 * Locate the first block with the smallest size into which \p size fits,
 * using binary search. If found, \p id will be set to the id.
 *
 * \return true if found, false if not
 */
static bool first_smallest(const sauMempool *restrict o,
		size_t size, size_t *restrict id) {
	size_t i = 0;
	size_t min = o->first_i, max = o->count;
	while (min < max) {
		i = min + ((max - min) >> 1);
		if (o->a[i].free < size) {
			min = i + 1;
			i = min;
		} else {
			max = i;
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
#define first_greater(o, size, id) \
	first_smallest((o), (size) + 1, (id))

/*
 * Copy the blocks from \p from to \p to upwards one step.
 *
 * Technically, only the first block of each successive size is overwritten,
 * by the previous such block, until finally the last such block overwrites
 * the block at \p to.
 */
static void copy_up_one(sauMempool *restrict o,
		size_t to, size_t from) {
	size_t higher_from;
	if (from == (to - 1) || o->a[from].free == o->a[to - 1].free) {
		/*
		 * Either there are no blocks in-between, or they all have
		 * the same free space as the first; simply set the last to
		 * the first.
		 */
		o->a[to] = o->a[from];
	} else if (first_greater(o, o->a[from].free, &higher_from)) {
		/*
		 * Find the first block of the next larger size and recurse.
		 * Afterwards that block is overwritten by the original
		 * first block of this call.
		 */
		copy_up_one(o, to, higher_from);
		o->a[higher_from] = o->a[from];
	}
}
#endif

/**
 * Create instance.
 *
 * \p start_size specifies the normal size of the first internal memory block
 * made; 0 can be passed to use a default value of 512. The second block made
 * will be twice as large, and after that, the size will double each time the
 * block count rises above a power of two.
 *
 * For an allocation with a size larger than the current block size, a single
 * block sized according to need will be made while the block count increases
 * by one and normal block sizing continues as if the block had been normally
 * sized. Some such outliers early on will be accommodated gracefully, but if
 * there are many over time, a larger \p start_size value may perform better.
 *
 * \return instance, or NULL on allocation failure
 */
sauMempool *sau_create_Mempool(size_t start_size) {
	sauMempool *o = calloc(1, sizeof(sauMempool));
	if (!o)
		return NULL;
	o->block_size = (start_size > 0) ?
		ALIGN_SIZE(start_size) :
		DEFAULT_START_SIZE;
	o->block_size >>= 1;
	return o;
}

/**
 * Destroy instance.
 *
 * Frees all memory blocks. Any destructor functions registered
 * are called beforehand, in the reverse order of registration.
 */
void sau_destroy_Mempool(sauMempool *restrict o) {
	if (!o)
		return;
	for (DtorItem *n = o->last_dtor; n; n = n->prev) {
		n->func(n->arg);
	}
	for (size_t i = 0; i < o->count; ++i) {
		free(o->a[i].mem);
	}
	free(o->a);
	free(o);
}

/**
 * Allocate block of \p size within the memory pool,
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
void *sau_mpalloc(sauMempool *restrict o, size_t size) {
#if !SAU_MEM_DEBUG
	size_t i = o->count;
	void *mem;
	size = ALIGN_SIZE(size);
	/*
	 * If blocks exist and the most spacious can hold the size,
	 * pick least-free-space best fit using binary search.
	 * Otherwise, use a new block.
	 */
	if (first_smallest(o, size, &i)) {
		o->a[i].free -= size;
		mem = o->a[i].mem + o->a[i].free;
	} else {
		mem = add(o, size);
		if (!mem)
			return NULL;
	}
	/*
	 * Sort blocks after allocation so that binary search will work.
	 */
	if (i > 0) {
		/*
		 * The free space of the block at i is temporarily
		 * fudged in order for binary search to work reliably.
		 */
		size_t j;
		size_t i_free = o->a[i].free;
		o->a[i].free = o->a[i - 1].free;
		if (first_greater(o, i_free, &j) && j < i) {
			/*
			 * Copy blocks upwards, then
			 * set the one at j to the one originally at i
			 * (and give it the restored free space value).
			 */
			MemBlock tmp = o->a[i];
			tmp.free = i_free;
			copy_up_one(o, i, j);
			o->a[j] = tmp;
		} else {
			o->a[i].free = i_free;
		}
	}
	return mem;
#else /* SAU_MEM_DEBUG */
	if (o->count == o->a_len && !upsize(o))
		return NULL;
	void *mem = calloc(1, size);
	if (!mem)
		return NULL;
	o->a[o->count++].mem = mem;
	return mem;
#endif
}

/**
 * Allocate block of \p size within the memory pool,
 * copied from \p src if not NULL, otherwise
 * initialized to zero bytes.
 *
 * \return allocated memory, or NULL on allocation failure
 */
void *sau_mpmemdup(sauMempool *restrict o,
		const void *restrict src, size_t size) {
	void *mem = sau_mpalloc(o, size);
	if (!mem)
		return NULL;
	if (src != NULL)
		memcpy(mem, src, size);
	return mem;
}

/**
 * Register a destructor function for calling upon mempool destruction.
 * Meant for allocations for which a cleanup call should be associated.
 *
 * \return true, or false for NULL func or arg or on allocation failure
 */
bool sau_mpregdtor(sauMempool *restrict o,
		sauDtor_f func, void *restrict arg) {
	if (!func || !arg)
		return false;
	DtorItem *n = sau_mpalloc(o, sizeof(DtorItem));
	if (!n)
		return false;
	n->func = func;
	n->arg = arg;
	n->prev = o->last_dtor;
	o->last_dtor = n;
	return true;
}
