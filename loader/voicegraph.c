/* sgensys: Program voice graph traverser.
 * Copyright (c) 2018-2020 Joel K. Pettersson
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
	uint32_t i;
	if ((oas->flags & SGS_OAS_VISITED) != 0) {
		SGS_warning("parseconv",
"skipping operator %u; circular references unsupported",
			op_ref->id);
		return true;
	}
	if (level > o->op_nest_depth) {
		o->op_nest_depth = level;
	}
	op_ref->level = level++;
	if (oas->adjcs != NULL) {
		SGS_ProgramOpRef mod_op_ref;
		const SGS_ProgramOpAdjcs *adjcs = oas->adjcs;
		const uint32_t *mods = oas->adjcs->adjcs;
		uint32_t modc = 0;
		oas->flags |= SGS_OAS_VISITED;
		i = 0;
		modc += adjcs->fmodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_POP_FMOD;
//			fprintf(stderr, "visit fmod node %u\n", mod_op_ref.id);
			if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
		modc += adjcs->pmodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_POP_PMOD;
//			fprintf(stderr, "visit pmod node %u\n", mod_op_ref.id);
			if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
		modc += adjcs->amodc;
		for (; i < modc; ++i) {
			mod_op_ref.id = mods[i];
			mod_op_ref.use = SGS_POP_AMOD;
//			fprintf(stderr, "visit amod node %u\n", mod_op_ref.id);
			if (!SGS_VoiceGraph_traverse_ops(o, &mod_op_ref, level))
				return false;
		}
		oas->flags &= ~SGS_OAS_VISITED;
	}
	if (!OpRefArr_add(&o->op_list, op_ref))
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
	const SGS_ProgramOpGraph *graph = vas->op_graph;
	uint32_t i;
	if (!graph)
		return true;
	for (i = 0; i < graph->opc; ++i) {
		op_ref.id = graph->ops[i];
//		fprintf(stderr, "visit node %u\n", op_ref.id);
		if (!SGS_VoiceGraph_traverse_ops(o, &op_ref, 0))
			return false;
	}
	if (!OpRefArr_memdup(&o->op_list, &vd->op_list))
		return false;
	vd->op_count = o->op_list.count;
	o->op_list.count = 0; // reuse allocation
	return true;
}

/**
 * Destroy data held by instance.
 */
void SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o) {
	OpRefArr_clear(&o->op_list);
}
