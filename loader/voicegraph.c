/* saugns: Program voice graph traverser.
 * Copyright (c) 2018-2021 Joel K. Pettersson
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

#include "parseconv.h"

/*
 * Traverse parts of voice operator graph reached from operator node,
 * adding reference after traversal of modulator lists.
 *
 * \return true, or false on allocation failure
 */
static bool SAU_VoiceGraph_traverse_ops(SAU_VoiceGraph *restrict o,
		SAU_ProgramOpRef *restrict op_ref, uint32_t level) {
	SAU_OpAllocState *oas = &o->oa->a[op_ref->id];
	SAU_ProgramOpRef mod_op_ref;
	uint32_t i;
	if ((oas->flags & SAU_OAS_VISITED) != 0) {
		SAU_warning("parseconv",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (level > o->op_nest_depth) {
		o->op_nest_depth = level;
	}
	op_ref->level = level++;
	oas->flags |= SAU_OAS_VISITED;
	if (oas->fmods != NULL) {
		for (i = 0; i < oas->fmods->count; ++i) {
			mod_op_ref.id = oas->fmods->ids[i];
			mod_op_ref.use = SAU_POP_FMOD;
//			fprintf(stderr, "visit fmod node %u\n", mod_op_ref.id);
			if (!SAU_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
	}
	if (oas->pmods != NULL) {
		for (i = 0; i < oas->pmods->count; ++i) {
			mod_op_ref.id = oas->pmods->ids[i];
			mod_op_ref.use = SAU_POP_PMOD;
//			fprintf(stderr, "visit pmod node %u\n", mod_op_ref.id);
			if (!SAU_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
	}
	if (oas->amods != NULL) {
		for (i = 0; i < oas->amods->count; ++i) {
			mod_op_ref.id = oas->amods->ids[i];
			mod_op_ref.use = SAU_POP_AMOD;
//			fprintf(stderr, "visit amod node %u\n", mod_op_ref.id);
			if (!SAU_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
	}
	oas->flags &= ~SAU_OAS_VISITED;
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
bool SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev) {
	SAU_ProgramOpRef op_ref = {0, SAU_POP_CARR, 0};
	SAU_VoAllocState *vas = &o->va->a[ev->vo_id];
	SAU_ProgramVoData *vd = (SAU_ProgramVoData*) ev->vo_data;
	const SAU_ProgramOpList *carrs = vas->op_carrs;
	uint32_t i;
	if (!carrs)
		return true;
	for (i = 0; i < carrs->count; ++i) {
		op_ref.id = carrs->ids[i];
//		fprintf(stderr, "visit node %u\n", op_ref.id);
		if (!SAU_VoiceGraph_traverse_ops(o, &op_ref, 0))
			return false;
	}
	if (!OpRefArr_memdup(&o->vo_graph, &vd->graph))
		return false;
	vd->op_count = o->vo_graph.count;
	o->vo_graph.count = 0; // reuse allocation
	return true;
}

/**
 * Destroy data held by instance.
 */
void SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o) {
	OpRefArr_clear(&o->vo_graph);
}
