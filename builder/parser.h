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
 * Node type for operator data.
 */
typedef struct SAU_ParseOpData {
	struct SAU_ParseEvData *event;
	struct SAU_ParseOpData *next_bound;
	const char *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_params;
	uint32_t time_ms, silence_ms;
	uint8_t wave;
	SAU_Ramp freq, freq2;
	SAU_Ramp amp, amp2;
	float phase;
	struct SAU_ParseOpData *op_prev; /* preceding for same op(s) */
	void *op_conv; /* for parseconv */
	/* node adjacents in operator linkage graph */
	SAU_PtrList fmods, pmods, amods;
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
	SAU_PtrList operators; /* operator nodes directly linked from event */
	void *ev_conv; /* for parseconv */
	/* voice parameters */
	uint32_t vo_params;
	struct SAU_ParseEvData *vo_prev; /* preceding event for voice */
	SAU_Ramp pan;
} SAU_ParseEvData;

/**
 * Type returned after processing a file.
 */
typedef struct SAU_Parse {
	SAU_ParseEvData *events;
	const char *name; // currently simply set to the filename
	SAU_ScriptOptions sopt;
} SAU_Parse;

SAU_Parse *SAU_create_Parse(const char *restrict script_arg, bool is_path);
void SAU_destroy_Parse(SAU_Parse *restrict o);
