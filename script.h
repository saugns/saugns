/* sgensys: Script file data and functions.
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
#include "ptrlist.h"
#include "program.h"

/**
 * Script data operator flags.
 */
enum {
	SGS_SDOP_LATER_USED = 1<<0,
	SGS_SDOP_MULTIPLE = 1<<1,
	SGS_SDOP_NESTED = 1<<2,
	SGS_SDOP_TIME_DEFAULT = 1<<3,
	SGS_SDOP_SILENCE_ADDED = 1<<4,
	SGS_SDOP_HAS_COMPOSITE = 1<<5,
};

/**
 * Node type for operator data.
 */
typedef struct SGS_ScriptOpData {
	struct SGS_ScriptEvData *event;
	struct SGS_ScriptOpData *on_prev; /* preceding for same op(s) */
	SGS_PtrList on_next; /* all immediate forward refs for op(s) */
	struct SGS_ScriptOpData *next_bound;
	struct SGS_SymStr *label;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_id; /* not used by parser; for program module */
	uint32_t op_params;
	uint32_t time_ms, silence_ms;
	uint8_t wave;
	SGS_Ramp freq, freq2;
	SGS_Ramp amp, amp2;
	float phase;
	/* node adjacents in operator linkage graph */
	SGS_PtrList fmods, pmods, amods;
} SGS_ScriptOpData;

/**
 * Script data event flags.
 */
enum {
	SGS_SDEV_VOICE_LATER_USED = 1<<0,
	SGS_SDEV_ADD_WAIT_DURATION = 1<<1,
	SGS_SDEV_NEW_OPGRAPH = 1<<2,
};

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SGS_ScriptEvData {
	struct SGS_ScriptEvData *next;
	struct SGS_ScriptEvData *group_backref;
	struct SGS_ScriptEvData *composite;
	uint32_t wait_ms;
	SGS_PtrList operators; /* operators included in event */
	uint32_t ev_flags;
	/* voice parameters */
	uint32_t vo_id; /* not used by parser; for program module */
	uint32_t vo_params;
	struct SGS_ScriptEvData *voice_prev; /* preceding event for voice */
	SGS_Ramp pan;
	SGS_PtrList op_graph;
} SGS_ScriptEvData;

/**
 * Script data option flags.
 *
 * Set after parsing the setting of script options in a script.
 */
enum {
	SGS_SOPT_AMPMULT = 1<<0,
	SGS_SOPT_A4_FREQ = 1<<1,
	SGS_SOPT_DEF_TIME = 1<<2,
	SGS_SOPT_DEF_FREQ = 1<<3,
	SGS_SOPT_DEF_RATIO = 1<<4,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SGS_ScriptOptions {
	uint32_t changed; // flags (SGS_SOPT_*) set upon change by script
	float ampmult;    // amplitude multiplier for non-modulator operators
	float A4_freq;    // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_freq,
	      def_relfreq;
} SGS_ScriptOptions;

/**
 * Type returned after processing a file.
 */
typedef struct SGS_Script {
	SGS_ScriptEvData *events;
	const char *name; // currently simply set to the filename
	SGS_ScriptOptions sopt;
} SGS_Script;

SGS_Script *SGS_load_Script(const char *restrict script_arg, bool is_path);
void SGS_discard_Script(SGS_Script *restrict o);
