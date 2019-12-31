/* saugns: Audio program builder module.
 * Copyright (c) 2011-2013, 2017-2019 Joel K. Pettersson
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
#include "../arrtype.h"
#include "../mempool.h"

/**
 * Voice allocation state flags.
 */
enum {
	SAU_VOAS_GRAPH = 1<<0,
};

/**
 * Per-voice state used during program data allocation.
 */
typedef struct SAU_VoAllocState {
	SAU_ScriptEvData *last_ev;
	const SAU_ProgramOpList *op_carriers;
	uint32_t flags;
	uint32_t duration_ms;
} SAU_VoAllocState;

sauArrType(SAU_VoAlloc, SAU_VoAllocState, _)

/**
 * Operator allocation state flags.
 */
enum {
	SAU_OPAS_VISITED = 1<<0,
};

/**
 * Per-operator state used during program data allocation.
 */
typedef struct SAU_OpAllocState {
	SAU_ScriptOpData *last_sod;
	const SAU_ProgramOpList *fmods;
	const SAU_ProgramOpList *pmods;
	const SAU_ProgramOpList *amods;
	uint32_t flags;
	//uint32_t duration_ms;
} SAU_OpAllocState;

sauArrType(SAU_OpAlloc, SAU_OpAllocState, _)

sauArrType(OpRefArr, SAU_ProgramOpRef, )

/**
 * Voice data, held during program building and set per event.
 */
typedef struct VoiceGraph {
	OpRefArr vo_graph;
	uint32_t op_nest_level;
	uint32_t op_nest_max; // for all traversals
	SAU_VoAlloc *va;
	SAU_OpAlloc *oa;
	SAU_MemPool *mem;
} VoiceGraph;

/**
 * Initialize instance for use.
 */
static inline void SAU_init_VoiceGraph(VoiceGraph *restrict o,
		SAU_VoAlloc *restrict va,
		SAU_OpAlloc *restrict oa,
		SAU_MemPool *restrict mem) {
	o->va = va;
	o->oa = oa;
	o->mem = mem;
}

void SAU_fini_VoiceGraph(VoiceGraph *restrict o);

bool SAU_VoiceGraph_set(VoiceGraph *restrict o,
		const SAU_ProgramEvent *restrict ev);
