/* sgensys: Intermediate program module.
 * Copyright (c) 2011-2012, 2017-2018 Joel K. Pettersson
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
#include "plist.h"
#include "program.h"
struct SGS_ParseResult;

/*
 * Intermediate program from parsing data. Uses a per-voice
 * graph structure for operators, with an adjacency list per
 * operator.
 *
 * Constructed converting events one by one from a parse
 * result, tracking voice and operator changes to allocate
 * voice and operator IDs.
 */

typedef struct SGS_IMPGraph {
	uint32_t opc;
	uint32_t ops[1]; /* sized to opc */
} SGS_IMPGraph;

typedef struct SGS_IMPGraphAdjcs {
	uint32_t fmodc;
	uint32_t pmodc;
	uint32_t amodc;
	uint32_t level;  /* index for buffer used to store result to use if
	                    node revisited when traversing the graph. */
	uint32_t adjcs[1]; /* sized to total number */
} SGS_IMPGraphAdjcs;

typedef struct SGS_IMPVoiceData {
	SGS_IMPGraph *graph;
	uint8_t attr;
	float panning;
	SGS_ProgramValit valitpanning;
} SGS_IMPVoiceData;

typedef struct SGS_IMPOperatorData {
	SGS_IMPGraphAdjcs *adjcs;
	uint32_t op_id;
	//uint32_t output_block_id;
	//int32_t freq_block_id, /* -1 if none */
	//	freq_mod_block_id,
	//	phase_mod_block_id,
	//	amp_block_id,
	//	amp_mod_block_id;
	uint8_t attr;
	SGS_wave_t wave;
	uint32_t time_ms, silence_ms;
	float freq, dynfreq, phase, amp, dynamp;
	SGS_ProgramValit valitfreq, valitamp;
} SGS_IMPOperatorData;

typedef struct SGS_IMPEvent {
	uint32_t wait_ms;
	uint32_t params;
	uint32_t vo_id; /* needed for both voice and operator data */
	SGS_IMPOperatorData *op_data;
	SGS_IMPVoiceData *vo_data;
} SGS_IMPEvent;

typedef struct SGS_IMP {
	SGS_PList ev_list;
	struct SGS_ParseResult *parse;
	uint32_t op_count;
	uint16_t vo_count;
	size_t odata_count,
		vdata_count;
} SGS_IMP;

SGS_IMP *SGS_create_IMP(struct SGS_ParseResult *parse);
void SGS_destroy_IMP(SGS_IMP *o);

void SGS_IMP_print_info(SGS_IMP *o);
