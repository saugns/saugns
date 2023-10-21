/* SAU library: Script file data and functions.
 * Copyright (c) 2011-2012, 2017-2023 Joel K. Pettersson
 * <joelkp@tuta.io>.
 *
 * This file and the software of which it is part is distributed under the
 * terms of the GNU Lesser General Public License, either version 3 or (at
 * your option) any later version, WITHOUT ANY WARRANTY, not even of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * View the files COPYING.LESSER and COPYING for details, or if missing, see
 * <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "program.h"

/**
 * Script data operator flags.
 */
enum {
	SAU_SDOP_NESTED   = 1U<<0,
	SAU_SDOP_MULTIPLE = 1U<<1,
};

/** Info shared by all references to an object. */
typedef struct sauScriptObjInfo {
	uint8_t obj_type; // type of object described
	uint16_t last_vo_id; // for voice allocation (objects change voices)
	uint32_t root_obj_id; // root carrier for a carrier or modulator
	uint32_t parent_obj_id; // parent carrier for a carrier or modulator
	uint32_t seed; // TODO: divide containing node type
} sauScriptObjInfo;

/**
 * Container node for linked list, used for nesting.
 */
typedef struct sauScriptListData {
	void *first_item;
	struct sauScriptListData *next_list;
	uint32_t parent_obj_id;
	uint8_t use_type;
	uint8_t flags;
} sauScriptListData;

enum {
	SAU_SDLI_APPEND = 1U<<0,
};

/**
 * Node type for operator data.
 */
typedef struct sauScriptOpData {
	struct sauScriptEvData *event;
	struct sauScriptOpData *next; // next in list, scope, grouping...
	struct sauScriptOpData *prev_ref; // preceding for same op(s)
	uint32_t obj_id; // shared by all references to an object
	uint8_t obj_type; // included for quick access
	uint16_t vo_id; // ID for carrier use, or SAU_PVO_NO_ID
	uint32_t op_flags;
	/* operator parameters */
	uint32_t params;
	sauTime time;
	sauLine *pan;
	sauLine *amp, *amp2;
	sauLine *freq, *freq2;
	uint32_t phase;
	uint8_t wave;
	sauRasOpt ras_opt; // TODO: divide containing node type
	/* node adjacents in operator linkage graph */
	sauScriptListData *mods;
} sauScriptOpData;

/**
 * Script data event flags.
 */
enum {
	SAU_SDEV_ASSIGN_VOICE     = 1U<<0, // numbered voice has new carrier
	SAU_SDEV_VOICE_SET_DUR    = 1U<<1,
	SAU_SDEV_IMPLICIT_TIME    = 1U<<2,
	SAU_SDEV_WAIT_PREV_DUR    = 1U<<3, // compound step timing
	SAU_SDEV_FROM_GAPSHIFT    = 1U<<4, // gapshift follow-on event
	SAU_SDEV_LOCK_DUR_SCOPE   = 1U<<5, // nested data can't lengthen dur
};

struct sauScriptEvBranch;

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
typedef struct sauScriptEvData {
	struct sauScriptEvData *next;
	struct sauScriptEvBranch *forks;
	sauScriptListData objs;
	uint32_t wait_ms;
	uint32_t dur_ms;
	uint8_t ev_flags;
} sauScriptEvData;

/**
 * Script data option flags.
 *
 * Set after parsing the setting of script options in a script.
 */
enum {
	SAU_SOPT_AMPMULT     = 1U<<0,
	SAU_SOPT_DEF_CHANMIX = 1U<<1,
	SAU_SOPT_DEF_TIME    = 1U<<2,
	SAU_SOPT_DEF_FREQ    = 1U<<3,
	SAU_SOPT_DEF_RELFREQ = 1U<<4,
	SAU_SOPT_A4_FREQ     = 1U<<5,
	SAU_SOPT_NOTE_KEY    = 1U<<6,
	SAU_SOPT_NOTE_SCALE  = 1U<<7,
};

/** Specifies a script to parse (and possibly process further). */
typedef struct sauScriptArg {
	const char *str;
	bool is_path : 1;
	bool no_time : 1;
} sauScriptArg;

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct sauScriptOptions {
	uint32_t set;  // flags (SAU_SOPT_*) set upon change by script
	float ampmult; // amplitude multiplier for non-modulator operators
	float A4_freq; // A4 tuning for frequency as note
	/* operator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_freq,
	      def_relfreq,
	      def_chanmix;
	int8_t note_key;
	uint8_t key_octave;
	uint8_t key_system;
} sauScriptOptions;

/**
 * Type returned after processing a file. The data is divided into
 * two mempools, one specific to the parse and one shared with any
 * later program data (sauProgram), if built from the same parse.
 */
typedef struct sauScript {
	sauScriptEvData *events;
	sauScriptObjInfo *objects; // currently also op info array
	sauScriptOptions sopt;
	uint32_t duration_ms;
	uint16_t voice_count;
	uint32_t object_count;
	const char *name; // currently simply set to the filename
	struct sauMempool *mp, *prg_mp;
	struct sauSymtab *st;
} sauScript;

sauScript *sau_read_Script(const sauScriptArg *restrict arg) sauMalloclike;
void sau_discard_Script(sauScript *restrict o);
