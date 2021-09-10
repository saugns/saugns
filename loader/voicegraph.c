/* sgensys: Program voice graph traverser.
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
static bool SGS_VoiceGraph_traverse_ops(SGS_VoiceGraph *restrict o,
		SGS_ProgramOpRef *restrict op_ref, uint32_t level) {
	SGS_OpAllocState *oas = &o->oa->a[op_ref->id];
	SGS_ProgramOpRef mod_op_ref;
	uint32_t i;
	if ((oas->flags & SGS_OAS_VISITED) != 0) {
		SGS_warning("parseconv",
"skipping operator %d; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (level > o->op_nest_depth) {
		o->op_nest_depth = level;
	}
	op_ref->level = level++;
	oas->flags |= SGS_OAS_VISITED;
	if (oas->fmods != NULL) {
		for (i = 0; i < oas->fmods->count; ++i) {
			mod_op_ref.id = oas->fmods->ids[i];
			mod_op_ref.use = SGS_POP_FMOD;
//			fprintf(stderr, "visit fmod node %d\n", mod_op_ref.id);
			if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
	}
	if (oas->pmods != NULL) {
		for (i = 0; i < oas->pmods->count; ++i) {
			mod_op_ref.id = oas->pmods->ids[i];
			mod_op_ref.use = SGS_POP_PMOD;
//			fprintf(stderr, "visit pmod node %d\n", mod_op_ref.id);
			if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
	}
	if (oas->amods != NULL) {
		for (i = 0; i < oas->amods->count; ++i) {
			mod_op_ref.id = oas->amods->ids[i];
			mod_op_ref.use = SGS_POP_AMOD;
//			fprintf(stderr, "visit amod node %d\n", mod_op_ref.id);
			if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
	}
	oas->flags &= ~SGS_OAS_VISITED;
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
bool SGS_VoiceGraph_set(SGS_VoiceGraph *restrict o,
		const SGS_ProgramEvent *restrict ev) {
	SGS_ProgramOpRef op_ref = {0, SGS_POP_CARR, 0};
	SGS_VoAllocState *vas = &o->va->a[ev->vo_id];
	SGS_ProgramVoData *vd = (SGS_ProgramVoData*) ev->vo_data;
	const SGS_ProgramOpList *carrs = vas->op_carrs;
	uint32_t i;
	if (!carrs)
		return true;
	for (i = 0; i < carrs->count; ++i) {
		op_ref.id = carrs->ids[i];
//		fprintf(stderr, "visit node %d\n", op_ref.id);
		if (!SGS_VoiceGraph_traverse_ops(o, &op_ref, 0))
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
void SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o) {
	OpRefArr_clear(&o->vo_graph);
}
