/* saugns: Node list module.
 * Copyright (c) 2019 Joel K. Pettersson
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

#pragma once
#include "common.h"

/**
 * Node reference modes.
 */
enum {
	SAU_NRM_UPDATE = 0,
	SAU_NRM_ADD = 1<<0,
};

typedef struct SAU_NodeRef {
	struct SAU_NodeRef *next;
	void *data;
	const char *label;
	uint8_t mode;
	uint8_t list_type;
} SAU_NodeRef;

/**
 * Node list types.
 */
enum {
	SAU_NLT_GRAPH = 0,
	SAU_NLT_FMODS,
	SAU_NLT_PMODS,
	SAU_NLT_AMODS,
};

typedef struct SAU_NodeList {
	SAU_NodeRef *refs;
	SAU_NodeRef *new_refs; // NULL on copy
	SAU_NodeRef *last_ref; // NULL on copy
	struct SAU_NodeList *next;
	uint8_t type;
} SAU_NodeList;

typedef void (*SAU_NodeRef_data_f)(void *restrict data);

struct SAU_MemPool;

SAU_NodeList *SAU_create_NodeList(uint8_t list_type,
		struct SAU_MemPool *restrict mempool);
bool SAU_copy_NodeList(SAU_NodeList **restrict olp,
		const SAU_NodeList *restrict src_ol,
		struct SAU_MemPool *restrict mempool);

SAU_NodeRef *SAU_NodeList_add(SAU_NodeList *restrict ol,
		void *restrict data, uint8_t ref_mode,
		struct SAU_MemPool *restrict mempool);
void SAU_NodeList_fornew(SAU_NodeList *restrict ol,
		SAU_NodeRef_data_f data_f);
