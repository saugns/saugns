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

#pragma once
#include "common.h"

/**
 * Forward-linked "reference" node.
 */
typedef struct SAU_NodeRef {
	struct SAU_NodeRef *next;
	void *data;
	void *label;
	uint8_t mode; // values defined by/for user
	uint8_t list_type; // copied from list type
} SAU_NodeRef;

/**
 * List of "reference" nodes, in turn also forward-linked.
 */
typedef struct SAU_NodeList {
	SAU_NodeRef *refs;
	SAU_NodeRef *new_refs; // NULL on copy
	SAU_NodeRef *last_ref; // NULL on copy
	struct SAU_NodeList *next;
	uint8_t type; // values defined by/for user
} SAU_NodeList;

struct SAU_MemPool;

SAU_NodeList *SAU_create_NodeList(uint8_t list_type,
		struct SAU_MemPool *restrict mempool) sauMalloclike;
bool SAU_copy_NodeList(SAU_NodeList **restrict dstp,
		const SAU_NodeList *restrict src,
		struct SAU_MemPool *restrict mempool);

SAU_NodeRef *SAU_NodeList_add(SAU_NodeList *restrict o,
		void *restrict data, uint8_t ref_mode,
		struct SAU_MemPool *restrict mempool) sauMalloclike;
void SAU_NodeList_clear(SAU_NodeList *restrict o);
