/* sgensys memory pool module.
 * Copyright (c) 2014 Joel K. Pettersson <joelkpettersson@gmail.com>
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include "sgensys.h"
#include "mempool.h"
#include <string.h>
#include <stdlib.h>

#ifdef __WIN32
# define GET_PAGESIZE() 4096
#else
# include <sys/types.h>
# include <sys/mman.h>
# include <unistd.h>
# define GET_PAGESIZE() sysconf(_SC_PAGESIZE)
#endif

struct SGSMemPool {
	void **pages;
	uint *page_select;
	uint *page_space;
	uint page_size;
	uint page_count;
	uint page_alloc;
};

SGSMemPool *SGS_create_mempool() {
	SGSMemPool *o = malloc(sizeof(SGSMemPool));
	if (o == NULL) return NULL;
	o->pages = NULL;
	o->page_select = NULL;
	o->page_space = NULL;
	o->page_size = GET_PAGESIZE();
	o->page_count = 0;
	o->page_alloc = 0;
	return o;
}

void SGS_destroy_mempool(SGSMemPool *o) {
	uint i;
	for (i = 0; i < o->page_count; ++i) {
		free(o->pages[i]);
	}
	free(o->pages);
	free(o->page_select);
	free(o->page_space);
	free(o);
}

/**
 * Locate the smallest page for the given size using binary search.
 * \return id if found, -1 if not found
 */
static int smallest_page_for_size(SGSMemPool *o, uint size) {
	uint i = 0;
	int min = 0;
	int max = o->page_count - 1;
	for (;;) {
		uint page;
		i = ((uint)(min + max) >> 1);
		page = o->page_select[i];
		if (o->page_space[page] < size) {
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
	return (i < o->page_count && o->page_space[i] >= size) ?
		(int)i :
		-1;
}

/**
 * Add pages to the memory pool. One if none, otherwise double the number.
 * \return the new number of pages, or -1 upon failure
 */
static int extend_mempool(SGSMemPool *o) {
	uint i;
	uint new_page_alloc = (o->page_alloc > 0) ? (o->page_alloc << 1) : 1;
	o->pages = realloc(o->pages, sizeof(void*) * new_page_alloc);
	o->page_select = realloc(o->page_select, sizeof(uint) * new_page_alloc);
	o->page_space = realloc(o->page_space, sizeof(uint) * new_page_alloc);
	if (o->pages == NULL
	    || o->page_select == NULL
	    || o->page_space == NULL) return -1;
	for (i = o->page_alloc; i < new_page_alloc; ++i) {
		o->pages[i] = NULL; /* Allocated upon use. */
		o->page_select[i] = i;
		o->page_space[i] = o->page_size;
	}
	o->page_alloc = new_page_alloc;
	return o->page_alloc;
}

/**
 * Allocate object of \p size within the memory pool. If \p data is not NULL,
 * \p size bytes will be copied from it into the new object.
 *
 * \p size cannot exceed the memory page size of the system.
 * \return the allocated object, or NULL if allocation fails
 */
void *SGS_mempool_add(SGSMemPool *o, const void *data, uint size) {
	uint tmp, aligned_size, page_offset;
	int i, page = -1;
	void *dst;
	if (size > o->page_size) return NULL;
	/*
	 * Ensure size is aligned.
	 */
	aligned_size = size;
	tmp = aligned_size & (sizeof(void*)-1);
	if (tmp > 0) {
		aligned_size -= tmp;
		aligned_size += sizeof(void*);
	}
	/*
	 * Search for suitable page using binary search on available space.
	 */
	if (o->page_count > 0) {
		page = smallest_page_for_size(o, aligned_size);
		if (page >= 0) goto ALLOC;
	}
	/*
	 * Otherwise, use the next free page, if any.
	 */
	if (o->page_count < o->page_alloc) {
		page = o->page_count;
		goto ALLOC;
	}
	/*
	 * No suitable page found; extend allocation and use a new page.
	 */
	page = o->page_count;
	if (extend_mempool(o) == -1) return NULL;
	/*
	 * Allocate memory in the chosen page.
	 */
ALLOC:
	if (o->pages[page] == NULL) {
		o->pages[page] = malloc(o->page_size);
		if (o->pages[page] == NULL) return NULL;
		++o->page_count;
	}
	page_offset = o->page_size - o->page_space[page];
	dst = ((char*)o->pages[page]) + page_offset;
	if (data != NULL) memcpy(dst, data, size);
	o->page_space[page] -= aligned_size;
	/*
	 * Reorder pages according to new size.
	 */
	i = smallest_page_for_size(o, o->page_space[page]);
	if (i < page && i >= 0) {
		uint tmp2;
		tmp = o->page_select[i];
		o->page_select[i] = o->page_select[page];
		for (;;) {
			++i;
			tmp2 = o->page_select[i];
			o->page_select[i] = tmp;
			if (i >= page) {
				o->page_select[page] = tmp2;
				break;
			}
			++i;
			tmp = o->page_select[i];
			o->page_select[i] = tmp2;
			if (i >= page) {
				o->page_select[page] = tmp;
				break;
			}
		}
	}
	/*
	 * Done.
	 */
	return dst;
}

/* EOF */
