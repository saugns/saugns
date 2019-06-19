/* saugns: Script file data and functions.
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
#include "ptrlist.h"
#include "program.h"

/**
 * Script data operator flags.
 */
enum {
	SAU_SDOP_NEW_CARRIER = 1<<0,
	SAU_SDOP_LATER_USED = 1<<1,
	SAU_SDOP_MULTIPLE = 1<<2,
	SAU_SDOP_NESTED = 1<<3,
	SAU_SDOP_TIME_DEFAULT = 1<<4,
	SAU_SDOP_SILENCE_ADDED = 1<<5,
};

/**
 * Node type for operator data.
 */
typedef struct SAU_ScriptOpData {
	struct SAU_ScriptEvData *event;
	struct SAU_ScriptOpData *next_bound;
	const char *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_id;
	uint32_t op_params;
	uint32_t time_ms, silence_ms;
	uint8_t wave;
	SAU_Ramp freq, freq2;
	SAU_Ramp amp, amp2;
	float phase;
	struct SAU_ScriptOpData *op_prev; /* preceding for same op(s) */
	SAU_PtrList op_next; /* all immediate forward refs for op(s) */
	/* node adjacents in operator linkage graph */
	SAU_PtrList fmods, pmods, amods;
} SAU_ScriptOpData;

/**
 * Script data event flags.
 */
enum {
	SAU_SDEV_NEW_OPGRAPH = 1<<0,
	SAU_SDEV_VOICE_LATER_USED = 1<<1,
	SAU_SDEV_ADD_WAIT_DURATION = 1<<2,
};

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SAU_ScriptEvData {
	struct SAU_ScriptEvData *next;
	uint32_t wait_ms;
	uint32_t ev_flags;
	SAU_PtrList op_all;
	/* voice parameters */
	uint32_t vo_id;
	uint32_t vo_params;
	struct SAU_ScriptEvData *vo_prev; /* preceding event for voice */
	SAU_Ramp pan;
	SAU_PtrList op_graph;
} SAU_ScriptEvData;

/**
 * Script data option flags.
 *
 * Set after parsing the setting of script options in a script.
 */
enum {
	SAU_SOPT_AMPMULT = 1<<0,
	SAU_SOPT_A4_FREQ = 1<<1,
	SAU_SOPT_DEF_TIME = 1<<2,
	SAU_SOPT_DEF_FREQ = 1<<3,
	SAU_SOPT_DEF_RATIO = 1<<4,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SAU_ScriptOptions {
	uint32_t changed; // flags (SAU_SOPT_*) set upon change by script
	float ampmult;    // amplitude multiplier for non-modulator operators
	float A4_freq;    // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_freq,
	      def_relfreq;
} SAU_ScriptOptions;

/**
 * Type returned after processing a file.
 */
typedef struct SAU_Script {
	SAU_ScriptEvData *events;
	const char *name; // currently simply set to the filename
	SAU_ScriptOptions sopt;
} SAU_Script;

SAU_Script *SAU_load_Script(const char *restrict script_arg, bool is_path);
void SAU_discard_Script(SAU_Script *restrict o);
