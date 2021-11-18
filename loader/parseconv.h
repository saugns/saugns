/* saugns: Parse result to audio program converter.
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
#include "../mempool.h"

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
	const SAU_ProgramOpList *op_carrs;
	uint32_t flags;
	uint32_t duration_ms;
} SAU_VoAllocState;

sauArrType(SAU_VoAlloc, SAU_VoAllocState, _)

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
	SAU_ScriptOpRef *last_pod;
	const SAU_ProgramOpList *fmods, *pmods, *amods;
	uint32_t flags;
	//uint32_t duration_ms;
} SAU_OpAllocState;

sauArrType(SAU_OpAlloc, SAU_OpAllocState, _)

sauArrType(OpRefArr, SAU_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct SAU_VoiceGraph {
	OpRefArr vo_graph;
	SAU_VoAlloc *va;
	SAU_OpAlloc *oa;
	uint32_t op_nest_level, op_nest_max;
	struct SAU_MemPool *mem;
} SAU_VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void
SAU_init_VoiceGraph(SAU_VoiceGraph *restrict o,
		SAU_VoAlloc *restrict va,
		SAU_OpAlloc *restrict oa,
		struct SAU_MemPool *restrict mem) {
	o->va = va;
	o->oa = oa;
	o->mem = mem;
}

void SAU_fini_VoiceGraph(SAU_VoiceGraph *restrict o);

bool SAU_VoiceGraph_set(SAU_VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev);
