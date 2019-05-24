/* sgensys: Parser output data and functions.
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
typedef struct SGS_ParseOpData {
	struct SGS_ParseEvData *event;
	struct SGS_ParseOpData *next_bound;
	const char *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_params;
	uint32_t time_ms, silence_ms;
	uint8_t wave;
	SGS_Ramp freq, freq2;
	SGS_Ramp amp, amp2;
	float phase;
	struct SGS_ParseOpData *op_prev; /* preceding for same op(s) */
	void *op_conv; /* for parseconv */
	/* node adjacents in operator linkage graph */
	SGS_PtrList fmods, pmods, amods;
} SGS_ParseOpData;

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SGS_ParseEvData {
	struct SGS_ParseEvData *next;
	struct SGS_ParseEvData *groupfrom;
	struct SGS_ParseEvData *composite;
	uint32_t wait_ms;
	uint32_t ev_flags;
	SGS_PtrList operators; /* operator nodes directly linked from event */
	void *ev_conv; /* for parseconv */
	/* voice parameters */
	uint32_t vo_params;
	struct SGS_ParseEvData *vo_prev; /* preceding event for voice */
	SGS_Ramp pan;
} SGS_ParseEvData;

/**
 * Type returned after processing a file.
 */
typedef struct SGS_Parse {
	SGS_ParseEvData *events;
	const char *name; // currently simply set to the filename
	SGS_ScriptOptions sopt;
} SGS_Parse;

SGS_Parse *SGS_create_Parse(const char *restrict script_arg, bool is_path);
void SGS_destroy_Parse(SGS_Parse *restrict o);
