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
#include "../nodelist.h"

/**
 * Parse data operator flags.
 */
enum {
	SAU_PDOP_MULTIPLE = 1<<0,
	SAU_PDOP_NESTED = 1<<1,
	SAU_PDOP_HAS_COMPOSITE = 1<<2,
	SAU_PDOP_TIME_DEFAULT = 1<<3,
	SAU_PDOP_SILENCE_ADDED = 1<<4,
	SAU_PDOP_IGNORED = 1<<5, // used by parseconv
};

/**
 * Node type for operator data.
 */
typedef struct SAU_ParseOpData {
	struct SAU_ParseEvData *event;
	struct SAU_ParseOpData *prev; // previous for same op(s)
	SAU_NodeList *nest_lists;
	SAU_NodeList *last_nest_list;
	struct SAU_ParseOpData *next_bound;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_params;
	uint32_t time_ms, silence_ms;
	uint8_t wave;
	SAU_Ramp freq, freq2;
	SAU_Ramp amp, amp2;
	float phase;
	void *op_conv; // for parseconv
	void *op_context; // for parseconv
} SAU_ParseOpData;

/**
 * Parse data event flags.
 */
enum {
	SAU_PDEV_ADD_WAIT_DURATION = 1<<0,
};

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
	SAU_NodeList op_list; // immediately included operator references
	void *ev_conv; // for parseconv
	/* voice parameters */
	uint32_t vo_params;
	struct SAU_ParseEvData *vo_prev; /* preceding event for same voice */
	void *vo_context; // for parseconv
	SAU_Ramp pan;
} SAU_ParseEvData;

struct SAU_MemPool;
struct SAU_SymTab;

/**
 * Type returned after processing a file.
 */
typedef struct SAU_Parse {
	SAU_ParseEvData *events;
	const char *name; // currently simply set to the filename
	SAU_ScriptOptions sopt;
	struct SAU_SymTab *symtab;
	struct SAU_MemPool *mem; // internally used, provided until destroy
} SAU_Parse;

SAU_Parse *SAU_create_Parse(const char *restrict script_arg, bool is_path);
void SAU_destroy_Parse(SAU_Parse *restrict o);
