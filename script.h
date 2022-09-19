/* sgensys: Script file data and functions.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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
	SGS_SDOP_LATER_USED = 1<<0,
	SGS_SDOP_MULTIPLE = 1<<1,
	SGS_SDOP_NESTED = 1<<2,
};

/**
 * Container node for linked list, used for nesting.
 */
typedef struct SGS_ScriptListData {
	struct SGS_ScriptOpData *first_item;
	struct SGS_ScriptListData *next;
	uint32_t count;
	uint8_t use_type;
	bool append;
} SGS_ScriptListData;

/** Info shared by all references to an object. */
typedef struct SGS_ScriptObjInfo {
	struct SGS_ScriptOpData *last_ref; // used for iterating references
	struct SGS_ScriptEvData *root_event;
	uint32_t id; // for conversion
} SGS_ScriptObjInfo;

/**
 * Node type for operator data.
 */
typedef struct SGS_ScriptOpData {
	struct SGS_ScriptEvData *event;
	struct SGS_ScriptOpData *next; // next in list, scope, grouping...
	struct SGS_ScriptObjInfo *info; // shared by all references
	struct SGS_ScriptOpData *prev_ref; // preceding for same op(s)
	uint32_t op_flags;
	/* operator parameters */
	uint32_t params;
	SGS_Time time;
	SGS_Ramp *pan;
	SGS_Ramp *amp, *amp2;
	SGS_Ramp *freq, *freq2;
	uint32_t phase;
	uint8_t wave;
	/* node adjacents in operator linkage graph */
	SGS_ScriptListData *mods;
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
	SGS_SDEV_LOCK_DUR_SCOPE   = 1<<5, // nested data can't lengthen dur
};

struct SGS_ScriptEvBranch;

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
typedef struct SGS_ScriptEvData {
	struct SGS_ScriptEvData *next;
	struct SGS_ScriptEvData *group_backref;
	struct SGS_ScriptEvBranch *forks;
	SGS_ScriptListData objs;
	uint32_t ev_flags;
	uint32_t wait_ms;
	uint32_t dur_ms;
	/* for conversion */
	uint32_t vo_id;
	struct SGS_ScriptEvData *root_ev; // if not the root event
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
	SGS_SOPT_DEF_CHANMIX = 1<<5,
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
	      def_relfreq,
	      def_chanmix;
} SGS_ScriptOptions;

/**
 * Type returned after processing a file. The data is divided into
 * two mempools, one specific to the parse and one shared with any
 * later program data (SGS_Program), if built from the same parse.
 */
typedef struct SGS_Script {
	SGS_ScriptEvData *events;
	const char *name; // currently simply set to the filename
	SGS_ScriptOptions sopt;
	struct SGS_Mempool *mp, *prg_mp;
	struct SGS_Symtab *st;
} SGS_Script;

SGS_Script *SGS_read_Script(const char *restrict script_arg, bool is_path) sgsMalloclike;
void SGS_discard_Script(SGS_Script *restrict o);
