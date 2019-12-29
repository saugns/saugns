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
 * Create copy of \src_ol using mempool, unless \src_ol is NULL.
 * A NULL list is copied by setting \p olp to NULL.
 *
 * \return true, or false on allocation failure
 */
bool SAU_copy_NodeList(SAU_NodeList **restrict olp,
		const SAU_NodeList *restrict src_ol,
		SAU_MemPool *restrict memp) {
	if (!src_ol) {
		*olp = NULL;
		return true;
	}
	if (!*olp) *olp = SAU_MemPool_alloc(memp, sizeof(SAU_NodeList));
	if (!*olp)
		return false;
	(*olp)->refs = src_ol->refs;
	(*olp)->new_refs = NULL;
	(*olp)->last_ref = NULL;
	(*olp)->type = src_ol->type;
	return true;
}

/**
 * Add reference to the list after creation using the mempool.
 *
 * \return instance, or NULL on allocation failure
 */
SAU_NodeRef *SAU_NodeList_add(SAU_NodeList *restrict ol,
		void *restrict data, uint8_t ref_mode,
		SAU_MemPool *restrict memp) {
	SAU_NodeRef *ref = SAU_MemPool_alloc(memp, sizeof(SAU_NodeRef));
	if (!ref)
		return NULL;
	ref->data = data;
	if (!ol->refs)
		ol->refs = ref;
	if (!ol->new_refs)
		ol->new_refs = ref;
	else
		ol->last_ref->next = ref;
	ol->last_ref = ref;
	ref->mode = ref_mode;
	ref->list_type = ol->type;
	return ref;
}

/**
 * Loop through \a new_refs in list, calling \p data_f on each node.
 */
void SAU_NodeList_fornew(SAU_NodeList *restrict ol,
		SAU_NodeRef_data_f data_f) {
	SAU_NodeRef *op_ref = ol->new_refs;
	for (; op_ref != NULL; op_ref = op_ref->next) data_f(op_ref->data);
}
