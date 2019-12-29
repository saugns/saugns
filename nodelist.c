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

#include "nodelist.h"
#include "mempool.h"

/**
 * Create instance using mempool.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_NodeList *SAU_create_NodeList(uint8_t list_type,
		SAU_MemPool *restrict memp) {
	SAU_NodeList *ol = SAU_MemPool_alloc(memp, sizeof(SAU_NodeList));
	if (!ol)
		return NULL;
	ol->type = list_type;
	return ol;
}

/**
 * Create shallow copy of \src_ol using \p mempool.
 * If \src_ol is NULL, the copy amounts to setting \p olp to NULL.
 *
 * As any node reference items are carried over directly,
 * further additions to the list will also change \p src_ol.
 *
 * \return true, or false on allocation failure
 */
bool SAU_shallow_copy_NodeList(SAU_NodeList **restrict olp,
		const SAU_NodeList *restrict src_ol,
		SAU_MemPool *restrict mempool) {
	if (!src_ol) {
		*olp = NULL;
		return true;
	}
	if (!*olp) *olp = SAU_MemPool_alloc(mempool, sizeof(SAU_NodeList));
	if (!*olp)
		return false;
	(*olp)->refs = src_ol->refs;
	(*olp)->last_ref = NULL;
	(*olp)->type = src_ol->type;
	return true;
}

/**
 * Add reference item to the list, created using \p mempool.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_NodeRef *SAU_NodeList_add(SAU_NodeList *restrict ol,
		void *restrict data, uint8_t ref_mode,
		SAU_MemPool *restrict mempool) {
	SAU_NodeRef *ref = SAU_MemPool_alloc(mempool, sizeof(SAU_NodeRef));
	if (!ref)
		return NULL;
	ref->data = data;
	if (!ol->refs)
		ol->refs = ref;
	else if (ol->last_ref != NULL)
		ol->last_ref->next = ref;
	else {
		/* un-shallowing of copy can be done here */
	}
	ol->last_ref = ref;
	ref->mode = ref_mode;
	ref->list_type = ol->type;
	return ref;
}

/**
 * Loop through non-copied reference items in the list,
 * calling \p data_f on each node.
 */
void SAU_NodeList_fornew(SAU_NodeList *restrict ol,
		SAU_NodeRef_data_f data_f) {
	if (!ol->last_ref)
		return;
	SAU_NodeRef *op_ref = ol->refs;
	for (; op_ref != NULL; op_ref = op_ref->next) data_f(op_ref->data);
}
