/* sgensys pointer array module.
 * Copyright (c) 2011-2012, 2018 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version; WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the file COPYING for details, or if missing, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include "ptrarr.h"

/**
 * Add a pointer to the given array.
 */
void SGS_ptrarr_add(struct SGSPtrArr *list, void *value) {
	if (list->count == list->copy_count) {
		void *old_list = list->data;
		if (list->count == list->alloc) {
			list->alloc <<= 1;
		}
		list->data = malloc(sizeof(void*) * list->alloc);
		memcpy(list->data, old_list, sizeof(void*) * list->count);
	} else if (list->count == list->alloc) {
		list->alloc <<= 1;
	    	list->data = realloc(list->data, sizeof(void*) * list->alloc);
	}

	list->data[list->count] = value;
	++list->count;
}

/**
 * Clear the given array.
 */
void SGS_ptrarr_clear(struct SGSPtrArr *list) {
	if (list->count > list->copy_count) {
		free(list->data);
	}
	list->count = 0;
	list->copy_count = 0;
	list->data = 0;
	list->alloc = 0;
}

/**
 * Copy the array src to dst (clearing dst first if needed); to save
 * memory, dst will actually merely reference the data in src
 * unless/until added to.
 *
 * copy_count will be set to the count of src, so that iteration
 * beginning at that value will ignore copied entries.
 */
void SGS_ptrarr_copy(struct SGSPtrArr *dst, const struct SGSPtrArr *src) {
	SGS_ptrarr_clear(dst);
	dst->count = src->count;
	dst->copy_count = src->count;
	dst->data = src->data;
	dst->alloc = src->alloc;
}
