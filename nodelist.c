/* saugns: Node list module.
 * Copyright (c) 2019-2020 Joel K. Pettersson
 * <joelkpettersson@gmail.com>.
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

#include "nodelist.h"
#include "mempool.h"

/**
 * Create instance using \p mempool.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_NodeList *SAU_create_NodeList(uint8_t list_type,
		SAU_MemPool *restrict mempool) {
	SAU_NodeList *o = SAU_MemPool_alloc(mempool, sizeof(SAU_NodeList));
	if (!o)
		return NULL;
	o->type = list_type;
	return o;
}

/**
 * Create shallow copy of \p src using \p mempool.
 * If \p src is NULL, the copy amounts to setting \p dstp to NULL.
 *
 * Further additions to a list with shallowly copied items
 * will un-shallow the copy.
 *
 * \return true, or false on allocation failure
 */
bool SAU_copy_NodeList(SAU_NodeList **restrict dstp,
		const SAU_NodeList *restrict src,
		SAU_MemPool *restrict mempool) {
	if (!src) {
		*dstp = NULL;
		return true;
	}
	if (!*dstp) {
		*dstp = SAU_MemPool_alloc(mempool, sizeof(SAU_NodeList));
		if (!*dstp)
			return false;
	}
	(*dstp)->refs = src->refs;
	(*dstp)->new_refs = NULL;
	(*dstp)->last_ref = NULL;
	(*dstp)->type = src->type;
	return true;
}

/**
 * Add reference item to the list, created using \p mempool.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_NodeRef *SAU_NodeList_add(SAU_NodeList *restrict o,
		void *restrict data, uint8_t ref_mode,
		SAU_MemPool *restrict mempool) {
	SAU_NodeRef *ref = SAU_MemPool_alloc(mempool, sizeof(SAU_NodeRef));
	if (!ref)
		return NULL;
	ref->data = data;
	if (!o->refs) {
		o->refs = ref;
		o->new_refs = ref;
	} else if (!o->new_refs) {
		/*
		 * Un-shallow copy.
		 */
		SAU_NodeRef *first_ref = SAU_MemPool_memdup(mempool,
				o->refs, sizeof(SAU_NodeRef));
		if (!first_ref)
			return NULL;
		SAU_NodeRef *last_ref = first_ref;
		for (SAU_NodeRef *src_ref = first_ref->next;
				src_ref != NULL; src_ref = src_ref->next) {
			SAU_NodeRef *dst_ref = SAU_MemPool_memdup(mempool,
					src_ref, sizeof(SAU_NodeRef));
			if (!dst_ref)
				return NULL;
			last_ref->next = dst_ref;
			last_ref = dst_ref;
		}
		o->refs = first_ref;
		o->new_refs = ref;
		o->last_ref = last_ref;
		o->last_ref->next = ref;
	} else {
		o->last_ref->next = ref;
	}
	o->last_ref = ref;
	ref->mode = ref_mode;
	ref->list_type = o->type;
	return ref;
}

/**
 * Remove reference items from the list,
 * leaving next list and type fields in place.
 */
void SAU_NodeList_clear(SAU_NodeList *restrict o) {
	o->refs = NULL;
	o->new_refs = NULL;
	o->last_ref = NULL;
}
