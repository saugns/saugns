/* sgensys: Script file data and functions.
 * Copyright (c) 2011-2012, 2017-2022 Joel K. Pettersson
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
#include "ptrarr.h"
#include "program.h"

/**
 * Script data operator flags.
 */
enum {
	SGS_SDOP_LATER_USED = 1<<0,
	SGS_SDOP_MULTIPLE = 1<<1,
	SGS_SDOP_NESTED = 1<<2,
};

/**
 * Node type for operator data.
 */
typedef struct SGS_ScriptOpData {
	struct SGS_ScriptEvData *event;
	struct SGS_ScriptOpData *on_prev; /* preceding for same op(s) */
	SGS_PtrArr on_next; /* all immediate forward refs for op(s) */
	struct SGS_ScriptOpData *next_bound;
	uint32_t op_flags;
	/* operator parameters */
	uint32_t op_id; /* not used by parser; for program module */
	uint32_t op_params;
	SGS_Time time;
	uint32_t silence_ms;
	uint8_t wave;
	uint8_t mods_set;
	SGS_Ramp freq, amp;
	float phase, dynfreq, dynamp;
	/* node adjacents in operator linkage graph */
	SGS_PtrArr fmods, pmods, amods;
} SGS_ScriptOpData;

/**
 * Script data event flags.
 */
enum {
	SGS_SDEV_VOICE_LATER_USED = 1<<0,
	SGS_SDEV_VOICE_SET_DUR    = 1<<1,
	SGS_SDEV_IMPLICIT_TIME    = 1<<2,
	SGS_SDEV_WAIT_PREV_DUR    = 1<<3, // compound step timing
	SGS_SDEV_FROM_GAPSHIFT    = 1<<4, // gapshift follow-on event
	SGS_SDEV_NEW_OPGRAPH      = 1<<5,
};

struct SGS_ScriptEvBranch;

/**
 * Node type for event data. Includes any voice and operator data part
 * of the event.
 */
typedef struct SGS_ScriptEvData {
	struct SGS_ScriptEvData *next;
	struct SGS_ScriptEvData *group_backref;
	struct SGS_ScriptEvBranch *forks;
	SGS_PtrArr operators; /* operators included in event */
	uint32_t ev_flags;
	uint32_t wait_ms;
	uint32_t dur_ms;
	/* voice parameters */
	uint32_t vo_id; /* not used by parser; for program module */
	uint32_t vo_params;
	struct SGS_ScriptEvData *voice_prev; /* preceding event for voice */
	SGS_Ramp pan;
	SGS_PtrArr op_graph;
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
	SGS_SOPT_DEF_RELFREQ = 1<<4,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SGS_ScriptOptions {
	uint32_t set;  // flags (SGS_SOPT_*) set upon change by script
	float ampmult; // amplitude multiplier for non-modulator operators
	float A4_freq; // A4 tuning for frequency as note
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

struct SGS_File;
SGS_Script *SGS_read_Script(struct SGS_File *restrict f) sgsMalloclike;
void SGS_discard_Script(SGS_Script *restrict o);
