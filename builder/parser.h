/* saugns: Parser output data and functions.
 * Copyright (c) 2011-2012, 2017-2019 Joel K. Pettersson
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

/**
 * Node link types.
 */
enum {
	SAU_PDNL_REFER = 0,
	SAU_PDNL_GRAPH,
	SAU_PDNL_FMODS,
	SAU_PDNL_PMODS,
	SAU_PDNL_AMODS,
};

typedef struct SAU_ParseOpRef {
	struct SAU_ParseOpRef *next;
	struct SAU_ParseOpData *data;
	const char *label;
	uint8_t link_type;
} SAU_ParseOpRef;

typedef struct SAU_ParseOpList {
	SAU_ParseOpRef *refs;
	SAU_ParseOpRef *new_refs; // NULL on copy
	SAU_ParseOpRef *last_ref; // NULL on copy
} SAU_ParseOpList;

/**
 * Node type for operator data.
 */
typedef struct SAU_ParseOpData {
	struct SAU_ParseEvData *event;
	struct SAU_ParseOpData *next_bound;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_params;
	uint32_t time_ms, silence_ms;
	uint8_t wave;
	SAU_Ramp freq, freq2;
	SAU_Ramp amp, amp2;
	float phase;
	struct SAU_ParseOpData *op_prev; /* preceding for same op(s) */
	void *op_conv; // for parseconv
	/* node adjacents in operator linkage graph */
	SAU_ParseOpList fmod_list;
	SAU_ParseOpList pmod_list;
	SAU_ParseOpList amod_list;
} SAU_ParseOpData;

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SAU_ParseEvData {
	struct SAU_ParseEvData *next;
	struct SAU_ParseEvData *groupfrom;
	struct SAU_ParseEvData *composite;
	uint32_t wait_ms;
	uint32_t ev_flags;
	SAU_ParseOpList op_list; // immediately included operator references
	void *ev_conv; // for parseconv
	/* voice parameters */
	uint32_t vo_params;
	struct SAU_ParseEvData *vo_prev; /* preceding event for voice */
	SAU_Ramp pan;
} SAU_ParseEvData;

struct SAU_SymTab;

/**
 * Type returned after processing a file.
 */
typedef struct SAU_Parse {
	SAU_ParseEvData *events;
	const char *name; // currently simply set to the filename
	SAU_ScriptOptions sopt;
	struct SAU_SymTab *symtab;
	void *mem; // for internal use
} SAU_Parse;

SAU_Parse *SAU_create_Parse(const char *restrict script_arg, bool is_path);
void SAU_destroy_Parse(SAU_Parse *restrict o);
