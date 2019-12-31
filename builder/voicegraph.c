/* saugns: Program voice graph traverser.
 * Copyright (c) 2018-2019 Joel K. Pettersson
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

#include "builder.h"

static bool VoiceGraph_handle_op_node(VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref);

/*
 * Traverse operator list, as part of building a graph for the voice.
 *
 * \return true, or false on allocation failure
 */
static bool VoiceGraph_handle_op_list(VoiceGraph *restrict o,
		const SAU_ProgramOpList *restrict op_list, uint8_t mod_use) {
	SAU_ProgramOpRef op_ref = {0, mod_use, o->op_nest_level};
	for (uint32_t i = 0; i < op_list->count; ++i) {
		op_ref.id = op_list->ids[i];
		if (!VoiceGraph_handle_op_node(o, &op_ref))
			return false;
	}
	return true;
}

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool VoiceGraph_handle_op_node(VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref) {
	SAU_OpAllocState *oas = &o->oa->a[op_ref->id];
	if (oas->flags & SAU_OPAS_VISITED) {
		SAU_warning("voicegraph",
"skipping operator %d; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (o->op_nest_level > o->op_nest_max) {
		o->op_nest_max = o->op_nest_level;
	}
	++o->op_nest_level;
	oas->flags |= SAU_OPAS_VISITED;
	if (!VoiceGraph_handle_op_list(o, oas->fmods, SAU_POP_FMOD))
		return false;
	if (!VoiceGraph_handle_op_list(o, oas->pmods, SAU_POP_PMOD))
		return false;
	if (!VoiceGraph_handle_op_list(o, oas->amods, SAU_POP_AMOD))
		return false;
	oas->flags &= ~SAU_OPAS_VISITED;
	--o->op_nest_level;
	if (!OpRefArr_add(&o->vo_graph, op_ref))
		return false;
	return true;
}

/**
 * Create operator graph for voice using data built
 * during allocation, assigning an operator reference
 * list to the voice and block IDs to the operators.
 *
 * \return true, or false on allocation failure
 */
bool SAU_VoiceGraph_set(VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev) {
	SAU_VoAllocState *vas = &o->va->a[ev->vo_id];
	if (!vas->op_carriers->count) goto DONE;
	if (!VoiceGraph_handle_op_list(o, vas->op_carriers, SAU_POP_CARR))
		return false;
	SAU_ProgramVoData *vd = (SAU_ProgramVoData*) ev->vo_data;
	if (!OpRefArr_mpmemdup(&o->vo_graph, &vd->graph, o->mem))
		return false;
	vd->graph_count = o->vo_graph.count;
DONE:
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/**
 * Destroy data held by instance.
 */
void SAU_fini_VoiceGraph(VoiceGraph *restrict o) {
	OpRefArr_clear(&o->vo_graph);
}
