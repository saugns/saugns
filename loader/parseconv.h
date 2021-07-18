/* sgensys: Parse result to audio program converter.
 * Copyright (c) 2011-2012, 2017-2021 Joel K. Pettersson
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
#include "../script.h"
#include "../program.h"
#include "../arrtype.h"

/*
 * Voice allocation state flags.
 */
enum {
	SGS_VAS_GRAPH = 1<<0,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct SGS_VoAllocState {
	SGS_ScriptEvData *last_ev;
	SGS_ProgramOpList *op_carrs;
	uint32_t flags;
	uint32_t duration_ms;
} SGS_VoAllocState;

SGS_DEF_ArrType(SGS_VoAlloc, SGS_VoAllocState, _)

/*
 * Operator allocation state flags.
 */
enum {
	SGS_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct SGS_OpAllocState {
	SGS_ScriptOpData *last_pod;
	SGS_ProgramOpList *fmods, *pmods, *amods;
	uint32_t flags;
	//uint32_t duration_ms;
} SGS_OpAllocState;

SGS_DEF_ArrType(SGS_OpAlloc, SGS_OpAllocState, _)

SGS_DEF_ArrType(OpRefArr, SGS_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SGS_VoiceGraph {
	OpRefArr vo_graph;
	SGS_VoAlloc *va;
	SGS_OpAlloc *oa;
	uint32_t op_nest_depth;
} SGS_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void SGS_init_VoiceGraph(SGS_VoiceGraph *restrict o,
		SGS_VoAlloc *restrict va, SGS_OpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

void SGS_fini_VoiceGraph(SGS_VoiceGraph *restrict o);

bool SGS_VoiceGraph_set(SGS_VoiceGraph *restrict o,
		const SGS_ProgramEvent *restrict ev);
