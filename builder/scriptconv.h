/* saugns: Script data to audio program converter.
 * Copyright (c) 2011-2012, 2017-2020 Joel K. Pettersson
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
	SAU_VAS_GRAPH = 1<<0,
};

/*
 * Per-voice state used during program data allocation.
 */
typedef struct SAU_VoAllocState {
	SAU_ScriptEvData *last_ev;
	SAU_ProgramOpGraph *op_graph;
	uint32_t flags;
	uint32_t duration_ms;
} SAU_VoAllocState;

SAU_DEF_ArrType(SAU_VoAlloc, SAU_VoAllocState, _)

/*
 * Operator allocation state flags.
 */
enum {
	SAU_OAS_VISITED = 1<<0,
};

/*
 * Per-operator state used during program data allocation.
 */
typedef struct SAU_OpAllocState {
	SAU_ScriptOpData *last_sod;
	SAU_ProgramOpAdjcs *adjcs;
	uint32_t flags;
	//uint32_t duration_ms;
} SAU_OpAllocState;

SAU_DEF_ArrType(SAU_OpAlloc, SAU_OpAllocState, _)

SAU_DEF_ArrType(OpRefArr, SAU_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SAU_VoiceGraph {
	OpRefArr op_list;
	SAU_VoAlloc *va;
	SAU_OpAlloc *oa;
	uint32_t op_nest_depth;
} SAU_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void SAU_init_VoiceGraph(SAU_VoiceGraph *restrict o,
		SAU_VoAlloc *restrict va, SAU_OpAlloc *restrict oa) {
	o->va = va;
	o->oa = oa;
}

void SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o);

bool SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev);
