/* SAU library: Script file data and functions.
 * Copyright (c) 2011-2012, 2017-2024 Joel K. Pettersson
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
 * Script data generator flags.
 */
enum {
	SAU_SDGEN_NESTED   = 1U<<0,
	SAU_SDGEN_MULTIPLE = 1U<<1,
};

/** Info per script data object, shared by all references to the object. */
typedef struct sauScriptObjInfo {
	uint8_t obj_type; // type of object described
	uint8_t gen_type; // type of audio generator, if such
	uint16_t last_vo_id; // for voice allocation (objects change voices)
	uint32_t last_gen_id; // ID for audio generator, if such
	uint32_t root_gen_obj; // root gen for gen
	uint32_t parent_gen_obj; // parent gen for any object
	uint32_t seed; // TODO: divide containing node type
	bool has_osc_parent;
} sauScriptObjInfo;

/** Reference to script data object, common data for all subtypes. */
typedef struct sauScriptObjRef {
	uint32_t obj_id; // shared by all references to an object
	uint8_t obj_type; // included for quick access
	uint8_t gen_type; // included for quick access
	uint16_t vo_id; // ID for carrier use, or SAU_PVO_NO_ID
	void *next; // next in set of objects
} sauScriptObjRef;

/**
 * Container node for linked list, used for nesting.
 */
typedef struct sauScriptListData {
	sauScriptObjRef ref;
	void *first_item;
	uint8_t use_type;
	bool append;
} sauScriptListData;

/**
 * Node type for generator data.
 */
typedef struct sauScriptGenData {
	sauScriptObjRef ref;
	struct sauScriptEvData *event;
	struct sauScriptGenData *prev_ref; // preceding for same gen(s)
	uint32_t gen_flags;
	/* generator parameters */
	uint32_t params;
	sauTime time;
	sauRange *amp, *pan;
	sauRange *freq;
	sauRange *pm_a;
	sauRange *pd_c, *pd_h, *pd_x, *pd_y;
	uint32_t phase;
	uint32_t seed;
	union sauPGenMode mode;
	/* node adjacents in generator linkage graph */
	sauScriptListData *mods;
} sauScriptGenData;

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
	void *main_obj;
	uint32_t wait_ms;
	uint32_t dur_ms; // for level at which main object is included
	uint8_t ev_flags;
} sauScriptEvData;

/** String and number pair for predefined values passed as arguments. */
typedef struct sauScriptPredef {
	const char *key;
	uint32_t len;
	double val;
} sauScriptPredef;

/** Specifies a script to parse (and possibly process further). */
typedef struct sauScriptArg {
	const char *str;
	bool is_path : 1;
	bool no_time : 1;
	sauScriptPredef *predef;
	size_t predef_count;
} sauScriptArg;

/**
 * Options set for a script, affecting parsing.
 *
 * The final state is included in the parse result.
 */
typedef struct sauScriptOptions {
	float ampmult; // global amplitude multiplier for whole script
	float A4_freq; // A4 tuning for frequency as note
	/* generator parameter default values (use depends on context) */
	uint32_t def_time_ms;
	float def_ampmult,
	      def_freq,
	      def_relfreq,
	      def_chanmix;
	int8_t note_key;
	uint8_t key_octave;
	uint8_t key_system;
	sauRasOpt def_ras;
	sauWaveOpt def_woo;
} sauScriptOptions;

/**
 * Type returned after processing a file. The data is divided into
 * two mempools, one specific to the parse and one shared with any
 * later program data (sauProgram), if built from the same parse.
 */
typedef struct sauScript {
	sauScriptEvData *events;
	sauScriptObjInfo *objects; // currently also gen info array
	sauScriptOptions sopt;
	uint32_t object_count;
	const char *name; // currently simply set to the filename
	struct sauSymtab *st;
} sauScript;
