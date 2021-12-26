/* saugns: Script file data and functions.
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
#include "program.h"

/**
 * Script data operator flags.
 */
enum {
	SAU_SDOP_LATER_USED = 1<<0,
	SAU_SDOP_MULTIPLE = 1<<1,
	SAU_SDOP_NESTED = 1<<2,
	SAU_SDOP_HAS_SUBEV = 1<<3,
};

/**
 * Node type for nested list data.
 */
typedef struct SAU_ScriptListData {
	struct SAU_ScriptRef *first_item;
	struct SAU_ScriptListData *next_list;
	uint8_t use_type;
} SAU_ScriptListData;

/**
 * Object type for all types, an instance of which is shared by all references.
 */
typedef struct SAU_ScriptObj {
	struct SAU_ScriptRef *last_ref;    // updated until timewise last
	struct SAU_ScriptEvData *root_event; // where object was created
	uint32_t obj_type;
	uint32_t obj_id; /* for conversion */
} SAU_ScriptObj;

/**
 * Reference type for object.
 */
typedef struct SAU_ScriptRef {
	struct SAU_ScriptRef *next_item;
	struct SAU_ScriptEvData *event;
	struct SAU_ScriptObj *obj;     /* shared by all references */
	struct SAU_ScriptRef *on_prev; /* preceding for same op(s) */
	struct SAU_SymStr *label;
	uint32_t op_flags;
	/* operator parameters */
	SAU_ProgramOpData *data;
	/* nested lists of references */
	SAU_ScriptListData *mods;
} SAU_ScriptRef;

/**
 * Script data event flags.
 */
enum {
	SAU_SDEV_VOICE_LATER_USED = 1<<0,
	SAU_SDEV_WAIT_PREV_DUR    = 1<<1, // composite event timing
	SAU_SDEV_FROM_GAPSHIFT    = 1<<2, // gapshift follow-on event
};

struct SAU_ScriptEvBranch;

/**
 * Node type for event data. Events are placed in time per script contents,
 * in a nested way during parsing and flattened after for later processing.
 *
 * The flow of time and nesting in a script end up two different dimensions
 * of data. Attached objects introduce (sub)trees of script contents, after
 * which they may also refer back to just parts of them in follow-on nodes.
 * (E.g. a tree of carriers and modulators in one event, and then an update
 * node for a modulator in the next event. An update could add a sub-tree.)
 */
typedef struct SAU_ScriptEvData {
	struct SAU_ScriptEvData *next;
	struct SAU_ScriptEvData *group_backref;
	struct SAU_ScriptEvBranch *forks;
	SAU_ScriptListData main_refs;
	uint32_t ev_flags;
	uint32_t wait_ms;
	uint32_t dur_ms;
	/* for conversion */
	uint32_t vo_id;
	struct SAU_ScriptEvData *root_ev; // if main object not created here
	size_t ev_id;
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
	SAU_SOPT_DEF_RELFREQ = 1<<4,
	SAU_SOPT_DEF_CHANMIX = 1<<5,
};

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct SAU_ScriptOptions {
	uint32_t set;   // flags (SAU_SOPT_*) set upon change by script
	float ampmult;  // amplitude multiplier for non-nested sound generators
	float A4_freq;  // A4 tuning for frequency as note
	/* sound generator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_freq,
	      def_relfreq,
	      def_chanmix;
} SAU_ScriptOptions;

/**
 * Type returned after processing a file.
 */
typedef struct SAU_Script {
	SAU_ScriptEvData *events;
	const char *name; // currently simply set to the filename
	SAU_ScriptOptions sopt;
	struct SAU_MemPool *mem; // holds memory for the specific script
	SAU_Program *program;    // holds data to run/render after built
} SAU_Script;

SAU_Script *SAU_load_Script(const char *restrict script_arg, bool is_path)
	sauMalloclike;
void SAU_discard_Script(SAU_Script *restrict o);
